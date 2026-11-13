#ifndef _WS2812_LED_H_
#define _WS2812_LED_H_

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

void ws2812_led_task();

#endif
