# rp2040-ws2812

A small, self-contained WS2812 (NeoPixel / SK6812) RGB LED driver for the
[hid-remapper](https://github.com/jfedor2/hid-remapper) RP2040/RP2350 firmware.

The driver lives entirely in this folder so it can later be detached as its own
git submodule. It uses the RP2040 PIO to generate the WS2812 one-wire waveform
and writes pixels with blocking FIFO pushes (no DMA channel is claimed, so it
never conflicts with the Pico-PIO-USB host).

## Files

| File            | Purpose                                                        |
| --------------- | -------------------------------------------------------------- |
| `ws2812.pio`    | PIO program + `ws2812_program_init()` (BSD-3-Clause, pico-examples). |
| `ws2812_led.h`  | Public API and compile-time configuration.                     |
| `ws2812_led.cc` | Framebuffer, PIO setup, and an optional activity-blink helper.  |

## Public API

```c
void ws2812_led_init();                                   // call once, after board_init()
void ws2812_led_set(uint32_t index, uint8_t r, uint8_t g, uint8_t b);
void ws2812_led_set_all(uint8_t r, uint8_t g, uint8_t b);
void ws2812_led_show();                                   // flush framebuffer to strip
void ws2812_led_activity_on();                            // optional activity_led replacement
void ws2812_led_activity_off_maybe();
```

## Compile-time configuration

Set these with `target_compile_definitions(...)` (or in a board header). All
have defaults so the module builds without configuration.

| Define              | Default  | Meaning                                  |
| ------------------- | -------- | ---------------------------------------- |
| `WS2812_PIN`        | `16`     | GPIO driving the LED data line.          |
| `WS2812_NUM_LEDS`   | `1`      | Number of LEDs in the chain.             |
| `WS2812_IS_RGBW`    | `0`      | `1` for RGBW (SK6812) strips.            |
| `WS2812_PIO`        | `pio0`   | PIO block to use.                        |
| `WS2812_FREQ`       | `800000` | Bit rate in Hz.                          |
| `WS2812_ACTIVITY_*` | dim green / 50 ms | Colour and duration of the activity blink. |

---

## Changes required to integrate into hid-remapper

> These edits **are applied** in this tree, but every upstream call site is
> guarded by `#ifdef WS2812_ENABLED` and the define is only set for selected
> boards (see step 1). Boards without it compile byte-identically to upstream,
> so the module stays a low-impact, drop-in submodule. The sections below
> document exactly what was changed and why.

All paths below are relative to `firmware/` in the hid-remapper repo.

Enablement is per-board via `WS2812_ENABLED` + `WS2812_PIN`, set in
`CMakeLists.txt`. Currently enabled for `pico`, `pico2`, and
`waveshare_rp2040_pizero` (all WS2812-on-GPIO16):

```cmake
if((PICO_BOARD STREQUAL "pico") OR (PICO_BOARD STREQUAL "pico2") OR (PICO_BOARD STREQUAL "waveshare_rp2040_pizero"))
target_compile_definitions(remapper PUBLIC WS2812_ENABLED WS2812_PIN=16)
endif()
```

### 1. `firmware/CMakeLists.txt`

Add the source file, the include directory, and PIO header generation to the
`remapper` target (the single-board target built from `main.cc`):

```cmake
add_executable(remapper
    src/main.cc
    ...
    src/xbox.cc
    rp2040-ws2812/ws2812_led.cc          # <-- add
)

target_include_directories(remapper PRIVATE
    src
    src/tusb_config_both
    ${PICO_PIO_USB_PATH}
    rp2040-ws2812                        # <-- add
)

# Generate ws2812.pio.h next to the build and link the PIO library.
pico_generate_pio_header(remapper ${CMAKE_CURRENT_LIST_DIR}/rp2040-ws2812/ws2812.pio)  # <-- add
```

`hardware_pio` is already in `target_link_libraries(remapper ...)`, so no extra
link library is needed. (Repeat for `remapper_serial`/`remapper_dual_a` only if
you want the LED on those build variants.)

### 2. `firmware/src/main.cc`

Include the header, initialise the driver, and drive the activity blink — all
guarded by `WS2812_ENABLED`:

```c
#include "tick.h"
#ifdef WS2812_ENABLED            // (a) with the other includes
#include "ws2812_led.h"
#endif
```

```c
    tusb_init();
    stdio_init_all();
#ifdef WS2812_ENABLED            // (b) after stdio_init_all() so the PIO
    ws2812_led_init();           //     claims the pin last (UART overlap)
#endif
```

```c
        if (new_report) {
            activity_led_on();
#ifdef WS2812_ENABLED            // (c) mirror the activity_led calls
            ws2812_led_activity_on();
#endif
        }
        ...
        activity_led_off_maybe();
#ifdef WS2812_ENABLED
        ws2812_led_activity_off_maybe();
#endif
```

(The same `ws2812_led_activity_on()` guard is added in the `gpio_state_changed`
branch.) If you only want to set colours from your own logic, skip step (c) and
call `ws2812_led_set*()` / `ws2812_led_show()` directly instead.

### 3. Free the data-line GPIO from hid-remapper's pin management — **important**

hid-remapper claims every "valid" GPIO for remappable input/output. On startup
`main()` calls `gpio_pins_init()` → `gpio_init_mask(get_gpio_valid_pins_mask())`,
and the mapping engine may later drive any valid pin. If `WS2812_PIN` is inside
that mask, the LED data line will glitch.

Pick **one** of:

- **Use a pin outside `GPIO_VALID_PINS_BASE`** (defined in `CMakeLists.txt` for
  the `pico`/`pico2` boards, or in the board header for custom boards). Nothing
  else to change.
- **Exclude the pin from the valid mask.** In
  `firmware/src/remapper_single.cc`, `get_gpio_valid_pins_mask()` already masks
  out the USB and UART pins; add `WS2812_PIN` the same way:

  ```c
  uint32_t get_gpio_valid_pins_mask() {
      return GPIO_VALID_PINS_BASE & ~(
                                        ...
                                        (1 << (PICO_DEFAULT_PIO_USB_DP_PIN + 1)) |
  #ifdef WS2812_ENABLED
                                        (1 << WS2812_PIN) |   // <-- add
  #endif
                                        0);
  }
  ```

This is the approach used here (in `remapper_single.cc`).

### Default pin caveat

`WS2812_PIN` is `16` for the enabled boards, which is the WS2812 data line on
the Waveshare RP2040-PiZero (and `PICO_DEFAULT_UART_TX_PIN` on a stock Pico —
the module deliberately initialises after `stdio_init_all()` so the PIO wins the
pin). Set `WS2812_PIN` to a free GPIO for any other board before enabling it.

## Build / verify

Build the firmware as usual (e.g. `cmake -B build -DPICO_BOARD=pico && make -C build remapper`).
The generated `ws2812.pio.h` must be on the include path — `pico_generate_pio_header`
handles that automatically. The PIO program is 4 instructions and the driver
claims one free state machine on `WS2812_PIO`, so it coexists with the
Pico-PIO-USB host.
