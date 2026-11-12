#include "ws2812_led.h"

#include "ws2812.pio.h"  // Generated from ws2812.pio by pico_generate_pio_header().

#include <hardware/gpio.h>
#include <hardware/pio.h>
#include <hardware/timer.h>

#define WS2812_FREQ 800000

// Time for the last pixel to shift out (30 us) plus the reset/latch gap
// (50 us for old WS2812, 280 us for WS2812B-V5 and clones).
#define WS2812_LATCH_US 350

// PIO resources and pin, only held while active.
static bool active = false;
static PIO pio = NULL;
static uint sm = 0;
static uint offset = 0;
static uint pin = 0;
static gpio_function_t prev_function = GPIO_FUNC_NULL;

// Colour order (3 nibbles, see WS2812_COLOR_ORDER) and master brightness.
static uint16_t order = WS2812_COLOR_ORDER;
static uint8_t master = 255;

// Master brightness levels for the settings L nibble. 0 = full; 1..15 rise
// roughly x1.35 per step, so the low end stays usable.
static const uint8_t master_levels[16] = {
    255, 2, 4, 6, 9, 13, 18, 25, 34, 46, 62, 84, 113, 152, 205, 255,
};

// Last settings value acted on, and last colour pushed (0xIIRRGGBB). The FIFO
// is only written when the colour changes; shown_valid is cleared when the
// settings change.
static uint32_t applied_settings = 0;
static uint32_t shown = 0;
static bool shown_valid = false;

// True if `o` is three nibbles that are a permutation of 1, 2, 3.
static bool order_valid(uint16_t o) {
    uint8_t a = (o >> 8) & 0xF;
    uint8_t b = (o >> 4) & 0xF;
    uint8_t c = o & 0xF;
    return (a >= 1) && (a <= 3) && (b >= 1) && (b <= 3) && (c >= 1) && (c <= 3) &&
           (a != b) && (b != c) && (a != c);
}

static bool pin_valid(uint p) {
    if (p >= NUM_BANK0_GPIOS) {
        return false;
    }
#ifdef PICO_DEFAULT_PIO_USB_DP_PIN
    // Never take the PIO-USB host port pins.
    if ((p == PICO_DEFAULT_PIO_USB_DP_PIN) || (p == PICO_DEFAULT_PIO_USB_DP_PIN + 1)) {
        return false;
    }
#endif
    return true;
}

static inline uint8_t scale(uint8_t v, uint8_t intensity) {
    return (uint8_t) (((uint16_t) v * intensity) / 255);
}

void ws2812_led_show(uint32_t iirrggbb) {
    if (!active) {
        return;
    }
    // II = intensity (0 = default), scaled by the master level and rounded
    // up so a lit colour never drops to 0.
    uint16_t intensity = iirrggbb >> 24;
    if (intensity == 0) {
        intensity = WS2812_DEFAULT_INTENSITY;
    }
    intensity = (intensity * master + 254) / 255;

    // Channel n (1 = R, 2 = G, 3 = B) for each of the three bytes sent.
    const uint8_t channel[4] = {
        0,
        scale((iirrggbb >> 16) & 0xFF, intensity),
        scale((iirrggbb >> 8) & 0xFF, intensity),
        scale(iirrggbb & 0xFF, intensity),
    };
    uint32_t word = ((uint32_t) channel[(order >> 8) & 0xF] << 24) |
                    ((uint32_t) channel[(order >> 4) & 0xF] << 16) |
                    ((uint32_t) channel[order & 0xF] << 8);
    // Bits are shifted out MSB first with a 24-bit autopull threshold, so the
    // colour sits in the top 24 bits of the FIFO word. The joined TX FIFO is 8
    // deep, so for a few LEDs this does not actually block.
    for (int i = 0; i < WS2812_NUM_LEDS; i++) {
        pio_sm_put_blocking(pio, sm, word);
    }
    shown = iirrggbb;
    shown_valid = true;
}

// Claim a free state machine on any PIO block with room for the 4
// instructions (coexists with Pico-PIO-USB) and take over the pin.
static bool activate(uint new_pin) {
    if (!pio_claim_free_sm_and_add_program_for_gpio_range(&ws2812_program, &pio, &sm, &offset, new_pin, 1, true)) {
        return false;
    }
    pin = new_pin;
    prev_function = gpio_get_function(pin);
    ws2812_program_init(pio, sm, offset, pin, WS2812_FREQ, 24);
    active = true;
    return true;
}

// Turn the LED off and give everything back: state machine, program space
// and the pin (restored to the function it had before).
static void deactivate() {
    ws2812_led_show(0);
    while (!pio_sm_is_tx_fifo_empty(pio, sm)) {
    }
    busy_wait_us(WS2812_LATCH_US);
    pio_sm_set_enabled(pio, sm, false);
    pio_remove_program_and_unclaim_sm(&ws2812_program, pio, sm, offset);
    gpio_set_function(pin, prev_function);
    active = false;
}

bool ws2812_led_configure(uint32_t settings) {
    applied_settings = settings;
    shown_valid = false;  // re-send the colour after any settings change

    uint p = settings & 0xFF;
    uint16_t o = (settings >> 8) & 0xFFF;
    if (o == 0) {
        o = WS2812_COLOR_ORDER;
    }
    // 0 or anything invalid means "no LED": release it if we had one.
    if ((settings == 0) || (settings >> 24) || !pin_valid(p) || !order_valid(o)) {
        if (active) {
            deactivate();
        }
        return false;
    }

    if (active && (p != pin)) {
        deactivate();
    }
    if (!active && !activate(p)) {
        return false;  // no free PIO resources; retried when settings change
    }
    order = o;
    master = master_levels[(settings >> 20) & 0xF];
    return true;
}

#ifdef WS2812_ENABLED
// hid-remapper's expression registers (remapper.cc). The config drives the
// LED with e.g. `0x00012310 31 store` and `0x20FF0000 32 store`.
extern int32_t registers[];

void ws2812_led_task() {
    uint32_t settings = (uint32_t) registers[WS2812_SETTINGS_REGISTER - 1];
    if (settings != applied_settings) {
        ws2812_led_configure(settings);
    }
    if (!active) {
        return;
    }
    uint32_t value = (uint32_t) registers[WS2812_COLOR_REGISTER - 1];
    if (!shown_valid || (value != shown)) {
        ws2812_led_show(value);
    }
}
#endif
