#include "real_team.h"

#ifdef REAL_TEAM_CMD_TABLE
#include "cmd_table_remapper.h"
#endif
#ifdef WS2812_ENABLED
#include "ws2812.h"
#endif
#ifdef WS2812_COMMANDS_ENABLED
#include "ws2812_cmd.h"
#endif
#ifdef VDISK_ENABLED
#include "vdisk.h"
#endif
#ifdef HIDOUT_ENABLED
#include "hidout.h"
#endif

void real_team_task() {
    static bool initialized = false;
    if (!initialized) {
        initialized = true;
#ifdef HIDOUT_ENABLED
        hidout_init();
#endif
#ifdef WS2812_COMMANDS_ENABLED
        ws2812_cmd_init();
#endif
    }
#ifdef REAL_TEAM_CMD_TABLE
    cmd_table_remapper_read(false);
#endif
#ifdef VDISK_ENABLED
    vdisk_task();
#endif
#ifdef WS2812_ENABLED
    ws2812_led_task();
#endif
#ifdef HIDOUT_ENABLED
    hidout_task();
#endif
}

void real_team_report_received(uint8_t dev_addr, uint8_t instance, const uint8_t* report, uint16_t len) {
#ifdef HIDOUT_ENABLED
    hidout_report_received(dev_addr, instance, report, len);
#endif
}

const uint8_t* real_team_configuration_descriptor(const uint8_t* base) {
#ifdef VDISK_ENABLED
    return vdisk_configuration_descriptor(base);
#else
    return base;
#endif
}
