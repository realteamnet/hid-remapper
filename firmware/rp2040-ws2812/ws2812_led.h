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

// Default colour order: three hex nibbles naming the channel sent first,
// second and third (1 = R, 2 = G, 3 = B). 0x213 = GRB (standard WS2812),
// 0x123 = RGB (this RP2040-Zero clone). Can be changed at runtime through
// WS2812_SETTINGS_REGISTER.
#ifndef WS2812_COLOR_ORDER
#define WS2812_COLOR_ORDER 0x213
#endif

// Default intensity, 0..255, used when a colour value's intensity (II) byte
// is 0.
#ifndef WS2812_DEFAULT_INTENSITY
#define WS2812_DEFAULT_INTENSITY 32
#endif

// hid-remapper expression register (1..32) whose value sets the LED as
// (set from CMake: WS2812_COLOR_REGISTER, default 32)
// 0xIIRRGGBB (II = intensity, 00 = WS2812_DEFAULT_INTENSITY), e.g.
// `0x20FF0000 32 store`. The firmware has no colours of its own: the LED is
// off until the config writes this register. 0 disables. Only used when
// WS2812_ENABLED.
#ifndef WS2812_COLOR_REGISTER
#define WS2812_COLOR_REGISTER 32
#endif

// hid-remapper expression register (1..32) with LED settings, 0x00LOOOPP
// (set from CMake: WS2812_SETTINGS_REGISTER, default 31):
// PP = data GPIO (hex), OOO = colour order as in WS2812_COLOR_ORDER, L =
// master brightness 1 (dimmest) .. F (full), 0 = full. e.g.
// `0x00812310 31 store` = level 8, RGB, GPIO16. 0 (the power-on value) =
// build defaults. Invalid pin/order fields are ignored; the PIO-USB host pins
// are refused.
// Set to 0 to disable. Only used when WS2812_ENABLED.
#ifndef WS2812_SETTINGS_REGISTER
#define WS2812_SETTINGS_REGISTER 31
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
// WS2812_DEFAULT_INTENSITY) and push it to the strip immediately.
void ws2812_led_put_rgb(uint8_t r, uint8_t g, uint8_t b);

// Show the colour from WS2812_COLOR_REGISTER (pushed only when it changes).
// Call once per main-loop iteration.
void ws2812_led_task();

#ifdef __cplusplus
}
#endif

#endif
