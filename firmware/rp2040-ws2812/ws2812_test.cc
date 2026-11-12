// Standalone WS2812 diagnostic firmware (ws2812_test.uf2).
//
// No USB, no remapper code: only the PIO driver. Cycles red, green, blue,
// white for one second each, forever. If this works on the board, the driver
// and the pin are good; any remaining problem is in the hid-remapper
// integration or the config.
//
// The pin and colour order come from WS2812_TEST_SETTINGS (same 0x00LOOOPP
// format as the settings register), set in ws2812.cmake.

#include <pico/stdlib.h>

#include "ws2812_led.h"

int main() {
    if (!ws2812_led_configure(WS2812_TEST_SETTINGS)) {
        // Invalid settings or no free PIO state machine.
        while (true) {
            tight_loop_contents();
        }
    }

    static const uint32_t colors[] = {
        0x20FF0000,  // red
        0x2000FF00,  // green
        0x200000FF,  // blue
        0x20FFFFFF,  // white
    };

    while (true) {
        for (unsigned i = 0; i < sizeof(colors) / sizeof(colors[0]); i++) {
            ws2812_led_show(colors[i]);
            sleep_ms(1000);
        }
    }
}
