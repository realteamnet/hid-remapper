#ifndef _WS2812_LED_H_
#define _WS2812_LED_H_

#include <stdbool.h>
#include <stdint.h>

// --- Compile-time configuration (override with target_compile_definitions) ---

// GPIO driving the WS2812 data line (GPIO16 on the Waveshare RP2040-Zero).
#ifndef WS2812_PIN
#define WS2812_PIN 16
#endif

// Number of LEDs in the chain.
#ifndef WS2812_NUM_LEDS
#define WS2812_NUM_LEDS 1
#endif

// 0 = GRB byte order (standard WS2812), 1 = RGB order (some clones/batches).
// If red and green appear swapped, flip this.
#ifndef WS2812_RGB_ORDER
#define WS2812_RGB_ORDER 0
#endif

// Global brightness scale, 0..255, applied to every colour.
#ifndef WS2812_BRIGHTNESS
#define WS2812_BRIGHTNESS 32
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Claim a free PIO state machine, load the program and start it. Returns false
// if no PIO/state machine/instruction space was available (LED then stays
// unused, the rest of the firmware is unaffected). Call after the clock is set
// and after other PIO users (Pico-PIO-USB) have been initialised.
// Starts the power-on R -> G -> B sweep, which ws2812_led_task() plays out.
bool ws2812_led_init();

// Set every LED to one colour (0..255 per channel, scaled by
// WS2812_BRIGHTNESS) and push it to the strip immediately.
void ws2812_led_put_rgb(uint8_t r, uint8_t g, uint8_t b);

// Non-blocking housekeeping: plays the power-on sweep and turns the activity
// blink off. Call once per main-loop iteration.
void ws2812_led_task();

// Flash green briefly (mirrors activity_led_on()).
void ws2812_led_activity_on();

#ifdef __cplusplus
}
#endif

#endif
