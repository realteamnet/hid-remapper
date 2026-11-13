#include "hidout.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include <tusb.h>

#include "globals.h"
#include "platform.h"
#include "remapper.h"

#define RECHECK_US 250000
#define MAX_DATA 7

extern int32_t registers[];  // remapper.cc

static_assert((HIDOUT_REGISTER >= 1) && (HIDOUT_TARGETS >= 0) && (HIDOUT_REGISTER + 1 + HIDOUT_TARGETS <= 32));

static uint32_t sent_regs[2 + HIDOUT_TARGETS];
static std::vector<uint16_t> sent_to;
static uint64_t next_check = 0;

static bool target_ok(const uint32_t* targets, uint8_t dev_addr) {
    uint16_t vid = 0;
    uint16_t pid = 0;
    bool known = tuh_vid_pid_get(dev_addr, &vid, &pid);
    bool any = false;
    for (int i = 0; i < HIDOUT_TARGETS; i++) {
        uint32_t t = targets[i];
        if (t == 0) {
            continue;
        }
        any = true;
        if (known && ((t <= 0xFFFF) ? (vid == t) : ((((uint32_t) vid << 16) | pid) == t))) {
            return true;
        }
    }
    return !any;
}

void hidout_task() {
    const uint32_t* regs = (const uint32_t*) &registers[HIDOUT_REGISTER - 1];
    uint8_t report_id = regs[0] >> 24;
    if (report_id == 0) {
        sent_to.clear();
        memset(sent_regs, 0, sizeof(sent_regs));
        return;
    }

    uint64_t now = get_time();
    bool changed = memcmp(regs, sent_regs, sizeof(sent_regs)) != 0;
    if (!changed && (now < next_check)) {
        return;
    }
    next_check = now + RECHECK_US;
    if (changed) {
        memcpy(sent_regs, regs, sizeof(sent_regs));
        sent_to.clear();
    }

    const uint8_t data[MAX_DATA] = {
        (uint8_t) (regs[0] >> 16),
        (uint8_t) (regs[0] >> 8),
        (uint8_t) regs[0],
        (uint8_t) (regs[1] >> 24),
        (uint8_t) (regs[1] >> 16),
        (uint8_t) (regs[1] >> 8),
        (uint8_t) regs[1],
    };

    std::vector<uint16_t> present;
    my_mutex_enter(MutexId::THEIR_USAGES);
    for (auto const& [interface, reports] : their_out_usages) {
        if (!reports.count(report_id) || !target_ok(regs + 2, interface >> 8)) {
            continue;
        }
        present.push_back(interface);
        if (std::find(sent_to.begin(), sent_to.end(), interface) != sent_to.end()) {
            continue;
        }
        auto size = out_report_sizes.find(((uint32_t) interface << 16) | report_id);
        uint8_t len = (size != out_report_sizes.end()) ? std::min<uint8_t>(size->second, MAX_DATA) : MAX_DATA;
        queue_out_report(interface, report_id, data, len);
    }
    my_mutex_exit(MutexId::THEIR_USAGES);
    sent_to = present;
}
