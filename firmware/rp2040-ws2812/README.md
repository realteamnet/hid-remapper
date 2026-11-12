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
| `ws2812_led.cc` | PIO setup; shows the colour the config writes to register 32. No built-in colours. |
| `ws2812_test.cc`| Standalone proof-of-concept firmware (`ws2812_test.uf2`): R, G, B, W cycle, no USB. |
| `ws2812.cmake`  | All build logic; pulled in by one line in `firmware/CMakeLists.txt`. |
| `example_scroll_lock_layers.json` | Example config: Scroll Lock cycles layers 0-7 (W R O Y G B I V), colour keys, Z X C V B animations. |

## API

```c
bool ws2812_led_init();                                // claim PIO SM, LED off
void ws2812_led_put_rgb(uint8_t r, uint8_t g, uint8_t b); // set all LEDs now
void ws2812_led_task();                                // call every main-loop iteration
```

## Configuration

C defines (`target_compile_definitions`):

| Define              | Default | Meaning                                        |
| ------------------- | ------- | ---------------------------------------------- |
| `WS2812_PIN`        | `16`    | Data GPIO.                                     |
| `WS2812_NUM_LEDS`   | `1`     | LEDs in the chain.                             |
| `WS2812_COLOR_ORDER` | `0x213` | Default colour order: 3 nibbles, channel sent 1st/2nd/3rd (1 = R, 2 = G, 3 = B). `0x213` = GRB (standard), `0x123` = RGB. |
| `WS2812_DEFAULT_INTENSITY` | `32` | Intensity 0..255 used when a colour's `II` byte is `00`. |
| `WS2812_COLOR_REGISTER` | `32` | Expression register (1-32) whose value (`0xIIRRGGBB`) sets the LED; `0` disables. |
| `WS2812_SETTINGS_REGISTER` | `31` | Expression register (1-32) with runtime LED settings (`0x00LOOOPP`, see below); `0` disables. |

CMake variables (set before the include, or with `-D`): `WS2812_BOARDS`
(default `pico`) — boards whose `remapper` target gets the LED; `WS2812_PIN`
(default `16`); `WS2812_COLOR_ORDER` (default `0x123`: the RP2040-Zero clone
used here has an RGB-ordered LED, confirmed on hardware).

## Setting the colour from the config (expressions)

The LED colour is read from hid-remapper **expression register 32** every main
loop iteration, as **`0xIIRRGGBB`**:

- `II` = intensity (`0x01`-`0xFF`); `00` = default (`WS2812_DEFAULT_INTENSITY`), so
  plain `0xRRGGBB` also works.
- `0x20FF0000 32 store` in any expression = red at intensity 0x20.
- The firmware has **no colours of its own**: the LED is off until the config
  writes the register (all registers are 0 at power-on), and then shows exactly
  that value. Colours per layer, toggles etc. are all done in the config.

No hid-remapper code is changed for this: the driver reads the existing global
`registers[]` array from `remapper.cc`. Layers are still switched the normal
hid-remapper way: only a mapping with a "Layer N" target can activate a layer
(one mapping per layer), and an expression can be that mapping's input.

### LED settings from the config: GPIO, colour order, master brightness (register 31)

Expression **register 31** sets the data pin, colour order and master
brightness at runtime, as `0x00LOOOPP`:

- `PP` = data GPIO in hex (`0x10` = GPIO16).
- `OOO` = colour order, three nibbles naming the channel sent first, second,
  third: 1 = R, 2 = G, 3 = B. `123` = RGB, `213` = GRB (standard WS2812),
  `231`, `312`, ... any permutation.
- `L` = master brightness, `1` (dimmest) to `F` (full); `0` = full. It
  multiplies every colour's `II`, so one value dims everything (layer colours,
  keys, animations). Levels follow a steep curve (about x1.35 per step:
  2, 4, 6, 9, 13, 18, 25, 34, 46, 62, 84, 113, 152, 205, 255 out of 255) so the
  low end stays usable; the result is rounded up so a lit colour never goes
  fully dark.
- `0` (the power-on value) = build defaults (`WS2812_PIN`,
  `WS2812_COLOR_ORDER`, full brightness).

Examples: `0x00012310 31 store` = GPIO16, RGB, full. `0x00812310 31 store` =
same at level 8. `0x00021305 31 store` = GPIO5, GRB, full. Put it at the start of any expression; writing it every tick is fine, the
driver only acts when the value changes.

- Changing the pin turns the LED on the old pin off, returns the old pin to a
  plain input and moves the PIO state machine to the new pin.
- Invalid fields are ignored (that part stays as it is): pins ≥ 30, the
  PIO-USB host pins (GPIO0/1 on `pico`), or an order that isn't a permutation
  of 1, 2, 3.
- Don't use a pin that is also used in your GPIO mappings. hid-remapper still
  treats other pins as remappable GPIOs (it may set pull-ups on them and read
  them as inputs); that doesn't disturb the LED, but a mapping driving the same
  pin would.
- Intensity is not part of the order: it scales R, G and B before sending.
  RGBW (SK6812) LEDs are not supported yet.

### Example config: Scroll Lock cycles layers 0-7, colour keys, animations

`example_scroll_lock_layers.json` (import it in the web config tool, then
"Save to device"; it **replaces** the current config). Everything below is
config; the firmware only displays register 32.

**Layers (W R O Y G B I V).** Mappings: Scroll Lock → Layer k+1, **sticky**, on
layer k, for k = 0-6. Each press latches
the next layer and releases the current one; on layer 7 nothing latches, the
release clears it and layer 0 is back. Expression 1 (first section) sets the
layer colour only when `layer_state` changes (register 30 = last state), as a sum of
`layer_state 0x.. eq <colour> mul` terms: 0 white, 1 red, 2 orange, 3 yellow,
4 green, 5 blue, 6 indigo, 7 violet (intensity 0x20).

hid-remapper supports 8 layers (0-7). Upstream `remapper.cc` still had a stale
`NLAYERS = 4`, which made sticky layer keys leak to the computer on layers 4-7
(and momentary layer mappings to 4-7 flicker unless their target layer is
ticked). This fork fixes it (`NLAYERS = 8`); with upstream firmware add
Scroll Lock → Nothing on layer 7.

**Colour keys (any layer).** Expression 1, second section: Q W E R T Y U = red orange yellow
green blue indigo violet at intensity 0x20. Third section: A S D F G H J = the
same at full intensity 0xFF. The last event wins (key colour stays until the
next colour key or layer change). These keys still type normally.

**Animations on Z X C V B (any layer).** A key starts its animation, the same
key again stops it, another key switches. Z red strobe 10 Hz, X green blink
2 Hz (full intensity), C police red/blue 4 Hz, V blue breathing (2 s), B
rainbow (1 cycle/s). The colour from before is restored when it stops.

| Register | Meaning |
| --- | --- |
| 29 | running animation: 0 none, 1-5 = Z X C V B |
| 21 | new value of 29 computed this tick |
| 28 | colour saved when an animation starts, restored when it stops |
| 25 | `time_sec` when the current animation started |
| 23 | seconds since it started (drives blink phase etc.) |
| 30 | last `layer_state` (expression 1, layer section) |
| 32 | LED colour `0xIIRRGGBB` |

Expression 1 = layer colour + colour keys (157 elements; the sections run in
order, each ending in `store`, so they share one expression). Expression 2 =
animation keys and state, expression 3 = animation rendering; together those
two are 268 elements, more than the **255 per expression** limit, so they stay
split. Order matters: the animation runs last so it overrides the other colours.

Limits when editing: at most **255 elements** per expression (`eol` line breaks
count, comments don't) and a **16**-deep stack. `time_sec` is milliseconds on
the x1000 scale (wraps after ~24 days), `sin` takes degrees, `round` gives
whole numbers, so `round(x) 0x10000 mul` places a 0-255 value in the red byte
and `0x1000000 mul` in the intensity byte. Decimal numbers are x1000
internally; hex values are used as-is.

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

### 2. `firmware/src/main.cc` — three guarded blocks

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
   Red/green swapped → change the order (`WS2812_COLOR_ORDER`, or at runtime
   with register 31).
2. Flash `remapper.uf2`: LED off; USB device works as normal. Then save a
   config that writes register 32 (e.g. the example) and the LED follows it.
