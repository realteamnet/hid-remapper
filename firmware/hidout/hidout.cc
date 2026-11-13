#include "hidout.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include <tusb.h>

#include "globals.h"
#include "platform.h"
#include "remapper.h"

#define RECHECK_US 250000
#define MAX_LINES 32
#define MAX_DATA 63

extern int32_t registers[];  // remapper.cc

static_assert((HIDOUT_REGISTER >= 1) && (HIDOUT_REGISTER <= 32));
static_assert((HIDOUT_TABLE >= 1) && (HIDOUT_TABLE <= NEXPRESSIONS));

struct line_t {
    uint8_t report_id;
    uint8_t len;  // bytes after the report ID
    uint8_t data[MAX_DATA];
    std::vector<uint32_t> targets;
};

static uint32_t prev_selected = 0;
static uint32_t prev_table_hash = 0;
static std::vector<uint16_t> sent_to[MAX_LINES];
static uint64_t next_check = 0;

// A line is <length in bytes, decimal> <report as 32-bit words, report ID first> <targets>, ended by eol.
// Lines without values (e.g. comment-only lines) are not counted.
static uint32_t parse_table(std::vector<line_t>& lines) {
    uint32_t hash = 2166136261u;
    std::vector<uint32_t> vals;
    my_mutex_enter(MutexId::EXPRESSIONS);
    auto const& expr = expressions[HIDOUT_TABLE - 1];
    for (size_t i = 0; i <= expr.size(); i++) {
        bool eol = (i == expr.size()) || (expr[i].op == Op::EOL);
        if (!eol) {
            hash = (hash ^ (uint8_t) expr[i].op ^ expr[i].val) * 16777619u;
            if ((expr[i].op == Op::PUSH) || (expr[i].op == Op::PUSH_USAGE)) {
                vals.push_back(((vals.empty()) && (expr[i].op == Op::PUSH)) ? expr[i].val / 1000 : expr[i].val);
            }
            continue;
        }
        if (vals.empty()) {
            continue;
        }
        line_t line = {};
        uint32_t len = vals[0];
        uint32_t words = (len + 3) / 4;
        if ((len >= 2) && (len <= MAX_DATA + 1) && (vals.size() >= 1 + words) && (lines.size() < MAX_LINES)) {
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
            lines.push_back(line);
        } else if (lines.size() < MAX_LINES) {
            lines.push_back(line);  // malformed: keeps its line number, never sent
        }
        vals.clear();
    }
    my_mutex_exit(MutexId::EXPRESSIONS);
    return hash;
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

static void send_line(const line_t& line, std::vector<uint16_t>& sent) {
    std::vector<uint16_t> present;
    my_mutex_enter(MutexId::THEIR_USAGES);
    for (auto const& [interface, reports] : their_out_usages) {
        if (!reports.count(line.report_id) || !target_ok(line.targets, interface >> 8)) {
            continue;
        }
        present.push_back(interface);
        if (std::find(sent.begin(), sent.end(), interface) != sent.end()) {
            continue;
        }
        auto size = out_report_sizes.find(((uint32_t) interface << 16) | line.report_id);
        uint8_t len = (size != out_report_sizes.end()) ? std::min<uint32_t>(size->second, MAX_DATA) : line.len;
        queue_out_report(interface, line.report_id, line.data, len);
    }
    my_mutex_exit(MutexId::THEIR_USAGES);
    sent = present;
}

void hidout_task() {
    uint32_t selected = registers[HIDOUT_REGISTER - 1];
    uint64_t now = get_time();
    if ((selected == prev_selected) && (now < next_check)) {
        return;
    }
    next_check = now + RECHECK_US;
    if (selected == 0) {
        for (auto& sent : sent_to) {
            sent.clear();
        }
        prev_selected = 0;
        return;
    }

    std::vector<line_t> lines;
    uint32_t hash = parse_table(lines);
    bool table_changed = hash != prev_table_hash;
    prev_table_hash = hash;

    for (uint32_t n = 0; n < MAX_LINES; n++) {
        uint32_t bit = 1u << n;
        if (!(selected & bit) || (n >= lines.size()) || (lines[n].report_id == 0)) {
            sent_to[n].clear();
            continue;
        }
        if (!(prev_selected & bit) || table_changed) {
            sent_to[n].clear();
        }
        send_line(lines[n], sent_to[n]);
    }
    prev_selected = selected;
}
