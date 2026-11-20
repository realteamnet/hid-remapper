#include "ws2812_cmd.h"

#include "cmd_table.h"

extern int32_t registers[];  // remapper.cc

struct animation_t {
    uint8_t reg;
    uint8_t n;
    uint32_t color[WS2812_CMD_MAX_STEPS];
    int32_t ms[WS2812_CMD_MAX_STEPS];  // negative = fade to the next step
    uint32_t total_ms;
    bool playing;
    uint64_t start_us;
};

static animation_t animations[WS2812_CMD_MAX_ANIMATIONS];
static uint8_t n_animations = 0;
static bool have_settings = false;
static uint32_t settings_value = 0;

static void table_begin(void*) {
    n_animations = 0;
    have_settings = false;
}

static bool table_line(const cmd_value_t* vals, size_t n, void*) {
    if (!vals[0].hex) {
        return false;
    }
    uint32_t tag = (uint32_t) vals[0].v >> 8;
    uint8_t param = (uint32_t) vals[0].v & 0xFF;
    if (tag == WS2812_CMD_SETTINGS_TAG) {
        if (n >= 2) {
            have_settings = true;
            settings_value = vals[1].v;
        }
        return true;
    }
    if (tag != WS2812_CMD_ANIMATION_TAG) {
        return false;
    }
    if ((param < 1) || (param > 32) || (n < 3) || (n_animations >= WS2812_CMD_MAX_ANIMATIONS)) {
        return true;  // ours, but malformed or no room: ignored
    }
    animation_t& a = animations[n_animations];
    a = animation_t();
    a.reg = param;
    for (size_t i = 1; (i + 1 < n) && (a.n < WS2812_CMD_MAX_STEPS); i += 2) {
        int32_t ms = vals[i + 1].v;  // decimal seconds x1000 = milliseconds; a hex time is taken as ms
        if (ms == 0) {
            continue;
        }
        a.color[a.n] = vals[i].v;
        a.ms[a.n] = ms;
        a.total_ms += (ms < 0) ? -ms : ms;
        a.n++;
    }
    if (a.n > 0) {
        n_animations++;
    }
    return true;
}

void ws2812_cmd_init() {
    cmd_table_add_handler(table_begin, table_line, nullptr);
}

bool ws2812_cmd_settings(uint32_t* settings) {
    if (have_settings) {
        *settings = settings_value;
    }
    return have_settings;
}

static uint32_t blend(uint32_t a, uint32_t b, uint32_t pos, uint32_t len) {
    uint32_t out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        int32_t ca = (a >> shift) & 0xFF;
        int32_t cb = (b >> shift) & 0xFF;
        out |= (uint32_t) (ca + (cb - ca) * (int32_t) pos / (int32_t) len) << shift;
    }
    return out;
}

bool ws2812_cmd_color(uint64_t now_us, uint32_t* color) {
    bool found = false;
    for (uint8_t i = 0; i < n_animations; i++) {
        animation_t& a = animations[i];
        bool on = registers[a.reg - 1] != 0;
        if (on && !a.playing) {
            a.start_us = now_us;
        }
        a.playing = on;
        if (!on) {
            continue;
        }
        uint32_t t = ((now_us - a.start_us) / 1000) % a.total_ms;
        for (uint8_t s = 0; s < a.n; s++) {
            uint32_t len = (a.ms[s] < 0) ? -a.ms[s] : a.ms[s];
            if (t < len) {
                *color = (a.ms[s] < 0) ? blend(a.color[s], a.color[(s + 1) % a.n], t, len) : a.color[s];
                break;
            }
            t -= len;
        }
        found = true;  // later active lines overwrite earlier ones
    }
    return found;
}
