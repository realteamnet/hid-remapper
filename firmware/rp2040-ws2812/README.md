# rp2040-ws2812

A small WS2812 (NeoPixel) RGB LED driver for the
[hid-remapper](https://github.com/jfedor2/hid-remapper) RP2040 firmware.
Target hardware: **Waveshare RP2040-Zero** (WS2812 on GPIO16), running the
`pico` build.

Everything lives in this folder so it can later become a git submodule.

## Files

| File            | Purpose                                                              |
| --------------- | -------------------------------------------------------------------- |
| `ws2812.pio`    | PIO assembler program, converted from the MicroPython `@rp2.asm_pio` sample, plus `ws2812_program_init()`. |
| `ws2812_led.h`  | Public API and compile-time configuration.                           |
| `ws2812_led.cc` | PIO setup, colour output, power-on sweep, activity blink.            |
| `ws2812_test.cc`| Standalone proof-of-concept firmware (`ws2812_test.uf2`): R, G, B, W cycle, no USB. |
| `ws2812.cmake`  | All build logic; pulled in by one line in `firmware/CMakeLists.txt`. |

## API

```c
bool ws2812_led_init();                                // claim PIO SM, start power-on sweep
void ws2812_led_put_rgb(uint8_t r, uint8_t g, uint8_t b); // set all LEDs now
void ws2812_led_task();                                // call every main-loop iteration
void ws2812_led_activity_on();                         // brief green flash
```

## Configuration

C defines (`target_compile_definitions`):

| Define              | Default | Meaning                                        |
| ------------------- | ------- | ---------------------------------------------- |
| `WS2812_PIN`        | `16`    | Data GPIO.                                     |
| `WS2812_NUM_LEDS`   | `1`     | LEDs in the chain.                             |
| `WS2812_RGB_ORDER`  | `0`     | `0` = GRB (standard), `1` = RGB. Flip if red/green are swapped. |
| `WS2812_BRIGHTNESS` | `32`    | Global brightness scale 0..255.                |

CMake variables (set before the include, or with `-D`): `WS2812_BOARDS`
(default `pico`) — boards whose `remapper` target gets the LED; `WS2812_PIN`.

## Changes required to integrate into hid-remapper

Boards not in `WS2812_BOARDS` build exactly as upstream (the `#ifdef`s compile
away and no source is added).

### 1. `firmware/CMakeLists.txt` — one line

After the `target_link_libraries(remapper ...)` block:

```cmake
include(rp2040-ws2812/ws2812.cmake OPTIONAL)
```

`OPTIONAL` means the build still works if this folder is absent. The included
file adds the driver source, generates `ws2812.pio.h` with `pioasm`, adds the
include directory and defines `WS2812_ENABLED WS2812_PIN=16` on the `remapper`
target, and adds the `ws2812_test` executable.

### 2. `firmware/src/main.cc` — four guarded blocks

```c
#include "tick.h"
#ifdef WS2812_ENABLED
#include "ws2812_led.h"
#endif
...
    stdio_init_all();
#ifdef WS2812_ENABLED
    ws2812_led_init();          // after tusb_init() (Pico-PIO-USB has its PIO SMs)
#endif                          // and after stdio (GPIO16 is UART TX on `pico`)
...
            activity_led_on();
#ifdef WS2812_ENABLED
            ws2812_led_activity_on();   // in both the new_report and gpio branches
#endif
...
        activity_led_off_maybe();
#ifdef WS2812_ENABLED
        ws2812_led_task();
#endif
```

### 3. GPIO mask — no change needed for `pico`

hid-remapper treats every pin in `get_gpio_valid_pins_mask()`
(`src/remapper_single.cc`) as a remappable GPIO. On the `pico` build GPIO16 is
`PICO_DEFAULT_UART_TX_PIN`, which is already excluded. On another board, if
`WS2812_PIN` is inside `GPIO_VALID_PINS_BASE`, add `(1 << WS2812_PIN)` to the
exclusion list under `#ifdef WS2812_ENABLED`.

## Resource use

- One PIO state machine and 4 instruction slots, found with
  `pio_claim_free_sm_and_add_program_for_gpio_range()` (Pico-PIO-USB already
  uses pio0/sm0 and pio1/sm0+sm1).
- No DMA, no IRQ. Pixels are written to the TX FIFO directly.

## Verify

1. Flash `ws2812_test.uf2`: LED cycles red, green, blue, white (1 s each).
   Red/green swapped → build with `WS2812_RGB_ORDER=1`.
2. Flash `remapper.uf2`: red → green → blue sweep at power-on, then off; green
   flash on HID input; USB device works as normal.
