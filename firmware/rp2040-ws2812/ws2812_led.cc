#include "ws2812_led.h"

#include "ws2812.pio.h"  // Generated from ws2812.pio by pico_generate_pio_header().

#include <hardware/gpio.h>
#include <hardware/pio.h>
#include <hardware/timer.h>

#define WS2812_FREQ 800000

static PIO pio = NULL;
static uint sm = 0;
static uint offset = 0;
static bool initialized = false;

// Active data pin and colour order (3 nibbles, see WS2812_COLOR_ORDER).
static uint pin = WS2812_PIN;
static uint16_t order = WS2812_COLOR_ORDER;

// Master brightness levels for the settings register's L nibble. 0 = full;
// 1..15 rise roughly x1.35 per step, so the low end stays usable.
static const uint8_t master_levels[16] = {
    255, 2, 4, 6, 9, 13, 18, 25, 34, 46, 62, 84, 113, 152, 205, 255,
};
static uint8_t master = 255;

// Last value pushed to the LED (0xIIRRGGBB), so the FIFO is only written when
// it changes. shown_valid is cleared when the pin or order changes.
static uint32_t shown = 0;
static bool shown_valid = false;

// Last settings register value acted on.
static uint32_t applied_settings = 0;

#if defined(WS2812_ENABLED)
// hid-remapper's expression registers (remapper.cc). The config sets the LED
// with e.g. `0x20FF0000 32 store` (intensity 0x20, red).
extern int32_t registers[];
#endif

static inline uint32_t read_register(int n) {
#if defined(WS2812_ENABLED)
    if (n > 0) {
        return (uint32_t) registers[n - 1];
    }
#endif
    return 0;
}

// True if `o` is three nibbles that are a permutation of 1, 2, 3.
static bool order_valid(uint16_t o) {
    uint8_t a = (o >> 8) & 0xF;
    uint8_t b = (o >> 4) & 0xF;
    uint8_t c = o & 0xF;
    return (o <= 0xFFF) && (a >= 1) && (a <= 3) && (b >= 1) && (b <= 3) && (c >= 1) && (c <= 3) &&
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

static void put(uint8_t r, uint8_t g, uint8_t b, uint8_t intensity) {
    if (!initialized) {
        return;
    }
    // Channel n (1 = R, 2 = G, 3 = B) for each of the three bytes sent.
    const uint8_t channel[4] = { 0, scale(r, intensity), scale(g, intensity), scale(b, intensity) };
    uint32_t word = ((uint32_t) channel[(order >> 8) & 0xF] << 24) |
                    ((uint32_t) channel[(order >> 4) & 0xF] << 16) |
                    ((uint32_t) channel[order & 0xF] << 8);
    // Bits are shifted out MSB first with a 24-bit autopull threshold, so the
    // colour sits in the top 24 bits of the FIFO word. The joined TX FIFO is 8
    // deep, so for a few LEDs this does not actually block.
    for (int i = 0; i < WS2812_NUM_LEDS; i++) {
        pio_sm_put_blocking(pio, sm, word);
    }
}

void ws2812_led_put_rgb(uint8_t r, uint8_t g, uint8_t b) {
    put(r, g, b, WS2812_DEFAULT_INTENSITY);
}

// Show 0xIIRRGGBB. II = intensity (0x00 = use WS2812_DEFAULT_INTENSITY),
// scaled by the master level (rounded up, so it never drops to 0).
static void show(uint32_t iirrggbb) {
    uint16_t intensity = iirrggbb >> 24;
    if (intensity == 0) {
        intensity = WS2812_DEFAULT_INTENSITY;
    }
    intensity = (intensity * master + 254) / 255;
    put((iirrggbb >> 16) & 0xFF, (iirrggbb >> 8) & 0xFF, iirrggbb & 0xFF, intensity);
    shown = iirrggbb;
    shown_valid = true;
}

// Move the state machine to a new data pin. The old pin goes back to a plain
// input so it no longer drives anything.
static void move_to_pin(uint new_pin) {
    show(0);  // turn the LED on the old pin off first
    while (!pio_sm_is_tx_fifo_empty(pio, sm)) {
    }
    busy_wait_us(100);  // last 24 bits (30 us) plus the >50 us latch gap
    pio_sm_set_enabled(pio, sm, false);
    gpio_init(pin);
    pin = new_pin;
    ws2812_program_init(pio, sm, offset, pin, WS2812_FREQ, 24);
}

// Settings register: 0x00LOOOPP. PP = GPIO, OOO = colour order nibbles,
// L = master brightness level (0 = full). 0 = build defaults. Invalid pin or
// order fields are ignored (that part stays as it was).
static void apply_settings(uint32_t settings) {
    uint new_pin = WS2812_PIN;
    uint16_t new_order = WS2812_COLOR_ORDER;
    master = master_levels[(settings >> 20) & 0xF];
    if (settings != 0) {
        uint p = settings & 0xFF;
        uint16_t o = (settings >> 8) & 0xFFF;
        new_pin = pin_valid(p) ? p : pin;
        new_order = order_valid(o) ? o : order;
    }
    if (new_pin != pin) {
        move_to_pin(new_pin);
    }
    order = new_order;
    shown_valid = false;  // re-send the colour with the new pin/order
    applied_settings = settings;
}

bool ws2812_led_init() {
    // Finds any PIO block with a free state machine and room for the 4
    // instructions, so it coexists with Pico-PIO-USB (pio0/sm0, pio1/sm0+1).
    if (!pio_claim_free_sm_and_add_program_for_gpio_range(&ws2812_program, &pio, &sm, &offset, pin, 1, true)) {
        return false;
    }
    ws2812_program_init(pio, sm, offset, pin, WS2812_FREQ, 24);
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
    uint32_t settings = read_register(WS2812_SETTINGS_REGISTER);
    if (settings != applied_settings) {
        apply_settings(settings);
    }
    uint32_t value = read_register(WS2812_COLOR_REGISTER);
    if (!shown_valid || (value != shown)) {
        show(value);
    }
}
