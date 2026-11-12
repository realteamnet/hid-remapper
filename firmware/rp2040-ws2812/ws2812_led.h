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

// Default brightness scale, 0..255, used when a colour value's brightness
// byte is 0.
#ifndef WS2812_BRIGHTNESS
#define WS2812_BRIGHTNESS 32
#endif

// hid-remapper expression register (1..32) whose value sets the LED as
// 0xBBRRGGBB (BB = brightness, 00 = WS2812_BRIGHTNESS), e.g.
// `0x20FF0000 32 store`. The firmware has no colours of its own: the LED is
// off until the config writes this register. 0 disables. Only used when
// WS2812_ENABLED.
#ifndef WS2812_COLOR_REGISTER
#define WS2812_COLOR_REGISTER 32
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Claim a free PIO state machine, load the program, start it and turn the LED
// off. Returns false if no PIO/state machine/instruction space was available
// (the LED then stays unused, the rest of the firmware is unaffected). Call
// after the clock is set and after other PIO users (Pico-PIO-USB) have been
// initialised.
bool ws2812_led_init();

// Set every LED to one colour (0..255 per channel, scaled by
// WS2812_BRIGHTNESS) and push it to the strip immediately.
void ws2812_led_put_rgb(uint8_t r, uint8_t g, uint8_t b);

// Show the colour from WS2812_COLOR_REGISTER (pushed only when it changes).
// Call once per main-loop iteration.
void ws2812_led_task();

#ifdef __cplusplus
}
#endif

#endif
