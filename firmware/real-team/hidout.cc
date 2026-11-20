#include "hidout.h"

#include "cmd_table.h"
#include "cmd_table_remapper.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include <tusb.h>

#include "globals.h"
#include "platform.h"
#include "remapper.h"

#define RECHECK_US 250000
#define REPLY_WINDOW_US 2000000
#define MAX_REPLIES 8
#define MAX_LINES 32
#define MAX_DATA 63
#define MAX_PATTERN 16
#define MONITOR_SENT 0xFFFE0000
#define MONITOR_HAVE_REPORT 0xFFFE0100
#define MONITOR_MATCHED 0xFFFE0200

extern int32_t registers[];  // remapper.cc

// DEBUG.TXT on the config drive, on vdisk debug builds
void vdisk_log(const char* fmt, ...) __attribute__((weak, format(printf, 1, 2)));
#define LOG(...)                    \
    do {                            \
        if (vdisk_log) {            \
            vdisk_log(__VA_ARGS__); \
        }                           \
    } while (0)

static_assert((HIDOUT_REGISTER >= 1) && (HIDOUT_REGISTER <= 32));

struct line_t {
    uint8_t report_id;
    uint8_t len;  // bytes after the report ID
    uint8_t data[MAX_DATA];
    std::vector<uint32_t> targets;
};

struct watch_t {
    uint8_t len;
    uint8_t bytes[MAX_PATTERN];
    std::vector<uint32_t> targets;
};

static std::vector<line_t> lines;
static std::vector<watch_t> watches;
static bool table_changed = false;
static uint32_t prev_selected = 0;
static std::vector<uint16_t> sent_to[MAX_LINES];
static uint64_t next_check = 0;
static uint32_t logged[MAX_LINES];
static uint32_t logged_devices = 0;
static uint32_t logged_selected = 0;
static uint16_t reply_interface = 0xFFFF;
static uint64_t reply_until = 0;
static uint8_t replies_left = 0;
static uint32_t last_reply = 0;

static uint32_t mix(uint32_t h, uint32_t v) {
    return (h ^ v) * 16777619u;
}

// writes the connected interfaces and their output report IDs to the log when they change
static void log_devices() {
    uint32_t h = 2166136261u;
    my_mutex_enter(MutexId::THEIR_USAGES);
    for (auto const& [interface, reports] : their_out_usages) {
        h = mix(h, interface);
        for (auto const& [report_id, usages] : reports) {
            h = mix(h, report_id);
        }
    }
    if (h != logged_devices) {
        logged_devices = h;
        LOG("hidout: %u interfaces with output reports", (unsigned) their_out_usages.size());
        for (auto const& [interface, reports] : their_out_usages) {
            uint16_t vid = 0;
            uint16_t pid = 0;
            tuh_vid_pid_get(interface >> 8, &vid, &pid);
            char ids[100] = "";
            for (auto const& [report_id, usages] : reports) {
                size_t n = strlen(ids);
                snprintf(ids + n, sizeof(ids) - n, " %02x", report_id);
            }
            LOG("hidout:   interface %04x %04X:%04X output reports:%s", interface, vid, pid, ids);
        }
    }
    my_mutex_exit(MutexId::THEIR_USAGES);
}

// Send line: <length in bytes> <report as 32-bit words, report ID first> <targets>, numbered from 0.
// Watch line: 0 <pattern length> <pattern words> <targets>, not numbered: an input report from a
// target that starts with the pattern makes the selected send lines go out again.
static void add_watch(std::vector<watch_t>& found, const std::vector<uint32_t>& vals) {
    if (vals.size() < 2) {
        return;
    }
    watch_t w = {};
    uint32_t len = vals[1];
    uint32_t words = (len + 3) / 4;
    if ((len < 1) || (len > MAX_PATTERN) || (vals.size() < 2 + words)) {
        return;
    }
    for (uint32_t b = 0; b < len; b++) {
        w.bytes[b] = vals[2 + b / 4] >> (24 - 8 * (b % 4));
    }
    w.len = len;
    w.targets.assign(vals.begin() + 2 + words, vals.end());
    found.push_back(w);
}

static void add_line(std::vector<line_t>& lines, const std::vector<uint32_t>& vals) {
    if (lines.size() >= MAX_LINES) {
        return;
    }
    line_t line = {};
    uint32_t len = vals[0];
    uint32_t words = (len + 3) / 4;
    if ((len >= 2) && (len <= MAX_DATA + 1) && (vals.size() >= 1 + words)) {
        for (uint32_t b = 0; b < len; b++) {
            uint8_t byte = vals[1 + b / 4] >> (24 - 8 * (b % 4));
            if (b == 0) {
                line.report_id = byte;
            } else {
                line.data[b - 1] = byte;
            }
        }
        line.len = len - 1;
        line.targets.assign(vals.begin() + 1 + words, vals.end());
    }
    lines.push_back(line);  // a malformed line keeps its number but has report ID 0 and is never sent
}

// command-table handler: hidout owns the lines whose first value is a decimal
static void table_begin(void*) {
    lines.clear();
    watches.clear();
    table_changed = true;
}

static bool table_line(const cmd_value_t* vals, size_t n, void*) {
    if (vals[0].hex) {
        return false;
    }
    std::vector<uint32_t> v;
    for (size_t i = 0; i < n; i++) {
        bool count = (i == 0) || ((i == 1) && (vals[0].v == 0));
        v.push_back((count && !vals[i].hex) ? vals[i].v / 1000 : (uint32_t) vals[i].v);
    }
    if (v[0] == 0) {
        add_watch(watches, v);
    } else {
        add_line(lines, v);
    }
    return true;
}

void hidout_init() {
    cmd_table_add_handler(table_begin, table_line, nullptr);
}

static bool target_ok(const std::vector<uint32_t>& targets, uint8_t dev_addr) {
    if (targets.empty()) {
        return true;
    }
    uint16_t vid = 0;
    uint16_t pid = 0;
    if (!tuh_vid_pid_get(dev_addr, &vid, &pid)) {
        return false;
    }
    for (uint32_t t : targets) {
        if ((t <= 0xFFFF) ? (vid == t) : ((((uint32_t) vid << 16) | pid) == t)) {
            return true;
        }
    }
    return false;
}

// Shown in the web tool's Monitor tab: 0xFFFE00nn = line n sent (value: interface),
// 0xFFFE01nn / 0xFFFE02nn = interfaces with its report ID / also matching its targets.
static void send_line(uint32_t n, const line_t& line, std::vector<uint16_t>& sent, bool report) {
    std::vector<uint16_t> present;
    std::vector<uint16_t> queued;
    int32_t have_report = 0;
    my_mutex_enter(MutexId::THEIR_USAGES);
    for (auto const& [interface, reports] : their_out_usages) {
        if (!reports.count(line.report_id)) {
            continue;
        }
        have_report++;
        if (!target_ok(line.targets, interface >> 8)) {
            continue;
        }
        present.push_back(interface);
        if (std::find(sent.begin(), sent.end(), interface) != sent.end()) {
            continue;
        }
        auto size = out_report_sizes.find(((uint32_t) interface << 16) | line.report_id);
        uint8_t len = (size != out_report_sizes.end()) ? std::min<uint32_t>(size->second, MAX_DATA) : line.len;
        queue_out_report(interface, line.report_id, line.data, len);
        queued.push_back(interface);
        reply_interface = interface;
        reply_until = get_time() + REPLY_WINDOW_US;
        replies_left = MAX_REPLIES;
        if (monitor_enabled) {
            monitor_usage(MONITOR_SENT | n, interface, 0);
        }
    }
    my_mutex_exit(MutexId::THEIR_USAGES);
    if (report && monitor_enabled) {
        monitor_usage(MONITOR_HAVE_REPORT | n, have_report, 0);
        monitor_usage(MONITOR_MATCHED | n, present.size(), 0);
    }
    if (report) {
        uint32_t h = mix(mix(2166136261u, have_report), present.size());
        for (uint16_t interface : queued) {
            h = mix(h, interface);
        }
        if (h != logged[n]) {
            logged[n] = h;
            char bytes[3 * MAX_DATA + 1] = "";
            for (uint8_t i = 0; (i < line.len) && (i < 16); i++) {
                snprintf(bytes + 3 * i, 4, " %02x", line.data[i]);
            }
            LOG("hidout: line %u (report %02x%s%s): %d interfaces have the report, %u match the targets", (unsigned) n, line.report_id,
                bytes, (line.len > 16) ? " ..." : "", (int) have_report, (unsigned) present.size());
            for (uint16_t interface : queued) {
                LOG("hidout: line %u sent to interface %04x", (unsigned) n, interface);
            }
        }
    }
    sent = present;
}

void hidout_task() {
    uint32_t selected = registers[HIDOUT_REGISTER - 1];
    uint64_t now = get_time();
    if ((selected == prev_selected) && (now < next_check)) {
        return;
    }
    next_check = now + RECHECK_US;
    if (selected != prev_selected) {
        cmd_table_remapper_read(true);
    }
    if (selected == 0) {
        for (auto& sent : sent_to) {
            sent.clear();
        }
        prev_selected = 0;
        return;
    }

    bool changed = table_changed;
    table_changed = false;
    if (changed) {
        LOG("hidout: table has %u send lines, %u watch lines", (unsigned) lines.size(), (unsigned) watches.size());
        for (uint32_t n = 0; n < lines.size(); n++) {
            if (lines[n].report_id == 0) {
                LOG("hidout: line %u is malformed and is never sent", (unsigned) n);
            }
        }
    }
    if (selected != logged_selected) {
        logged_selected = selected;
        LOG("hidout: selector (register %d) = %08lx", HIDOUT_REGISTER, (unsigned long) selected);
    }
    log_devices();

    for (uint32_t n = 0; n < MAX_LINES; n++) {
        uint32_t bit = 1u << n;
        if (!(selected & bit) || (n >= lines.size()) || (lines[n].report_id == 0)) {
            sent_to[n].clear();
            continue;
        }
        bool fresh = !(prev_selected & bit) || changed;
        if (fresh) {
            sent_to[n].clear();
        }
        send_line(n, lines[n], sent_to[n], fresh);
    }
    prev_selected = selected;
}

void hidout_report_received(uint8_t dev_addr, uint8_t instance, const uint8_t* report, uint16_t len) {
    for (auto const& w : watches) {
        if ((len >= w.len) && (memcmp(report, w.bytes, w.len) == 0) && target_ok(w.targets, dev_addr)) {
            for (auto& sent : sent_to) {
                sent.clear();
            }
            next_check = 0;
            LOG("hidout: watch line matched on interface %04x, sending the selected lines again", (dev_addr << 8) | instance);
            break;
        }
    }
    if ((replies_left == 0) || (((dev_addr << 8) | instance) != reply_interface) || (get_time() > reply_until)) {
        return;
    }
    replies_left--;
    uint32_t h = mix(mix(2166136261u, reply_interface), len);
    for (uint16_t i = 0; i < len; i++) {
        h = mix(h, report[i]);
    }
    if (h == last_reply) {
        return;  // same answer as last time: logged once
    }
    last_reply = h;
    char bytes[3 * 20 + 1] = "";
    for (uint16_t i = 0; (i < len) && (i < 20); i++) {
        snprintf(bytes + 3 * i, 4, " %02x", report[i]);
    }
    LOG("hidout: reply from interface %04x (%u bytes):%s", reply_interface, len, bytes);
}
