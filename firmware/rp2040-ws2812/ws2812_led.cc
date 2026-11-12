#include "ws2812_led.h"

#include "ws2812.pio.h"  // Generated from ws2812.pio by pico_generate_pio_header().

#include <hardware/pio.h>

#define WS2812_FREQ 800000

static PIO pio = NULL;
static uint sm = 0;
static uint offset = 0;
static bool initialized = false;

// Last value pushed to the LED (0xBBRRGGBB), so the FIFO is only written when
// it changes.
static uint32_t shown = 0;

#if defined(WS2812_ENABLED) && (WS2812_COLOR_REGISTER > 0)
// hid-remapper's expression registers (remapper.cc). The config sets the LED
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
    uint8_t brightness = bbrrggbb >> 24;
    if (brightness == 0) {
        brightness = WS2812_BRIGHTNESS;
    }
    put((bbrrggbb >> 16) & 0xFF, (bbrrggbb >> 8) & 0xFF, bbrrggbb & 0xFF, brightness);
    shown = bbrrggbb;
}

bool ws2812_led_init() {
    // Finds any PIO block with a free state machine and room for the 4
    // instructions, so it coexists with Pico-PIO-USB (pio0/sm0, pio1/sm0+1).
    if (!pio_claim_free_sm_and_add_program_for_gpio_range(&ws2812_program, &pio, &sm, &offset, WS2812_PIN, 1, true)) {
        return false;
    }
    ws2812_program_init(pio, sm, offset, WS2812_PIN, WS2812_FREQ, 24);
    initialized = true;

    // Clear whatever the LED latched before init (it can show white from
    // boot/UART activity on the data pin).
    show(0);
    return true;
}

void ws2812_led_task() {
    if (!initialized) {
        return;
    }
    uint32_t value = color_from_register();
    if (value != shown) {
        show(value);
    }
}
