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
- Registers keep their values until the device is power-cycled. Removing the
  line from the config does not release the LED until then; store `0` to
  release it straight away.
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

`config-tool-web` accepts colour names in expressions and saves
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
| `ws2812_led.h` | Compile-time settings and `ws2812_led_task()`. |
| `ws2812_led.cc` | Driver: follows registers 31 and 32. |
| `ws2812.cmake` | All build logic; pulled in by one line in `firmware/CMakeLists.txt`. |
| `ws2812_rgb_led.json` | Example config, described below. |

## Build settings

CMake variables (`-D...`, or set before the include):

| Variable | Default | Meaning |
| --- | --- | --- |
| `WS2812_ENABLE` | `ON` | `OFF` leaves the module out of the build. |
| `WS2812_SETTINGS_REGISTER` | `31` | Expression register (1-32) holding the settings. |
| `WS2812_COLOR_REGISTER` | `32` | Expression register (1-32) holding the colour. |

A project that needs registers 31/32 for something else can move them, e.g.
`cmake -DWS2812_SETTINGS_REGISTER=7 -DWS2812_COLOR_REGISTER=8 ..`. Configs must
then use the new numbers.

C defines with defaults in `ws2812_led.h`: `WS2812_DEFAULT_INTENSITY` (`32`)
and `WS2812_NUM_LEDS` (`1`; all LEDs show the same colour).

## Example config

`ws2812_rgb_led.json`: import it in the web config tool, then
"Save to device". It **replaces** the current config. It is written for GPIO16
with RGB order; change the first line of expression 1 for another board.

### What the keys do

All keys still reach the computer, except Scroll Lock.

| Key | Mode | In that mode |
| --- | --- | --- |
| F1 | Colour map, full brightness | Keys 1-9 pick a colour, 0 turns the LED off. |
| F2 | Colour map, low brightness (the power-on mode) | The same, at intensity 0x20. |
| F3 | Hub ports | Shows which hub ports (1-8) have a device, 0.6 s each, in turn. Port 1 is colour 1, port 2 is colour 2, and so on. |
| F4 | Animations | Keys 1-5 start an animation, 0 stops it. |
| F5 | Gamepad | Each button lights its own colour while it is held. With no button held, the sticks set the colour. |
| F6-F12 | Unused | The LED keeps the picked colour; the number keys do nothing. |

- **Brightness.** F1 and F2 also set the brightness used by modes 3 and 5 and
  by F6-F12.
- **Scroll Lock** steps through the 8 layers. Each layer change picks colour
  1-8 (layer 0 = white ... layer 7 = magenta), as a number key would.

| Index | Colour | Index | Colour |
| --- | --- | --- | --- |
| 1 | white `FFFFFF` | 8 | magenta `FF00FF` |
| 2 | red `FF0000` | 9 | pink `FF0040` |
| 3 | orange `FF2800` | 0 / 10 | off |
| 4 | yellow `FF9000` | 11 | lime `80FF00` |
| 5 | green `00FF00` | 12 | teal `00FF60` |
| 6 | cyan `00FFFF` | 13 | purple `8000FF` |
| 7 | blue `0000FF` | 14 | warm white `FFC0A0` |

Keys 1-9 and 0, layers and hub ports use indexes 1-10. Gamepad buttons 1-9 use
indexes 1-9 and buttons 10-13 use 11-14.

| Key in mode 4 | Animation |
| --- | --- |
| 1 | red strobe, 10 Hz |
| 2 | green blink, 2 Hz, full intensity |
| 3 | police red/blue, 4 Hz |
| 4 | blue breathing, 2 s per breath |
| 5 | rainbow, 1 cycle per second |

- An animation keeps its place when another mode is selected, and continues
  when F4 is pressed again.
- **Gamepad.** If several buttons are held, the highest-numbered one wins. The
  D-pad is not used. The stick direction is the hue and the deflection is the
  brightness; both sticks are added together (left = X/Y, right = Z/Rz, as
  hid-remapper normalises gamepads).

### How it is built

**Layers.** Seven sticky mappings, Scroll Lock → Layer k+1 on layer k
(k = 0-6). Each press latches the next layer and releases the current one. On
layer 7 nothing latches, the release clears it, and layer 0 is back.

Every colour source produces a palette **index**, and one expression turns the
chosen index into a colour. The expressions run in order, every millisecond:

| Expression | Elements | Stack | Job |
| --- | --- | --- | --- |
| 1 | 260 | 7 | LED settings; F keys → mode and brightness; number keys; layer change or number key → picked index. |
| 2 | 168 | 13 | Animation state and the animation colour. |
| 3 | 235 | 3 | Hub ports: counts the ports in use, then finds the n-th one for the current 0.6 s slot. Ends with `0 port`. |
| 4 | 201 | 4 | Gamepad: button index, and the stick colour. |
| 5 | 163 | 5 | Picks the index for the mode, looks it up in the palette, adds the brightness, and lets the stick colour or animation override it. Writes register 32. |

| Register | Meaning |
| --- | --- |
| 9, 10, 11 | stick hue angle, Y, X |
| 12, 14, 15 | hub ports: current slot, running count, ports in use |
| 13 | number key pressed while in mode 4 |
| 16 | gamepad button index (0 = none) |
| 17 | F key held (1-12, 0 = none) |
| 18 | number key held (1-9, 10 for the 0 key, 0 = none) |
| 19 | brightness as `0xII000000` |
| 20 | mode (1-12) |
| 21 | index being shown |
| 22 | stick colour |
| 23, 24, 25 | animation: elapsed time, rainbow angle, start time |
| 26 | hub port being shown |
| 27 | animation colour |
| 28 | picked index (number key or layer) |
| 29 | running animation (0 = none) |
| 30 | last `layer_state` |
| 31, 32 | LED settings and colour |

**Limits when editing.** The stack is **16** deep. There is no limit on the
length of one expression, but the whole config must fit in 4096 bytes when
saved: each word costs 1 byte and each number or hex value 5 bytes (this
example's expressions use 2471). Decimal numbers are x1000 internally; hex
values are used as-is. `time_sec` is in milliseconds on that scale and wraps
after about 24 days. `sin` takes degrees. `round` gives whole numbers, so
`round 0x10000 mul` places a 0-255 value in the red byte and
`round 0x1000000 mul` in the intensity byte. `N port` makes the following
inputs and `plugged_in` refer to hub port N until the next `port`; it carries
over into later expressions, so reset it with `0 port`.

**Layers 4-7.** The example needs `NLAYERS = 8` in `firmware/src/remapper.cc`.
With the old value of 4, sticky layer keys leak to the computer on layers 4-7
(and momentary layer mappings to 4-7 flicker unless their target layer is
ticked); in that case add Scroll Lock → Nothing on layer 7.

## Changes to hid-remapper's own files

With `WS2812_ENABLE=OFF`, or without this folder, the build is exactly
upstream.

### 1. `firmware/CMakeLists.txt`: one line

After the `target_link_libraries(remapper ...)` block:

```cmake
include(rp2040-ws2812/ws2812.cmake OPTIONAL)
```

The included file adds the driver source, the generated `ws2812.pio.h`, the
include directory and the `WS2812_ENABLED` define to the `remapper` target.

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

## How it works

- `ws2812_led_task()` runs once per main-loop iteration. It reads the settings
  register and, only when the value has changed, reconfigures. While the LED is
  not in use it returns straight after that comparison.
- Starting: the driver asks the SDK for a free state machine on any PIO block
  with room for the program, remembers the pin's current function, and starts
  the PIO program on the pin. If nothing is free it stays off and tries again
  when the settings value next changes.
- Stopping: it sends "off", waits for the FIFO to empty plus 350 us (the last
  pixel and the latch gap, which is up to 280 us on newer WS2812B parts),
  disables the state machine, gives back the program space and state machine,
  and restores the pin's previous function.
- The registers are hid-remapper's `registers[]` array in `remapper.cc`, which
  the expression engine writes in the same main loop, so no locking is needed.
- `ws2812.pio` is the pico-examples WS2812 program. Each bit takes 10 PIO
  cycles at 800 kHz: a 1 is high for 7 cycles and a 0 is high for 2. Data is
  shifted out MSB first, 24 bits per LED.

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
