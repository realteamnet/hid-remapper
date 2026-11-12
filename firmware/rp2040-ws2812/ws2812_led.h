#ifndef _WS2812_LED_H_
#define _WS2812_LED_H_

#include <stdbool.h>
#include <stdint.h>

// WS2812 RGB LED driven entirely by the hid-remapper config. Nothing here is
// board specific and nothing runs until the config asks for it: while the
// settings register is 0 no PIO state machine is claimed and no pin is
// touched.

// --- Compile-time configuration (set from ws2812.cmake) ----------------------

// hid-remapper expression register (1..32) holding the LED settings,
// 0x00LOOOPP:
//   PP  = data GPIO, hex (0x10 = GPIO16)
//   OOO = colour order, the channel sent 1st/2nd/3rd (1 = R, 2 = G, 3 = B):
//         213 = GRB (standard WS2812), 123 = RGB; 000 = WS2812_COLOR_ORDER
//   L   = master brightness, 1 (dimmest) .. F (full); 0 = full
// e.g. `0x00012310 31 store` = GPIO16, RGB, full brightness.
// 0 (the power-on value) or an invalid value (bad order, pin out of range or
// one of the PIO-USB host pins) = LED unused.
#ifndef WS2812_SETTINGS_REGISTER
#define WS2812_SETTINGS_REGISTER 31
#endif

// hid-remapper expression register (1..32) holding the colour, 0xIIRRGGBB
// (II = intensity, 00 = WS2812_DEFAULT_INTENSITY), e.g. `0x20FF0000 32 store`.
#ifndef WS2812_COLOR_REGISTER
#define WS2812_COLOR_REGISTER 32
#endif

// Colour order used when the settings leave OOO as 000.
#ifndef WS2812_COLOR_ORDER
#define WS2812_COLOR_ORDER 0x213
#endif

// Intensity, 0..255, used when a colour's II byte is 00.
#ifndef WS2812_DEFAULT_INTENSITY
#define WS2812_DEFAULT_INTENSITY 32
#endif

// Number of LEDs in the chain (all show the same colour).
#ifndef WS2812_NUM_LEDS
#define WS2812_NUM_LEDS 1
#endif

// --- API ---------------------------------------------------------------------

// Apply a settings value (format above). Claims a PIO state machine and the
// pin when the settings become valid, moves when the pin changes and releases
// everything (pin restored to its previous function) when they become 0 or
// invalid. Returns true if the LED is active afterwards.
bool ws2812_led_configure(uint32_t settings);

// Show 0xIIRRGGBB on the LED. Does nothing while the LED is not active.
void ws2812_led_show(uint32_t iirrggbb);

#ifdef WS2812_ENABLED
// Follow the two expression registers: applies the settings register when it
// changes and shows the colour register when it changes. Call once per
// main-loop iteration. While the settings register is 0 this only reads that
// register and returns.
void ws2812_led_task();
#endif

#endif
