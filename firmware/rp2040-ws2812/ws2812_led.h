#ifndef _WS2812_LED_H_
#define _WS2812_LED_H_

#include <stdint.h>

// --- Compile-time configuration ---------------------------------------------
// Override any of these from the build system (target_compile_definitions) or
// a board header so the module stays self-contained as a submodule.

// Number of WS2812 LEDs in the chain.
#ifndef WS2812_NUM_LEDS
#define WS2812_NUM_LEDS 1
#endif

// GPIO pin driving the WS2812 data line. NOTE: this pin must be excluded from
// hid-remapper's GPIO management (see README.md), otherwise it will be claimed
// as a remappable GPIO and the LED data line will glitch.
#ifndef WS2812_PIN
#define WS2812_PIN 16
#endif

// Set to 1 for RGBW (SK6812) strips, 0 for plain RGB WS2812.
#ifndef WS2812_IS_RGBW
#define WS2812_IS_RGBW 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Initialise the PIO state machine. Call once, after board_init().
void ws2812_led_init();

// Set a single LED in the framebuffer (channels 0..255). The strip is not
// updated until ws2812_led_show() is called.
void ws2812_led_set(uint32_t index, uint8_t r, uint8_t g, uint8_t b);

// Set every LED in the framebuffer to one colour. Does not update the strip.
void ws2812_led_set_all(uint8_t r, uint8_t g, uint8_t b);

// Sentinel for ws2812_led_show(): keep the LED lit indefinitely.
#define WS2812_INFINITE (-1)

// Push the framebuffer to the strip. ALWAYS non-blocking: it lights the LED
// immediately and returns; timing is advanced by ws2812_led_task().
//   lit_ms   - how long to keep the LED lit, in milliseconds. WS2812_INFINITE
//              (default) leaves it lit until the next show()/off.
//   flash_ms - if > 0, blink the LED on/off every flash_ms (until lit_ms
//              elapses, or forever if lit_ms is WS2812_INFINITE); 0 (default)
//              = no blink.
// Requires ws2812_led_task() to be called regularly from the main loop.
#ifdef __cplusplus
void ws2812_led_show(int32_t lit_ms = WS2812_INFINITE, uint32_t flash_ms = 0);
#else
void ws2812_led_show(int32_t lit_ms, uint32_t flash_ms);
#endif

// Advance the non-blocking show/flash timing. Call once per main-loop iteration
// (ws2812_led_activity_off_maybe() already does this, so wiring that in the loop
// is sufficient).
void ws2812_led_task();

// Optional drop-in replacement for the stock activity_led: blink the strip
// when HID activity is seen. Mirror the existing activity_led call sites:
//   - ws2812_led_activity_on()        when a report/GPIO change is processed
//   - ws2812_led_activity_off_maybe() once per main-loop iteration
void ws2812_led_activity_on();
void ws2812_led_activity_off_maybe();

#ifdef __cplusplus
}
#endif

#endif
