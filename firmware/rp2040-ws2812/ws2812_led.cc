#include "ws2812_led.h"

#include "ws2812.pio.h"  // Generated from ws2812.pio by pico_generate_pio_header().

#include <hardware/clocks.h>
#include <hardware/pio.h>
#include <hardware/timer.h>

// PIO instance used for the WS2812 state machine. Defaults to pio0. The
// Pico-PIO-USB host uses pio0/sm0 (TX) and pio1/sm0+sm1 (RX/EOP), so there is
// always a free state machine on either block; override this if the chosen
// block runs out of instruction memory.
#ifndef WS2812_PIO
#define WS2812_PIO pio0
#endif

// WS2812 bit rate in Hz. The classic part runs at 800kHz.
#ifndef WS2812_FREQ
#define WS2812_FREQ 800000
#endif

// How long the activity blink stays lit, in microseconds (matches activity_led).
#ifndef WS2812_ACTIVITY_US
#define WS2812_ACTIVITY_US 50000
#endif

// Colour shown for the activity blink (dim green by default).
#ifndef WS2812_ACTIVITY_R
#define WS2812_ACTIVITY_R 0
#endif
#ifndef WS2812_ACTIVITY_G
#define WS2812_ACTIVITY_G 8
#endif
#ifndef WS2812_ACTIVITY_B
#define WS2812_ACTIVITY_B 0
#endif

static PIO pio = WS2812_PIO;
static int sm = -1;

// One 32-bit word per LED, already byte-ordered for the strip (GRB, MSB first).
static uint32_t framebuffer[WS2812_NUM_LEDS];

static bool activity_state = false;
static uint64_t turn_off_after = 0;

// Push one pixel. The PIO autopull threshold is 24 bits for RGB (so the colour
// must sit in the top 24 bits) and 32 bits for RGBW.
static inline void put_pixel(uint32_t grb) {
#if WS2812_IS_RGBW
    pio_sm_put_blocking(pio, sm, grb);
#else
    pio_sm_put_blocking(pio, sm, grb << 8u);
#endif
}

void ws2812_led_init() {
    uint offset = pio_add_program(pio, &ws2812_program);
    sm = pio_claim_unused_sm(pio, true);
    ws2812_program_init(pio, sm, offset, WS2812_PIN, WS2812_FREQ, WS2812_IS_RGBW);

    ws2812_led_set_all(0, 0, 0);
    ws2812_led_show();
}

void ws2812_led_set(uint32_t index, uint8_t r, uint8_t g, uint8_t b) {
    if (index >= WS2812_NUM_LEDS) {
        return;
    }
    // WS2812 expects green, then red, then blue.
    framebuffer[index] = ((uint32_t) g << 16) | ((uint32_t) r << 8) | (uint32_t) b;
}

void ws2812_led_set_all(uint8_t r, uint8_t g, uint8_t b) {
    for (uint32_t i = 0; i < WS2812_NUM_LEDS; i++) {
        ws2812_led_set(i, r, g, b);
    }
}

void ws2812_led_show() {
    if (sm < 0) {
        return;  // Not initialised yet.
    }
    for (uint32_t i = 0; i < WS2812_NUM_LEDS; i++) {
        put_pixel(framebuffer[i]);
    }
}

void ws2812_led_activity_on() {
    activity_state = true;
    ws2812_led_set_all(WS2812_ACTIVITY_R, WS2812_ACTIVITY_G, WS2812_ACTIVITY_B);
    ws2812_led_show();
    turn_off_after = time_us_64() + WS2812_ACTIVITY_US;
}

void ws2812_led_activity_off_maybe() {
    if (activity_state && (time_us_64() > turn_off_after)) {
        activity_state = false;
        ws2812_led_set_all(0, 0, 0);
        ws2812_led_show();
    }
}
