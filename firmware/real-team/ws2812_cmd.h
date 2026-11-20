#ifndef _WS2812_CMD_H_
#define _WS2812_CMD_H_

#include <stdint.h>

// LED command lines (command table, see README.md "RGB LED commands"):
//   0x4C455300 <0xWWLOOOPP>                      settings
//   0x4C4541nn <0xIIRRGGBB> <seconds> ...         animation, plays while register nn != 0
#define WS2812_CMD_SETTINGS_TAG 0x4C4553
#define WS2812_CMD_ANIMATION_TAG 0x4C4541

#ifndef WS2812_CMD_MAX_ANIMATIONS
#define WS2812_CMD_MAX_ANIMATIONS 16
#endif
#ifndef WS2812_CMD_MAX_STEPS
#define WS2812_CMD_MAX_STEPS 32
#endif

// registers the handler with the command table
void ws2812_cmd_init();
// true and *settings set if the config has a settings line
bool ws2812_cmd_settings(uint32_t* settings);
// true and *color set if an animation is playing (the latest active line wins)
bool ws2812_cmd_color(uint64_t now_us, uint32_t* color);

#endif
