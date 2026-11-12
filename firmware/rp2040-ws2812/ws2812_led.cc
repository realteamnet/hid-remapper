#include "ws2812_led.h"

#include "ws2812.pio.h"  // Generated from ws2812.pio by pico_generate_pio_header().

#include <hardware/pio.h>
#include <hardware/timer.h>

#define WS2812_FREQ 800000

// Power-on sweep: each colour is shown for this long.
#define SWEEP_STEP_US 300000

// Activity blink duration (same as activity_led.cc).
#define ACTIVITY_US 50000

static PIO pio = NULL;
static uint sm = 0;
static uint offset = 0;
static bool initialized = false;

// Power-on sweep: red, green, blue, then off.
static const uint8_t sweep_colors[][3] = {
    { 255, 0, 0 },
    { 0, 255, 0 },
    { 0, 0, 255 },
    { 0, 0, 0 },
};
#define SWEEP_STEPS (sizeof(sweep_colors) / sizeof(sweep_colors[0]))

static uint32_t sweep_step = SWEEP_STEPS;  // == SWEEP_STEPS means finished
static uint64_t sweep_next = 0;

static bool activity_lit = false;
static uint64_t activity_off_at = 0;

// Last value pushed to the LED (0xBBRRGGBB), so the FIFO is only written when
// it changes. shown_valid is false until the first push after the sweep.
static uint32_t shown = 0;
static bool shown_valid = false;

#if defined(WS2812_ENABLED) && (WS2812_COLOR_REGISTER > 0)
// hid-remapper's expression registers (remapper.cc). Expressions set the LED
// with e.g. `0x20FF0000 32 store` (brightness 0x20, red).
extern int32_t registers[];

static inline uint32_t color_from_register() {
    return (uint32_t) registers[WS2812_COLOR_REGISTER - 1];
}
#else
static inline uint32_t color_from_register() {
    return 0;
}
#endif

static inline uint8_t scale(uint8_t v, uint8_t brightness) {
    return (uint8_t) (((uint16_t) v * brightness) / 255);
}

static void put(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness) {
    if (!initialized) {
        return;
    }
    r = scale(r, brightness);
    g = scale(g, brightness);
    b = scale(b, brightness);
#if WS2812_RGB_ORDER
    uint32_t word = ((uint32_t) r << 24) | ((uint32_t) g << 16) | ((uint32_t) b << 8);
#else
    uint32_t word = ((uint32_t) g << 24) | ((uint32_t) r << 16) | ((uint32_t) b << 8);
#endif
    // Bits are shifted out MSB first with a 24-bit autopull threshold, so the
    // colour sits in the top 24 bits of the FIFO word. The joined TX FIFO is 8
    // deep, so for a few LEDs this does not actually block.
    for (int i = 0; i < WS2812_NUM_LEDS; i++) {
        pio_sm_put_blocking(pio, sm, word);
    }
}

void ws2812_led_put_rgb(uint8_t r, uint8_t g, uint8_t b) {
    put(r, g, b, WS2812_BRIGHTNESS);
}

// Show 0xBBRRGGBB. BB = brightness (0x00 = use WS2812_BRIGHTNESS).
static void show(uint32_t bbrrggbb) {
    if (shown_valid && (bbrrggbb == shown)) {
        return;
    }
    uint8_t brightness = bbrrggbb >> 24;
    if (brightness == 0) {
        brightness = WS2812_BRIGHTNESS;
    }
    put((bbrrggbb >> 16) & 0xFF, (bbrrggbb >> 8) & 0xFF, bbrrggbb & 0xFF, brightness);
    shown = bbrrggbb;
    shown_valid = true;
}

bool ws2812_led_init() {
    // Finds any PIO block with a free state machine and room for the 4
    // instructions, so it coexists with Pico-PIO-USB (pio0/sm0, pio1/sm0+1).
    if (!pio_claim_free_sm_and_add_program_for_gpio_range(&ws2812_program, &pio, &sm, &offset, WS2812_PIN, 1, true)) {
        return false;
    }
    ws2812_program_init(pio, sm, offset, WS2812_PIN, WS2812_FREQ, 24);
    initialized = true;

    sweep_step = 0;
    sweep_next = time_us_64();
    return true;
}

void ws2812_led_task() {
    if (!initialized) {
        return;
    }
    uint64_t now = time_us_64();

    if (sweep_step < SWEEP_STEPS) {
        if (now >= sweep_next) {
            const uint8_t* c = sweep_colors[sweep_step];
            ws2812_led_put_rgb(c[0], c[1], c[2]);
            sweep_step++;
            sweep_next = now + SWEEP_STEP_US;
        }
        return;
    }

    if (activity_lit && (now > activity_off_at)) {
        activity_lit = false;
    }

    // A colour in the register wins; otherwise off, with the green activity
    // flash on top.
    uint32_t value = color_from_register();
    if ((value & 0xFFFFFF) == 0) {
        value = activity_lit ? 0x00FF00 : 0x000000;
    }
    show(value);
}

void ws2812_led_activity_on() {
    if (!initialized || (sweep_step < SWEEP_STEPS)) {
        return;
    }
    activity_lit = true;
    activity_off_at = time_us_64() + ACTIVITY_US;
}
