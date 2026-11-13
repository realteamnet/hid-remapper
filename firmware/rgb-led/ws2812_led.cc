#include "ws2812_led.h"

#include <hardware/gpio.h>
#include <hardware/pio.h>
#include <hardware/timer.h>

#include "ws2812.pio.h"

#define WS2812_FREQ 800000
#define WS2812_LATCH_US 350

#define BOARD_PIN 0xFF
#define POWER_PIN_FLAG 0x80
#define NO_PIN 0xFF

extern int32_t registers[];  // remapper.cc

static_assert((WS2812_SETTINGS_REGISTER >= 1) && (WS2812_SETTINGS_REGISTER <= 32) &&
              (WS2812_COLOR_REGISTER >= 1) && (WS2812_COLOR_REGISTER <= 32) &&
              (WS2812_SETTINGS_REGISTER != WS2812_COLOR_REGISTER));

static const uint8_t brightness_levels[16] = { 255, 2, 4, 6, 9, 13, 18, 25, 34, 46, 62, 84, 113, 152, 205, 255 };

static bool active = false;
static PIO pio;
static uint sm;
static uint offset;
static uint pin;
static uint power_pin = NO_PIN;
static gpio_function_t prev_function;
static gpio_function_t prev_power_function;
static uint8_t order[3];
static uint8_t brightness;

static uint32_t prev_settings = 0;
static uint32_t prev_color = 0;

static bool pin_ok(uint p) {
    return (p < NUM_BANK0_GPIOS) &&
#ifdef PICO_DEFAULT_PIO_USB_VBUSEN_PIN
           (p != PICO_DEFAULT_PIO_USB_VBUSEN_PIN) &&
#endif
           (p != PICO_DEFAULT_PIO_USB_DP_PIN) && (p != PICO_DEFAULT_PIO_USB_DP_PIN + 1);
}

static void show(uint32_t color) {
    uint32_t intensity = color >> 24;
    if (intensity == 0) {
        intensity = WS2812_DEFAULT_INTENSITY;
    }
    intensity = (intensity * brightness + 254) / 255;

    uint32_t word = 0;
    for (int i = 0; i < 3; i++) {
        uint32_t channel = (color >> (8 * (3 - order[i]))) & 0xFF;
        word = (word | (channel * intensity / 255)) << 8;
    }
    for (int i = 0; i < WS2812_NUM_LEDS; i++) {
        pio_sm_put_blocking(pio, sm, word);
    }
    prev_color = color;
}

static void stop() {
    show(0);
    while (!pio_sm_is_tx_fifo_empty(pio, sm)) {
    }
    busy_wait_us(WS2812_LATCH_US);
    pio_sm_set_enabled(pio, sm, false);
    pio_remove_program_and_unclaim_sm(&ws2812_program, pio, sm, offset);
    gpio_set_function(pin, prev_function);
    if (power_pin != NO_PIN) {
        gpio_set_dir(power_pin, GPIO_IN);
        gpio_set_function(power_pin, prev_power_function);
        power_pin = NO_PIN;
    }
    active = false;
}

static void configure(uint32_t settings) {
    uint new_pin = settings & 0xFF;
    uint new_power_pin = NO_PIN;
    if (settings >> 24) {
        new_power_pin = (settings >> 24) & ~POWER_PIN_FLAG;
    }
    bool board_ok = true;
    if (new_pin == BOARD_PIN) {
#ifdef PICO_DEFAULT_WS2812_PIN
        new_pin = PICO_DEFAULT_WS2812_PIN;
#ifdef PICO_DEFAULT_WS2812_POWER_PIN
        if (new_power_pin == NO_PIN) {
            new_power_pin = PICO_DEFAULT_WS2812_POWER_PIN;
        }
#endif
#else
        board_ok = false;
#endif
    }
    uint8_t a = (settings >> 16) & 0x0F;
    uint8_t b = (settings >> 12) & 0x0F;
    uint8_t c = (settings >> 8) & 0x0F;
    if ((a | b | c) == 0) {
        a = 2;
        b = 1;
        c = 3;
    }
    bool valid = (settings != 0) && board_ok && pin_ok(new_pin) &&
                 (((settings >> 24) == 0) || ((settings >> 24) & POWER_PIN_FLAG)) &&
                 ((new_power_pin == NO_PIN) || (pin_ok(new_power_pin) && (new_power_pin != new_pin))) &&
                 a && b && c && ((a | b | c) == 3) && ((a ^ b ^ c) == 0);

    if (active && (!valid || (new_pin != pin) || (new_power_pin != power_pin))) {
        stop();
    }
    prev_settings = settings;
    if (!valid) {
        return;
    }
    if (!active) {
        if (!pio_claim_free_sm_and_add_program_for_gpio_range(&ws2812_program, &pio, &sm, &offset, new_pin, 1, true)) {
            return;
        }
        if (new_power_pin != NO_PIN) {
            power_pin = new_power_pin;
            prev_power_function = gpio_get_function(power_pin);
            gpio_init(power_pin);
            gpio_put(power_pin, true);
            gpio_set_dir(power_pin, GPIO_OUT);
        }
        pin = new_pin;
        prev_function = gpio_get_function(pin);
        ws2812_program_init(pio, sm, offset, pin, WS2812_FREQ);
        active = true;
    }
    order[0] = a;
    order[1] = b;
    order[2] = c;
    brightness = brightness_levels[(settings >> 20) & 0x0F];
    show(registers[WS2812_COLOR_REGISTER - 1]);
}

void ws2812_led_task() {
    uint32_t settings = registers[WS2812_SETTINGS_REGISTER - 1];
    if (settings != prev_settings) {
        configure(settings);
    }
    if (active) {
        uint32_t color = registers[WS2812_COLOR_REGISTER - 1];
        if (color != prev_color) {
            show(color);
        }
    }
}
