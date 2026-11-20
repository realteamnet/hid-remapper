#ifndef _WS2812_H_
#define _WS2812_H_

#include <pico.h>

// expression register holding 0xWWLOOOPP (see README.md), 0 = LED not used
#ifndef WS2812_SETTINGS_REGISTER
#define WS2812_SETTINGS_REGISTER 31
#endif

// expression register holding 0xIIRRGGBB
#ifndef WS2812_COLOR_REGISTER
#define WS2812_COLOR_REGISTER 32
#endif

#ifndef WS2812_DEFAULT_INTENSITY
#define WS2812_DEFAULT_INTENSITY 32
#endif

#ifndef WS2812_NUM_LEDS
#define WS2812_NUM_LEDS 1
#endif

// used from power-on until the config writes the settings or colour register (0 = LED off until then);
// by default the board's own LED (PP = FF) where the board header names one
#ifndef WS2812_BOOT_SETTINGS
#ifdef PICO_DEFAULT_WS2812_PIN
#define WS2812_BOOT_SETTINGS 0x000000FF
#else
#define WS2812_BOOT_SETTINGS 0
#endif
#endif

#ifndef WS2812_BOOT_COLOR
#define WS2812_BOOT_COLOR 0x200000FF
#endif

void ws2812_led_task();

#endif
