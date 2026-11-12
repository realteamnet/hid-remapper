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
| `WS2812_COLOR_REGISTER` | `32` | Expression register (1-32) whose value (`0xBBRRGGBB`) sets the LED; `0` disables. |
| `WS2812_COLOR_REGISTER` | `32` | Expression register (1-32) that sets the LED colour; `0` disables. |

CMake variables (set before the include, or with `-D`): `WS2812_BOARDS`
(default `pico`) — boards whose `remapper` target gets the LED; `WS2812_PIN`
(default `16`); `WS2812_RGB_ORDER` (default `1`: the RP2040-Zero clone used
here has an RGB-ordered LED, confirmed on hardware).

## Setting the colour from the config (expressions)

The LED colour is read from hid-remapper **expression register 32** every main
loop iteration, as **`0xBBRRGGBB`**:

- `BB` = brightness (`0x01`-`0xFF`); `00` = default (`WS2812_BRIGHTNESS`), so
  plain `0xRRGGBB` also works.
- `0x20FF0000 32 store` in any expression = red at brightness 0x20.
- Register value with `RRGGBB` = 0 (the default at power-on) = LED off, with a
  short green flash on HID activity. Any other colour is shown steadily and the
  activity flash is not shown.

No hid-remapper code is changed for this: the driver reads the existing global
`registers[]` array from `remapper.cc`. Layers are still switched the normal
hid-remapper way: only a mapping with a "Layer N" target can activate a layer
(one mapping per layer), and an expression can be that mapping's input.

### Example: Scroll Lock cycles layers 0-3, LED white / red / green / blue

`example_scroll_lock_layers.json` (import it in the web config tool, then
"Save to device"; it **replaces** the current config).

Mappings (all on layers 0-3):

| Input | Output | Why |
| --- | --- | --- |
| Scroll Lock | Register 2 | Picks the key in the UI; register 2 = 1 while held. Also stops the key reaching the computer. |
| Expression 2 | Layer 1 | |
| Expression 3 | Layer 2 | |
| Expression 4 | Layer 3 | Layer 0 is active when no other layer is. |

Expressions (as typed in the config tool):

```
Expression 1 (next layer on each press, kept in register 1; register 3 = previous key state):
2 recall 3 recall not mul
1 recall add 4 mod
dup 1 store
2 recall 3 store

Expression 2:  1 recall 1 eq
Expression 3:  1 recall 2 eq
Expression 4:  1 recall 3 eq

Expression 5 (LED colour by active layer, 0xBBRRGGBB -> register 32):
layer_state 0x02 eq 0x20FF0000     IF layer 1: red
layer_state 0x04 eq 0x2000FF00     IF layer 2: green
layer_state 0x08 eq 0x200000FF     IF layer 3: blue
0x20FFFFFF                         ELSE (layer 0): white
ifte ifte ifte
32 store
```

(The IF/ELSE notes are explanation only; `/* comments */` are allowed in the
tool.) `layer_state` is the active-layer bit mask (layer 0 = 0x01, 1 = 0x02,
2 = 0x04, 3 = 0x08), so expression 5 follows the real active layer however it
was selected. Decimal numbers are scaled ×1000 internally; hex values are used
as-is. To change the key, change the input of the first mapping.

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
