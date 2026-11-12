// Standalone WS2812 proof-of-concept firmware (ws2812_test.uf2).
//
// No USB, no remapper code: only the PIO driver. Cycles red, green, blue,
// white for one second each, forever. If this works on the board, the driver
// and the pin are good; any remaining problem is in the hid-remapper
// integration.
//
// C equivalent of the MicroPython sample's main loop:
//   for color in (RED, GREEN, BLUE, WHITE):
//       pixels_fill(color); pixels_show(); time.sleep(1)

#include <pico/stdlib.h>

#include "ws2812_led.h"

int main() {
    if (!ws2812_led_init()) {
        // No free PIO state machine/instruction space (should not happen here).
        while (true) {
            tight_loop_contents();
        }
    }

    static const uint8_t colors[][3] = {
        { 255, 0, 0 },      // red
        { 0, 255, 0 },      // green
        { 0, 0, 255 },      // blue
        { 255, 255, 255 },  // white
    };

    while (true) {
        for (unsigned i = 0; i < sizeof(colors) / sizeof(colors[0]); i++) {
            ws2812_led_put_rgb(colors[i][0], colors[i][1], colors[i][2]);
            sleep_ms(1000);
        }
    }
}
