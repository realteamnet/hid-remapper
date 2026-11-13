#include "vdisk.h"

#include <strings.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <tusb.h>

#include "config.h"
#include "crc.h"
#include "globals.h"
#include "interval_override.h"
#include "platform.h"
#include "remapper.h"

// FAT12 volume layout
#define SECTOR 512
#define SECTORS_PER_CLUSTER 8
#define CLUSTER (SECTOR * SECTORS_PER_CLUSTER)
#define TOTAL_SECTORS 8192
#define FAT_SECTORS 3
#define ROOT_ENTRIES 64
#define FAT_LBA 1
#define ROOT_LBA (FAT_LBA + 2 * FAT_SECTORS)
#define DATA_LBA (ROOT_LBA + ROOT_ENTRIES / 16)
#define FAT_DATE 0x5C21  // 2026-01-01

#ifdef VDISK_WEB_TOOL
extern const uint8_t vdisk_tool_start[];
extern const uint8_t vdisk_tool_end[];
#define TOOL_SIZE ((uint32_t) (vdisk_tool_end - vdisk_tool_start))
#else
#define TOOL_SIZE 0u
#endif

#define APPLY_CLUSTER 2
#define README_CLUSTER 3
#define STATUS_CLUSTER 4
#define TOOL_CLUSTER 5
#define JSON_CLUSTER (TOOL_CLUSTER + (TOOL_SIZE + CLUSTER - 1) / CLUSTER)

#define VOLUME_LABEL "HIDREMAPPER"
#define JSON_NAME "config.json"
#define JSON_SHORT_NAME "CONFIG~1JSO"

#define QUIET_US 1500000
#define EJECT_US 2500000

// same bits as in config.cc
#define FLAG_IGNORE_AUTH_DEV_INPUTS (1 << 4)
#define FLAG_GPIO_OUTPUT_MODE (1 << 5)
#define FLAG_NORMALIZE_GAMEPAD_INPUTS (1 << 6)

#define STICKY_FLAG (1 << 0)
#define TAP_FLAG (1 << 1)
#define HOLD_FLAG (1 << 2)

static const char readme[] =
    "HID Remapper configuration drive\r\n"
    "\r\n"
    JSON_NAME "  The current configuration. Copy it off the drive to back it up.\r\n"
    "APPLY        Copy a configuration .json file into this folder to load it\r\n"
    "             and save it on the device. Copy a firmware .uf2 file into it\r\n"
    "             to restart in firmware flashing mode; then copy the .uf2\r\n"
    "             file to the RPI-RP2 drive that appears.\r\n"
    "STATUS.TXT   Result of the last file copied into APPLY.\r\n"
#ifdef VDISK_WEB_TOOL
    "WEBTOOL.HTM  The configuration tool. Open it in Chrome or Edge.\r\n"
#endif
    "\r\n"
    "The drive is rebuilt from the device every time, so nothing else that is\r\n"
    "written to it is kept.\r\n";

// in Op enum order (types.h)
static const char* const op_names[] = {
    "push", "push_usage", "input_state", "add", "mul", "eq", "time", "mod", "gt", "not",
    "input_state_binary", "abs", "dup", "sin", "cos", "debug", "auto_repeat", "relu", "clamp", "scaling",
    "layer_state", "sticky_state", "tap_state", "hold_state", "bitwise_or", "bitwise_and", "bitwise_not",
    "prev_input_state", "prev_input_state_binary", "store", "recall", "sqrt", "atan2", "round", "port",
    "dpad", "eol", "input_state_fp32", "prev_input_state_fp32", "min", "max", "ifte", "div", "swap",
    "monitor", "sign", "sub", "print_if", "time_sec", "lt", "plugged_in", "input_state_scaled",
    "prev_input_state_scaled", "deadzone", "deadzone2"
};
#define NOPS (sizeof(op_names) / sizeof(op_names[0]))

enum {
    SEC_NONE,
    SEC_VERSION,
    SEC_UNMAPPED_PASSTHROUGH_LAYERS,
    SEC_PARTIAL_SCROLL_TIMEOUT,
    SEC_TAP_HOLD_THRESHOLD,
    SEC_GPIO_DEBOUNCE_TIME_MS,
    SEC_INTERVAL_OVERRIDE,
    SEC_OUR_DESCRIPTOR_NUMBER,
    SEC_IGNORE_AUTH_DEV_INPUTS,
    SEC_MACRO_ENTRY_DURATION,
    SEC_GPIO_OUTPUT_MODE,
    SEC_NORMALIZE_GAMEPAD_INPUTS,
    SEC_MAPPINGS,
    SEC_MACROS,
    SEC_EXPRESSIONS,
    SEC_QUIRKS,
};

static const char* const root_keys[] = {
    "", "version", "unmapped_passthrough_layers", "partial_scroll_timeout", "tap_hold_threshold",
    "gpio_debounce_time_ms", "interval_override", "our_descriptor_number", "ignore_auth_dev_inputs",
    "macro_entry_duration", "gpio_output_mode", "normalize_gamepad_inputs", "mappings", "macros",
    "expressions", "quirks", nullptr
};

enum {
    F_NONE,
    F_SOURCE_USAGE,
    F_TARGET_USAGE,
    F_SCALING,
    F_STICKY,
    F_TAP,
    F_HOLD,
    F_SOURCE_PORT,
    F_TARGET_PORT,
    F_LAYERS,
    F_VENDOR_ID,
    F_PRODUCT_ID,
    F_INTERFACE,
    F_REPORT_ID,
    F_USAGE,
    F_BITPOS,
    F_SIZE,
    F_RELATIVE,
    F_SIGNED,
};

static const char* const item_keys[] = {
    "", "source_usage", "target_usage", "scaling", "sticky", "tap", "hold", "source_port", "target_port",
    "layers", "vendor_id", "product_id", "interface", "report_id", "usage", "bitpos", "size", "relative",
    "signed", nullptr
};

enum { LEX_NONE, LEX_STRING, LEX_ESCAPE, LEX_LITERAL };
enum { IDLE, PARSING, DONE, FAILED };

struct import_t {
    uint8_t state = IDLE;
    uint16_t cluster = 0;
    uint32_t next_lba = 0;
    uint32_t bytes = 0;
    const char* error = nullptr;

    uint8_t lex = LEX_NONE;
    bool is_key = false;
    bool in_expr = false;
    bool comment = false;
    bool expect_key = false;
    char prev = 0;
    char tok[40];
    uint8_t tok_len = 0;
    uint8_t depth = 0;
    uint8_t objects = 0;  // bit per depth
    uint8_t section = SEC_NONE;
    uint8_t field = F_NONE;

    int32_t version = 0;
    persist_config_t header;
    mapping_config11_t mapping;
    quirk_t quirk;
    std::vector<uint8_t> mappings, macros, expressions, quirks;  // in persisted format
    uint8_t n_macros = 0;
    uint8_t n_expressions = 0;
    size_t macro_idx = 0;
    size_t entry_idx = 0;
    size_t expr_idx = 0;
};

static import_t im;
static struct {
    uint16_t cluster;
    uint32_t size;
} dropped[4];
static uint8_t dropped_next = 0;

static char status[80] = "No file has been copied into APPLY yet.\r\n";
static uint32_t json_size = 0;
static bool apply_pending = false;
static uint16_t uf2_cluster = 0;
static bool bootloader_pending = false;
static bool changed = false;
static bool host_ejected = false;
static uint64_t last_write = 0;
static uint64_t eject_until = 0;

static uint8_t* out_buf;
static uint32_t out_start;
static uint32_t out_pos;

static void put(const char* s) {
    for (; *s; s++, out_pos++) {
        if ((out_buf != nullptr) && (out_pos - out_start < SECTOR)) {
            out_buf[out_pos - out_start] = *s;
        }
    }
}

static void putf(const char* fmt, ...) {
    char tmp[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, args);
    va_end(args);
    put(tmp);
}

static void put_layers(uint8_t mask) {
    const char* sep = "";
    put("[");
    for (int i = 0; i < 8; i++) {
        if (mask & (1 << i)) {
            putf("%s%d", sep, i);
            sep = ", ";
        }
    }
    put("]");
}

static const char* tf(bool x) {
    return x ? "true" : "false";
}

// Writes the part of the JSON that falls in [start, start + SECTOR) to buf and returns the total length.
static uint32_t generate_json(uint8_t* buf, uint32_t start) {
    out_buf = buf;
    out_start = start;
    out_pos = 0;

    putf("{\n\"version\": %d,\n\"unmapped_passthrough_layers\": ", VDISK_CONFIG_VERSION);
    put_layers(unmapped_passthrough_layer_mask);
    putf(",\n\"partial_scroll_timeout\": %lu,\n\"tap_hold_threshold\": %lu,\n",
        (unsigned long) partial_scroll_timeout, (unsigned long) tap_hold_threshold);
    putf("\"gpio_debounce_time_ms\": %d,\n\"interval_override\": %d,\n\"our_descriptor_number\": %d,\n",
        (int) (gpio_debounce_time / 1000), interval_override, our_descriptor_number);
    putf("\"ignore_auth_dev_inputs\": %s,\n\"macro_entry_duration\": %d,\n\"gpio_output_mode\": %d,\n",
        tf(ignore_auth_dev_inputs), macro_entry_duration + 1, gpio_output_mode);
    putf("\"input_labels\": 0,\n\"normalize_gamepad_inputs\": %s,\n\"mappings\": [", tf(normalize_gamepad_inputs));

    const char* sep = "\n";
    for (auto const& m : config_mappings) {
        putf("%s{\"source_usage\": \"0x%08lx\", \"target_usage\": \"0x%08lx\", \"layers\": ", sep,
            (unsigned long) m.source_usage, (unsigned long) m.target_usage);
        put_layers(m.layer_mask);
        putf(", \"sticky\": %s, \"tap\": %s, \"hold\": %s, \"scaling\": %ld, \"source_port\": %d, \"target_port\": %d}",
            tf(m.flags & STICKY_FLAG), tf(m.flags & TAP_FLAG), tf(m.flags & HOLD_FLAG), (long) m.scaling,
            m.hub_ports & 0x0F, m.hub_ports >> 4);
        sep = ",\n";
    }

    put("\n],\n\"macros\": [\n");
    my_mutex_enter(MutexId::MACROS);
    for (int i = 0; i < NMACROS; i++) {
        put("[");
        sep = "";
        for (auto const& entry : macros[i]) {
            putf("%s[", sep);
            sep = "";
            for (uint32_t usage : entry) {
                putf("%s\"0x%08lx\"", sep, (unsigned long) usage);
                sep = ", ";
            }
            put("]");
            sep = ", ";
        }
        put(i < NMACROS - 1 ? "],\n" : "]\n");
    }
    my_mutex_exit(MutexId::MACROS);

    put("],\n\"expressions\": [\n");
    my_mutex_enter(MutexId::EXPRESSIONS);
    for (int i = 0; i < NEXPRESSIONS; i++) {
        put("\"");
        sep = "";
        for (auto const& elem : expressions[i]) {
            put(sep);
            sep = " ";
            if (elem.op == Op::PUSH) {
                putf("%ld", (long) (int32_t) elem.val);
            } else if (elem.op == Op::PUSH_USAGE) {
                putf("0x%08lx", (unsigned long) elem.val);
            } else if ((uint8_t) elem.op < NOPS) {
                put(op_names[(uint8_t) elem.op]);
            }
        }
        put(i < NEXPRESSIONS - 1 ? "\",\n" : "\"\n");
    }
    my_mutex_exit(MutexId::EXPRESSIONS);

    put("],\n\"quirks\": [");
    sep = "\n";
    my_mutex_enter(MutexId::QUIRKS);
    for (auto const& q : quirks) {
        putf("%s{\"vendor_id\": \"0x%04x\", \"product_id\": \"0x%04x\", \"interface\": %d, \"report_id\": %d, ", sep,
            q.vendor_id, q.product_id, q.interface, q.report_id);
        putf("\"usage\": \"0x%08lx\", \"bitpos\": %d, \"size\": %d, \"relative\": %s, \"signed\": %s}",
            (unsigned long) q.usage, q.bitpos, q.size_flags & QUIRK_SIZE_MASK,
            tf(q.size_flags & QUIRK_FLAG_RELATIVE_MASK), tf(q.size_flags & QUIRK_FLAG_SIGNED_MASK));
        sep = ",\n";
    }
    my_mutex_exit(MutexId::QUIRKS);
    put("\n]\n}\n");

    return out_pos;
}

static uint32_t json_clusters() {
    if (json_size == 0) {
        json_size = generate_json(nullptr, 0);
    }
    return (json_size + CLUSTER - 1) / CLUSTER;
}

static void dir_entry(uint8_t* e, const char* name, uint8_t attr, uint16_t cluster, uint32_t size) {
    memcpy(e, name, 11);
    e[11] = attr;
    e[16] = e[24] = FAT_DATE & 0xFF;
    e[17] = e[25] = FAT_DATE >> 8;
    e[26] = cluster & 0xFF;
    e[27] = cluster >> 8;
    memcpy(e + 28, &size, 4);
}

// one long-name entry: name up to 13 characters
static void lfn_entry(uint8_t* e, const char* name, const char* short_name) {
    static const uint8_t pos[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++) {
        sum = ((sum & 1) << 7) + (sum >> 1) + short_name[i];
    }
    memset(e, 0xFF, 32);
    e[0] = 0x41;
    e[11] = 0x0F;
    e[12] = 0;
    e[13] = sum;
    e[26] = e[27] = 0;
    size_t len = strlen(name);
    for (size_t i = 0; (i < 13) && (i <= len); i++) {
        e[pos[i]] = name[i];
        e[pos[i] + 1] = 0;
    }
}

static uint16_t fat_entry(uint32_t cluster) {
    if (cluster == 0) {
        return 0xFF8;
    }
    if (cluster < TOOL_CLUSTER) {
        return 0xFFF;
    }
    uint32_t end = (cluster < JSON_CLUSTER) ? JSON_CLUSTER : JSON_CLUSTER + json_clusters();
    if (cluster >= end) {
        return 0;
    }
    return (cluster == end - 1) ? 0xFFF : cluster + 1;
}

static void fat_sector(uint32_t sector, uint8_t* buf) {
    uint32_t cluster = sector * SECTOR * 2 / 3;
    if (cluster > 0) {
        cluster--;
    }
    for (;; cluster++) {
        int32_t o = (int32_t) (cluster * 3 / 2) - (int32_t) (sector * SECTOR);
        if (o >= SECTOR) {
            break;
        }
        uint16_t v = fat_entry(cluster);
        uint8_t b0 = (cluster & 1) ? (v << 4) : v;
        uint8_t b1 = (cluster & 1) ? (v >> 4) : (v >> 8);
        if (o >= 0) {
            buf[o] |= b0;
        }
        if ((o + 1 >= 0) && (o + 1 < SECTOR)) {
            buf[o + 1] |= b1;
        }
    }
}

static void text_sector(const char* text, uint32_t offset, uint8_t* buf) {
    size_t len = strlen(text);
    if (offset < len) {
        memcpy(buf, text + offset, (len - offset < SECTOR) ? len - offset : SECTOR);
    }
}

static void read_sector(uint32_t lba, uint8_t* buf) {
    static const uint8_t boot[] = {
        0xEB, 0x3C, 0x90, 'M', 'S', 'D', 'O', 'S', '5', '.', '0',
        SECTOR & 0xFF, SECTOR >> 8, SECTORS_PER_CLUSTER, 1, 0, 2, ROOT_ENTRIES, 0,
        TOTAL_SECTORS & 0xFF, TOTAL_SECTORS >> 8, 0xF8, FAT_SECTORS, 0, 63, 0, 255, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0x80, 0, 0x29, 0x52, 0x4D, 0x50, 0x52
    };

    memset(buf, 0, SECTOR);
    if (lba == 0) {
        memcpy(buf, boot, sizeof(boot));
        memcpy(buf + 43, VOLUME_LABEL, 11);
        memcpy(buf + 54, "FAT12   ", 8);
        buf[510] = 0x55;
        buf[511] = 0xAA;
    } else if (lba < ROOT_LBA) {
        fat_sector((lba - FAT_LBA) % FAT_SECTORS, buf);
    } else if (lba == ROOT_LBA) {
        dir_entry(buf, VOLUME_LABEL, 0x08, 0, 0);
        dir_entry(buf + 32, "APPLY      ", 0x10, APPLY_CLUSTER, 0);
        dir_entry(buf + 64, "README  TXT", 0x20, README_CLUSTER, sizeof(readme) - 1);
        dir_entry(buf + 96, "STATUS  TXT", 0x20, STATUS_CLUSTER, strlen(status));
        lfn_entry(buf + 128, JSON_NAME, JSON_SHORT_NAME);
        json_clusters();
        dir_entry(buf + 160, JSON_SHORT_NAME, 0x20, JSON_CLUSTER, json_size);
        if (TOOL_SIZE > 0) {
            dir_entry(buf + 192, "WEBTOOL HTM", 0x20, TOOL_CLUSTER, TOOL_SIZE);
        }
    } else if (lba >= DATA_LBA) {
        uint32_t cluster = 2 + (lba - DATA_LBA) / SECTORS_PER_CLUSTER;
        uint32_t offset = (lba - DATA_LBA) % SECTORS_PER_CLUSTER * SECTOR;
        if (cluster == APPLY_CLUSTER) {
            if (offset == 0) {
                dir_entry(buf, ".          ", 0x10, APPLY_CLUSTER, 0);
                dir_entry(buf + 32, "..         ", 0x10, 0, 0);
            }
        } else if (cluster == README_CLUSTER) {
            text_sector(readme, offset, buf);
        } else if (cluster == STATUS_CLUSTER) {
            text_sector(status, offset, buf);
#ifdef VDISK_WEB_TOOL
        } else if (cluster < JSON_CLUSTER) {
            uint32_t pos = (cluster - TOOL_CLUSTER) * CLUSTER + offset;
            if (pos < TOOL_SIZE) {
                memcpy(buf, vdisk_tool_start + pos, (TOOL_SIZE - pos < SECTOR) ? TOOL_SIZE - pos : SECTOR);
            }
#endif
        } else if (cluster < JSON_CLUSTER + json_clusters()) {
            generate_json(buf, (cluster - JSON_CLUSTER) * CLUSTER + offset);
        }
    }
}

static void fail(const char* error) {
    if (im.state == PARSING) {
        im.state = FAILED;
        im.error = error;
    }
}

static uint8_t find_key(const char* const* keys) {
    for (uint8_t i = 1; keys[i] != nullptr; i++) {
        if (strcmp(im.tok, keys[i]) == 0) {
            return i;
        }
    }
    return 0;
}

static void append(std::vector<uint8_t>& v, const void* data, size_t len) {
    v.insert(v.end(), (const uint8_t*) data, (const uint8_t*) data + len);
}

static void tok_add(char c) {
    if (im.tok_len < sizeof(im.tok) - 1) {
        im.tok[im.tok_len++] = c;
    }
}

static void set_flag(uint8_t flag, bool value) {
    im.header.flags = (im.header.flags & ~flag) | (value ? flag : 0);
}

static void json_value(uint32_t v) {
    persist_config_t& h = im.header;
    mapping_config11_t& m = im.mapping;
    quirk_t& q = im.quirk;
    uint8_t d = im.depth;

    if (d == 1) {
        switch (im.section) {
            case SEC_VERSION:
                im.version = v;
                break;
            case SEC_PARTIAL_SCROLL_TIMEOUT:
                h.partial_scroll_timeout = v;
                break;
            case SEC_TAP_HOLD_THRESHOLD:
                h.tap_hold_threshold = v;
                break;
            case SEC_GPIO_DEBOUNCE_TIME_MS:
                h.gpio_debounce_time_ms = v;
                break;
            case SEC_INTERVAL_OVERRIDE:
                h.interval_override = v;
                break;
            case SEC_OUR_DESCRIPTOR_NUMBER:
                h.our_descriptor_number = v;
                break;
            case SEC_IGNORE_AUTH_DEV_INPUTS:
                set_flag(FLAG_IGNORE_AUTH_DEV_INPUTS, v);
                break;
            case SEC_MACRO_ENTRY_DURATION:
                h.macro_entry_duration = (v > 0) ? v - 1 : 0;
                break;
            case SEC_GPIO_OUTPUT_MODE:
                set_flag(FLAG_GPIO_OUTPUT_MODE, v);
                break;
            case SEC_NORMALIZE_GAMEPAD_INPUTS:
                set_flag(FLAG_NORMALIZE_GAMEPAD_INPUTS, v);
                break;
        }
    } else if ((im.section == SEC_UNMAPPED_PASSTHROUGH_LAYERS) && (d == 2)) {
        h.unmapped_passthrough_layer_mask |= 1 << (v & 7);
    } else if ((im.section == SEC_MAPPINGS) && (d == 3)) {
        switch (im.field) {
            case F_SOURCE_USAGE:
                m.source_usage = v;
                break;
            case F_TARGET_USAGE:
                m.target_usage = v;
                break;
            case F_SCALING:
                m.scaling = v;
                break;
            case F_STICKY:
                m.flags |= v ? STICKY_FLAG : 0;
                break;
            case F_TAP:
                m.flags |= v ? TAP_FLAG : 0;
                break;
            case F_HOLD:
                m.flags |= v ? HOLD_FLAG : 0;
                break;
            case F_SOURCE_PORT:
                m.hub_ports |= v & 0x0F;
                break;
            case F_TARGET_PORT:
                m.hub_ports |= (v & 0x0F) << 4;
                break;
        }
    } else if ((im.section == SEC_MAPPINGS) && (d == 4) && (im.field == F_LAYERS)) {
        m.layer_mask |= 1 << (v & 7);
    } else if ((im.section == SEC_MACROS) && (d == 4) && (im.n_macros < NMACROS)) {
        if (im.macros[im.entry_idx] == 255) {
            return fail("macro entry too long");
        }
        im.macros[im.entry_idx]++;
        append(im.macros, &v, 4);
    } else if ((im.section == SEC_QUIRKS) && (d == 3)) {
        switch (im.field) {
            case F_VENDOR_ID:
                q.vendor_id = v;
                break;
            case F_PRODUCT_ID:
                q.product_id = v;
                break;
            case F_INTERFACE:
                q.interface = v;
                break;
            case F_REPORT_ID:
                q.report_id = v;
                break;
            case F_USAGE:
                q.usage = v;
                break;
            case F_BITPOS:
                q.bitpos = v;
                break;
            case F_SIZE:
                q.size_flags |= v & QUIRK_SIZE_MASK;
                break;
            case F_RELATIVE:
                q.size_flags |= v ? QUIRK_FLAG_RELATIVE_MASK : 0;
                break;
            case F_SIGNED:
                q.size_flags |= v ? QUIRK_FLAG_SIGNED_MASK : 0;
                break;
        }
    }
}

static void json_begin(bool object) {
    if (++im.depth > 6) {
        return fail("nested too deep");
    }
    im.objects = (im.objects & ~(1 << im.depth)) | (object << im.depth);
    im.expect_key = object;
    uint8_t d = im.depth;

    if (im.section == SEC_MAPPINGS) {
        if (d == 3) {
            im.mapping = mapping_config11_t();
            im.mapping.scaling = 1000;
            im.mapping.layer_mask = 1;
            im.field = F_NONE;
        } else if ((d == 4) && (im.field == F_LAYERS)) {
            im.mapping.layer_mask = 0;
        }
    } else if ((im.section == SEC_UNMAPPED_PASSTHROUGH_LAYERS) && (d == 2)) {
        im.header.unmapped_passthrough_layer_mask = 0;
    } else if ((im.section == SEC_MACROS) && (im.n_macros < NMACROS)) {
        if (d == 3) {
            im.macro_idx = im.macros.size();
            im.macros.push_back(0);
        } else if (d == 4) {
            if (im.macros[im.macro_idx] == 255) {
                return fail("macro too long");
            }
            im.macros[im.macro_idx]++;
            im.entry_idx = im.macros.size();
            im.macros.push_back(0);
        }
    } else if ((im.section == SEC_QUIRKS) && (d == 3)) {
        im.quirk = quirk_t();
        im.field = F_NONE;
    }
}

static void json_end(bool object) {
    if ((im.depth == 0) || (object != !!(im.objects & (1 << im.depth)))) {
        return fail("brackets do not match");
    }
    uint8_t d = im.depth--;
    im.expect_key = false;

    if ((d == 3) && (im.section == SEC_MAPPINGS)) {
        append(im.mappings, &im.mapping, sizeof(im.mapping));
        im.header.mapping_count++;
    } else if ((d == 3) && (im.section == SEC_MACROS) && (im.n_macros < NMACROS)) {
        // the config tool drops an empty last entry
        if ((im.macros[im.macro_idx] > 0) && (im.entry_idx == im.macros.size() - 1)) {
            im.macros.pop_back();
            im.macros[im.macro_idx]--;
        }
        im.n_macros++;
    } else if ((d == 3) && (im.section == SEC_QUIRKS)) {
        append(im.quirks, &im.quirk, sizeof(im.quirk));
        im.header.quirk_count++;
    } else if (d == 1) {
        if (im.version != VDISK_CONFIG_VERSION) {
            return fail("config version is not supported");
        }
        im.state = DONE;
    }
}

static void expr_word() {
    if (im.tok_len == 0) {
        return;
    }
    im.tok[im.tok_len] = 0;
    im.tok_len = 0;

    uint8_t op = 0;
    uint32_t val = 0;
    if ((im.tok[0] == '0') && ((im.tok[1] | 0x20) == 'x')) {
        op = (uint8_t) Op::PUSH_USAGE;
        val = strtoul(im.tok, nullptr, 16);
    } else if (((im.tok[0] >= '0') && (im.tok[0] <= '9')) || (im.tok[0] == '-')) {
        op = (uint8_t) Op::PUSH;
        val = strtoul(im.tok, nullptr, 10);
    } else {
        for (op = 2; (op < NOPS) && (strcasecmp(im.tok, op_names[op]) != 0); op++) {
        }
        if (op == NOPS) {
            return fail("unknown word in an expression");
        }
    }

    if (im.n_expressions >= NEXPRESSIONS) {
        return;
    }
    im.expressions.push_back(op);
    if (op <= (uint8_t) Op::PUSH_USAGE) {
        append(im.expressions, &val, 4);
    }
    if (++im.expressions[im.expr_idx] == 0) {
        im.expressions[im.expr_idx + 1]++;
    }
}

static void string_char(char c) {
    if (!im.in_expr) {
        return tok_add(c);
    }
    if (im.comment) {
        im.comment = !((c == '/') && (im.prev == '*'));
        im.prev = c;
    } else if ((c == '*') && (im.prev == '/')) {
        im.tok_len--;
        expr_word();
        im.comment = true;
        im.prev = 0;
    } else {
        im.prev = c;
        if (c == ' ') {
            expr_word();
        } else {
            tok_add(c);
        }
    }
}

static void string_end() {
    im.tok[im.tok_len] = 0;
    if (im.is_key) {
        im.expect_key = false;
        if (im.depth == 1) {
            im.section = find_key(root_keys);
        } else if (im.depth == 3) {
            im.field = find_key(item_keys);
        }
    } else if (im.in_expr) {
        expr_word();
        im.n_expressions += (im.n_expressions < 255);
    } else {
        json_value(strtoul(im.tok, nullptr, 16));
    }
}

static void literal_end() {
    im.tok[im.tok_len] = 0;
    if (strcmp(im.tok, "true") == 0) {
        json_value(1);
    } else if ((strcmp(im.tok, "false") == 0) || (strcmp(im.tok, "null") == 0)) {
        json_value(0);
    } else if ((im.tok[0] == '-') || ((im.tok[0] >= '0') && (im.tok[0] <= '9'))) {
        json_value(strtoul(im.tok, nullptr, 10));
    } else {
        fail("unexpected word");
    }
}

static void json_char(char c) {
    switch (im.lex) {
        case LEX_STRING:
            if (c == '\\') {
                im.lex = LEX_ESCAPE;
            } else if (c == '"') {
                im.lex = LEX_NONE;
                string_end();
            } else {
                string_char(c);
            }
            return;
        case LEX_ESCAPE:
            im.lex = LEX_STRING;
            string_char(((c == 'n') || (c == 'r') || (c == 't') || (c == 'u')) ? ' ' : c);
            return;
        case LEX_LITERAL:
            if (((c >= '0') && (c <= '9')) || ((c >= 'a') && (c <= 'z')) || (c == '.') || (c == '-') || (c == '+') || (c == 'E')) {
                tok_add(c);
                return;
            }
            im.lex = LEX_NONE;
            literal_end();
            break;
    }

    switch (c) {
        case '{':
        case '[':
            json_begin(c == '{');
            break;
        case '}':
        case ']':
            json_end(c == '}');
            break;
        case '"':
            im.lex = LEX_STRING;
            im.tok_len = 0;
            im.is_key = im.expect_key;
            im.in_expr = !im.is_key && (im.section == SEC_EXPRESSIONS) && (im.depth == 2);
            im.comment = false;
            im.prev = 0;
            if (im.in_expr && (im.n_expressions < NEXPRESSIONS)) {
                im.expr_idx = im.expressions.size();
                im.expressions.resize(im.expr_idx + 2);
            }
            break;
        case ',':
            im.expect_key = im.objects & (1 << im.depth);
            break;
        case ':':
        case ' ':
        case '\t':
        case '\r':
        case '\n':
            break;
        default:
            if (((c >= '0') && (c <= '9')) || ((c >= 'a') && (c <= 'z')) || (c == '-')) {
                im.lex = LEX_LITERAL;
                im.tok_len = 0;
                tok_add(c);
            } else {
                fail((c == 0) ? "file is not a complete configuration" : "unexpected character");
            }
    }
}

static size_t staged_size() {
    uint8_t n_macros = (im.n_macros < NMACROS) ? im.n_macros : NMACROS;
    uint8_t n_expressions = (im.n_expressions < NEXPRESSIONS) ? im.n_expressions : NEXPRESSIONS;
    return sizeof(persist_config_t) + im.mappings.size() + im.macros.size() + (NMACROS - n_macros) +
           im.expressions.size() + 2 * (NEXPRESSIONS - n_expressions) + im.quirks.size() + 4;
}

// a config file starts with { and a quoted key
static bool looks_like_json(const uint8_t* buf) {
    const uint8_t* p = buf;
    if ((p[0] == 0xEF) && (p[1] == 0xBB) && (p[2] == 0xBF)) {
        p += 3;
    }
    bool brace = false;
    for (; p < buf + SECTOR; p++) {
        if ((*p == ' ') || (*p == '\t') || (*p == '\r') || (*p == '\n')) {
            continue;
        }
        if (!brace && (*p == '{')) {
            brace = true;
            continue;
        }
        return brace && (*p == '"');
    }
    return false;
}

static void set_status(const char* prefix, const char* text) {
    snprintf(status, sizeof(status), "%s%s\r\n", prefix, text);
    changed = true;
}

static void check_dropped() {
    for (auto const& d : dropped) {
        if ((uf2_cluster != 0) && (d.cluster == uf2_cluster)) {
            bootloader_pending = true;
        }
    }
    if ((im.state == IDLE) || apply_pending) {
        return;
    }
    for (auto const& d : dropped) {
        if (d.cluster != im.cluster) {
            continue;
        }
        if ((im.state == PARSING) && (im.bytes >= d.size)) {
            fail("file is not a complete configuration");
        }
        if (im.state == DONE) {
            apply_pending = true;
        } else if (im.state == FAILED) {
            set_status("ERROR: ", im.error);
            im = import_t();
        }
        return;
    }
}

static void write_sector(uint32_t lba, const uint8_t* buf) {
    last_write = get_time();
    if (lba < DATA_LBA) {
        return;
    }
    uint32_t cluster = 2 + (lba - DATA_LBA) / SECTORS_PER_CLUSTER;
    bool cluster_start = ((lba - DATA_LBA) % SECTORS_PER_CLUSTER) == 0;

    if (cluster == APPLY_CLUSTER) {
        for (const uint8_t* e = buf; e < buf + SECTOR; e += 32) {
            uint16_t first = e[26] | (e[27] << 8);
            uint32_t size;
            memcpy(&size, e + 28, 4);
            if ((e[0] == 0) || (e[0] == 0xE5) || (e[11] & 0x18) || (first < 2) || (size == 0)) {
                continue;
            }
            dropped[dropped_next] = { first, size };
            dropped_next = (dropped_next + 1) % (sizeof(dropped) / sizeof(dropped[0]));
        }
    } else {
        uint32_t magic[2];
        memcpy(magic, buf, 8);
        if (cluster_start && (magic[0] == 0x0A324655) && (magic[1] == 0x9E5D5157)) {
            uf2_cluster = cluster;
        }
        bool next = (im.state == PARSING) && (lba == im.next_lba);
        if (!next && cluster_start && !apply_pending && looks_like_json(buf)) {
            im = import_t();
            im.state = PARSING;
            im.cluster = cluster;
            im.header = persist_config_t();
            im.header.flags = FLAG_NORMALIZE_GAMEPAD_INPUTS;
            im.header.unmapped_passthrough_layer_mask = 0xFF;
            im.header.partial_scroll_timeout = 1000000;
            im.header.tap_hold_threshold = 200000;
            im.header.gpio_debounce_time_ms = 5;
            next = true;
        }
        if (next) {
            for (int i = 0; (i < SECTOR) && (im.state == PARSING); i++) {
                if (buf[i] < 0x80) {
                    json_char(buf[i]);
                }
            }
            im.next_lba = lba + 1;
            im.bytes += SECTOR;
            if (staged_size() > PERSISTED_CONFIG_SIZE) {
                fail("configuration is too big");
            }
        }
    }
    check_dropped();
}

static void apply() {
    std::vector<uint8_t> image(PERSISTED_CONFIG_SIZE);
    uint8_t* p = image.data();
    auto add = [&p](const void* data, size_t len) {
        memcpy(p, data, len);
        p += len;
    };

    im.header.version = VDISK_CONFIG_VERSION;
    add(&im.header, sizeof(im.header));
    add(im.mappings.data(), im.mappings.size());
    add(im.macros.data(), im.macros.size());
    p += NMACROS - ((im.n_macros < NMACROS) ? im.n_macros : NMACROS);
    add(im.expressions.data(), im.expressions.size());
    p += 2 * (NEXPRESSIONS - ((im.n_expressions < NEXPRESSIONS) ? im.n_expressions : NEXPRESSIONS));
    add(im.quirks.data(), im.quirks.size());
    uint32_t crc = crc32(image.data(), PERSISTED_CONFIG_SIZE - 4);
    memcpy(image.data() + PERSISTED_CONFIG_SIZE - 4, &crc, 4);
    im = import_t();

    uint8_t prev_interval_override = interval_override;
    config_mappings.clear();
    my_mutex_enter(MutexId::QUIRKS);
    quirks.clear();
    my_mutex_exit(MutexId::QUIRKS);
    load_config(image.data());
    if (interval_override != prev_interval_override) {
        interval_override_updated();
    }
    reset_state();
    config_updated = true;
    need_to_persist_config = true;
    set_status("OK: ", "configuration loaded and saved.");
}

void vdisk_task() {
    if (apply_pending) {
        apply_pending = false;
        apply();
    }
    if (config_updated || need_to_persist_config) {
        changed = true;
    }
    uint64_t now = get_time();
    if (now - last_write <= QUIET_US) {
        return;
    }
    if (bootloader_pending) {
        bootloader_pending = false;
        reset_to_bootloader();
    }
    if (!changed && (dropped[0].cluster != 0)) {
        set_status("ERROR: ", (im.state == FAILED) ? im.error : "file is not a configuration");
    }
    // make the host read the drive again
    if (changed) {
        changed = false;
        json_size = 0;
        im = import_t();
        memset(dropped, 0, sizeof(dropped));
        uf2_cluster = 0;
        eject_until = now + EJECT_US;
    }
}

const uint8_t* vdisk_configuration_descriptor(const uint8_t* base) {
    static uint8_t desc[128];
    uint16_t len = base[2] | (base[3] << 8);
    const uint8_t msc[] = { TUD_MSC_DESCRIPTOR(base[4], 0, VDISK_EP_OUT, VDISK_EP_IN, 64) };

    // emulated consoles keep their original descriptor
    if ((our_descriptor->vid != 0) || (len + sizeof(msc) > sizeof(desc))) {
        return base;
    }
    memcpy(desc, base, len);
    memcpy(desc + len, msc, sizeof(msc));
    len += sizeof(msc);
    desc[2] = len & 0xFF;
    desc[3] = len >> 8;
    desc[4]++;
    return desc;
}

static bool media_present() {
    return !host_ejected && (get_time() >= eject_until);
}

void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4]) {
    memcpy(vendor_id, "HIDRemap", 8);
    memcpy(product_id, "Config Drive    ", 16);
    memcpy(product_rev, "1.0 ", 4);
}

bool tud_msc_test_unit_ready_cb(uint8_t lun) {
    if (!media_present()) {
        tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3A, 0x00);
        return false;
    }
    return true;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t* block_count, uint16_t* block_size) {
    *block_count = TOTAL_SECTORS;
    *block_size = SECTOR;
}

bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject) {
    if (load_eject) {
        host_ejected = !start;
    }
    return true;
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
    if (!media_present() || (offset != 0)) {
        return -1;
    }
    for (uint32_t i = 0; i + SECTOR <= bufsize; i += SECTOR) {
        read_sector(lba + i / SECTOR, (uint8_t*) buffer + i);
    }
    return bufsize;
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize) {
    if (!media_present() || (offset != 0)) {
        return -1;
    }
    for (uint32_t i = 0; i + SECTOR <= bufsize; i += SECTOR) {
        write_sector(lba + i / SECTOR, buffer + i);
    }
    return bufsize;
}

int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void* buffer, uint16_t bufsize) {
    tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
    return -1;
}
