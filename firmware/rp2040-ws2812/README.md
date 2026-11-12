# rp2040-ws2812

WS2812 (NeoPixel) RGB LED support for the
[hid-remapper](https://github.com/jfedor2/hid-remapper) RP2040/RP2350 firmware,
driven entirely by the hid-remapper config.

- **Any board.** The module is built into the `remapper` target on every board.
  Nothing in it is board specific.
- **Dormant by default.** Until the config writes the settings register, no LED
  code runs: no PIO state machine is claimed and no pin is touched. A board
  without the LED configured behaves exactly like stock hid-remapper.
- **Nothing hardcoded.** The firmware has no colours, animations or layer
  logic. The config says which GPIO the LED is on and what colour to show, using
  two expression registers.

Everything lives in this folder so it can later become a git submodule.

## Quick start

In any expression (web config tool, "Expressions" tab):

```
0x00012310 31 store      /* LED on GPIO16, RGB order */
0x20FF0000 32 store      /* red at intensity 0x20 */
```

Save to device and the LED turns red. Developed on a Waveshare RP2040-Zero
clone (WS2812 on GPIO16, RGB order) running the `pico` build.

## The two registers

### Register 31: settings, `0x00LOOOPP`

| Field | Meaning |
| --- | --- |
| `PP` | Data GPIO in hex (`10` = GPIO16). Required. |
| `OOO` | Colour order: the channel sent first, second, third (1 = R, 2 = G, 3 = B). `213` = GRB (standard WS2812), `123` = RGB, or any other permutation. `000` = GRB. |
| `L` | Master brightness, `1` (dimmest) to `F` (full); `0` = full. |

- `0` (the power-on value) means **no LED**: nothing is claimed or touched.
- An invalid value also means no LED: a pin that doesn't exist, one of the
  PIO-USB host port pins, or an order that isn't a permutation of 1, 2, 3.
- Examples: `0x00012310` = GPIO16, RGB, full brightness. `0x00812310` = the same
  at level 8. `0x00000005` = GPIO5, GRB, full.
- Writing it every tick is fine. The driver only acts when the value changes.
- When the value becomes valid the driver claims a free PIO state machine and
  the pin. When the pin changes it moves. When the value goes back to 0 it turns
  the LED off, releases the state machine and program space, and restores the
  pin to the function it had before.
- The master level multiplies every colour's intensity, so one value dims
  everything. The 15 steps rise about 1.35x each (2, 4, 6, 9, 13, 18, 25, 34,
  46, 62, 84, 113, 152, 205, 255 out of 255). The result is rounded up, so a lit
  colour never goes fully dark.
- Don't use the LED pin in GPIO mappings. hid-remapper still treats it as a
  remappable GPIO (it may set a pull-up on it and read it as an input). That
  doesn't disturb the LED, but a mapping that reads or drives the same pin
  would misbehave.

### Register 32: colour, `0xIIRRGGBB`

- `II` = intensity, `01`-`FF`. `00` = the default (`WS2812_DEFAULT_INTENSITY`,
  32), so plain `0xRRGGBB` also works.
- `RR GG BB` = the colour. `0` = off.
- The LED is updated only when the value changes.
- Intensity scales R, G and B before they are sent. RGBW (SK6812) LEDs are not
  supported.

### Colour names in the web config tool

This fork's `config-tool-web` accepts colour names in expressions and saves
them as hex: `cyan` becomes `0x0000FFFF`, and `cyan:40` becomes `0x4000FFFF`
(intensity 0x40). The device and exported JSON only ever hold hex, so a config
loaded back from the device shows hex.

| Name | RRGGBB | Name | RRGGBB |
| --- | --- | --- | --- |
| `off` | `000000` | `green` | `00FF00` |
| `white` | `FFFFFF` | `teal` | `00FF60` |
| `warmwhite` | `FFC0A0` | `cyan` | `00FFFF` |
| `red` | `FF0000` | `blue` | `0000FF` |
| `orange` | `FF2800` | `purple` | `8000FF` |
| `yellow` | `FF9000` | `magenta` | `FF00FF` |
| `lime` | `80FF00` | `pink` | `FF0040` |

The values are tuned for LEDs, not screens: screen orange (`FF7F00`) looks
yellow on a WS2812 and screen yellow (`FFFF00`) looks greenish. Indigo and
violet are left out because they are hard to tell apart from blue and magenta.
The orange, yellow, lime, teal, purple and pink values have not been checked on
hardware yet.

## Files

| File | Purpose |
| --- | --- |
| `ws2812.pio` | PIO program, converted line by line from the MicroPython `@rp2.asm_pio` sample, plus `ws2812_program_init()`. |
| `ws2812_led.h` | API and compile-time settings. |
| `ws2812_led.cc` | Driver: follows registers 31 and 32. |
| `ws2812.cmake` | All build logic; pulled in by one line in `firmware/CMakeLists.txt`. |
| `ws2812_test.cc` | Standalone diagnostic firmware (`ws2812_test.uf2`): R, G, B, W cycle, no USB. The only place with fixed colours. |
| `example_scroll_lock_layers.json` | Example config, described below. |

## API

```c
bool ws2812_led_configure(uint32_t settings);  // apply 0x00LOOOPP; true if the LED is active
void ws2812_led_show(uint32_t iirrggbb);       // show a colour (no-op while inactive)
void ws2812_led_task();                        // follow registers 31/32; call every main-loop iteration
```

## Build settings

CMake variables (`-D...`, or set before the include):

| Variable | Default | Meaning |
| --- | --- | --- |
| `WS2812_ENABLE` | `ON` | `OFF` leaves the module out of the build. |
| `WS2812_SETTINGS_REGISTER` | `31` | Expression register (1-32) holding the settings. |
| `WS2812_COLOR_REGISTER` | `32` | Expression register (1-32) holding the colour. |
| `WS2812_TEST_SETTINGS` | `0x00012310` | Settings used by `ws2812_test` only. |

A project that needs registers 31/32 for something else can move them, e.g.
`cmake -DWS2812_SETTINGS_REGISTER=7 -DWS2812_COLOR_REGISTER=8 ..`. Configs must
then use the new numbers.

C defines with defaults in `ws2812_led.h`: `WS2812_COLOR_ORDER` (`0x213`, used
when `OOO` is `000`), `WS2812_DEFAULT_INTENSITY` (`32`), `WS2812_NUM_LEDS` (`1`;
all LEDs show the same colour).

## Example config

`example_scroll_lock_layers.json`: import it in the web config tool, then
"Save to device". It **replaces** the current config. It is written for GPIO16
with RGB order; change the first line of expression 1 for another board.

**Layers.** Seven sticky mappings, Scroll Lock → Layer k+1 on layer k
(k = 0-6). Each press latches the next layer and releases the current one. On
layer 7 nothing latches, the release clears it, and layer 0 is back. Scroll
Lock is not sent to the computer.

**Expression 1** (160 of 255 elements, stack 15 of 16), in order:

1. `0x00012310 31 store`: the LED settings.
2. Layer colour, written only when `layer_state` changes (register 30 holds the
   last state), as a sum of `layer_state 0x.. eq <colour> mul` terms: 0 white,
   1 red, 2 orange, 3 yellow, 4 green, 5 cyan, 6 blue, 7 magenta, at intensity
   0x20.
3. Q W E R T Y U set red, orange, yellow, green, cyan, blue, magenta at
   intensity 0x20 on any layer.
4. A S D F G H J set the same colours at full intensity.

The last event wins: a key colour stays until the next colour key or layer
change. The colour keys still type normally.

**Expression 2** (242 of 255 elements, stack 13 of 16): animations on
Z X C V B, on any layer. A key starts its animation, the same key stops it,
another key switches. The colour from before is restored when it stops.

| Key | Animation |
| --- | --- |
| Z | red strobe, 10 Hz |
| X | green blink, 2 Hz, full intensity |
| C | police red/blue, 4 Hz |
| V | blue breathing, 2 s per breath |
| B | rainbow, 1 cycle per second |

| Register | Meaning |
| --- | --- |
| 21 | animation key pressed this tick (0 none, 1-5 = Z X C V B) |
| 22 | next value of register 29 |
| 23 | seconds since the animation started |
| 24 | rainbow angle |
| 25 | `time_sec` when the animation started |
| 28 | colour saved when an animation starts |
| 29 | running animation (0 none, 1-5) |
| 30 | last `layer_state` |
| 31, 32 | LED settings and colour |

Expression 2 runs after expression 1, so a running animation overrides the
layer and key colours.

**Limits when editing.** An expression holds at most **255 elements** (`eol`
line breaks count, comments don't) and the stack is **16** deep. Decimal
numbers are x1000 internally; hex values are used as-is. `time_sec` is in
milliseconds on that scale and wraps after about 24 days. `sin` takes degrees.
`round` gives whole numbers, so `round 0x10000 mul` places a 0-255 value in the
red byte and `round 0x1000000 mul` in the intensity byte.

**Layers 4-7 on upstream firmware.** Upstream `remapper.cc` still has a stale
`NLAYERS = 4`, which makes sticky layer keys leak to the computer on layers 4-7
and momentary layer mappings to 4-7 flicker unless their target layer is
ticked. This fork sets it to 8. With upstream firmware, add Scroll Lock →
Nothing on layer 7.

## Changes to hid-remapper's own files

With `WS2812_ENABLE=OFF`, or without this folder, the build is exactly
upstream.

### 1. `firmware/CMakeLists.txt`: one line

After the `target_link_libraries(remapper ...)` block:

```cmake
include(rp2040-ws2812/ws2812.cmake OPTIONAL)
```

The included file adds the driver source, the generated `ws2812.pio.h`, the
include directory and the `WS2812_ENABLED` define to the `remapper` target, and
defines the `ws2812_test` target (RP2040 only, built with `make ws2812_test`).

### 2. `firmware/src/main.cc`: two guarded blocks

```c
#include "tick.h"
#ifdef WS2812_ENABLED
#include "ws2812_led.h"
#endif
...
        activity_led_off_maybe();
#ifdef WS2812_ENABLED
        ws2812_led_task();
#endif
```

There is no init call: the driver sets itself up from `ws2812_led_task()` when
the settings register is first written, which is always after `tusb_init()` and
`stdio_init_all()`.

### 3. `config-tool-web/code.js`: colour names

A `led_colors` table and `led_color_to_hex()` next to the `ops` table, and four
lines in `expression_onchange()` that try a colour name before reporting an
invalid expression. Optional; the firmware doesn't depend on it.

## Resource use

- Dormant: one register read and compare per main-loop iteration.
- Active: one PIO state machine and 4 instruction slots on whichever PIO block
  has room (`pio_claim_free_sm_and_add_program_for_gpio_range()`; Pico-PIO-USB
  uses pio0/sm0 and pio1/sm0+sm1). No DMA, no IRQ.

## Verify

1. Flash `remapper.uf2` with no LED lines in the config: the LED pin is
   untouched and the device works as stock.
2. Save a config that writes registers 31 and 32 (e.g. the example): the LED
   follows it. Red and green swapped means the order is wrong; change `OOO`.
3. `ws2812_test.uf2` checks the driver and pin without hid-remapper: the LED
   cycles red, green, blue, white, one second each.
