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
| `example_scroll_lock_layers.json` | Config-tool import: Scroll Lock cycles layers 0-3, LED white/red/green/blue. |

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
| `WS2812_COLOR_REGISTER` | `32` | Expression register (1-32) whose value sets the LED colour; `0` disables. |
| `WS2812_COLOR_REGISTER` | `32` | Expression register (1-32) that sets the LED colour; `0` disables. |

CMake variables (set before the include, or with `-D`): `WS2812_BOARDS`
(default `pico`) — boards whose `remapper` target gets the LED; `WS2812_PIN`
(default `16`); `WS2812_RGB_ORDER` (default `1`: the RP2040-Zero clone used
here has an RGB-ordered LED, confirmed on hardware).

## Setting the colour from the config (expressions)

The LED colour is read from hid-remapper **expression register 32** every main
loop iteration, as `0xRRGGBB`:

- `0xFF0000 32 store` in any expression makes the LED red.
- Register value `0` (the default at power-on) = LED off, with a short green
  flash on HID activity. Any non-zero value shows that colour steadily and the
  activity flash is not shown.
- Brightness is scaled by `WS2812_BRIGHTNESS`.

No hid-remapper code is changed for this: the driver reads the existing global
`registers[]` array from `remapper.cc`.

### Example: Scroll Lock cycles layers, LED shows the layer (W, R, G, B)

`example_scroll_lock_layers.json` (import it in the web config tool, then
"Save to device"; it **replaces** the current config):

| Expression | Text (as typed in the config tool) | Purpose |
| --- | --- | --- |
| 1 | `0x00070047 input_state_binary 0x00070047 prev_input_state_binary not mul 1 recall add 4 mod 1 store` | On each Scroll Lock press: register 1 = (register 1 + 1) mod 4 |
| 2 | `1 recall 1 eq` | true when layer 1 is selected |
| 3 | `1 recall 2 eq` | true when layer 2 is selected |
| 4 | `1 recall 3 eq` | true when layer 3 is selected |
| 5 | `1 recall 1 eq 0xFF0000 1 recall 2 eq 0x00FF00 1 recall 3 eq 0x0000FF 0xFFFFFF ifte ifte ifte 32 store` | LED colour: layer 0 white, 1 red, 2 green, 3 blue |

Mappings (all on layers 0-3): Scroll Lock → Nothing (so the key is not sent to
the computer), Expression 2 → Layer 1, Expression 3 → Layer 2, Expression 4 →
Layer 3. Layer 0 is active when no other layer is. Add your own per-layer
mappings on top. Decimal numbers are scaled ×1000 internally; hex values (like
the colours) are used as-is.

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
