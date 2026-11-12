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
| `example_scroll_lock_layers.json` | Config-tool import: Scroll Lock cycles layers 0-3, LED white/red/green/blue. |

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
| `WS2812_RGB_ORDER`  | `0`     | `0` = GRB (standard), `1` = RGB. Flip if red/green are swapped. |
| `WS2812_DEFAULT_INTENSITY` | `32` | Intensity 0..255 used when a colour's `II` byte is `00`. |
| `WS2812_COLOR_REGISTER` | `32` | Expression register (1-32) whose value (`0xIIRRGGBB`) sets the LED; `0` disables. |
| `WS2812_COLOR_REGISTER` | `32` | Expression register (1-32) that sets the LED colour; `0` disables. |

CMake variables (set before the include, or with `-D`): `WS2812_BOARDS`
(default `pico`) — boards whose `remapper` target gets the LED; `WS2812_PIN`
(default `16`); `WS2812_RGB_ORDER` (default `1`: the RP2040-Zero clone used
here has an RGB-ordered LED, confirmed on hardware).

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

### Example: Scroll Lock cycles layers 0-3, LED white / red / green / blue

`example_scroll_lock_layers.json` (import it in the web config tool, then
"Save to device"; it **replaces** the current config). Layer switching uses
only standard sticky layer mappings; three expressions set the colour.

Mappings (all **Sticky**):

| Input | Output | Layers |
| --- | --- | --- |
| Scroll Lock | Layer 1 | 0 |
| Scroll Lock | Layer 2 | 1 |
| Scroll Lock | Layer 3 | 2 |

Each press latches the next layer and releases the current one; on layer 3 no
mapping is active, the release rule clears layer 3 and layer 0 is back. To use
another key, change the input of the three mappings.

Expressions (as typed in the config tool):

```
Expression 1: on a layer change, set the layer colour (register 30 = last layer_state)
layer_state 30 recall eq           layer unchanged?
32 recall                          THEN keep the current colour
layer_state 0x02 eq 0x20FF0000     ELSE IF layer 1: red
layer_state 0x04 eq 0x2000FF00          IF layer 2: green
layer_state 0x08 eq 0x200000FF          IF layer 3: blue
0x20FFFFFF                              ELSE (layer 0): white
ifte ifte ifte
ifte
32 store
layer_state 30 store

Expression 2: keys Q W E R T Y U set red orange yellow green blue indigo violet
0x00070014 input_state_binary 0x20FF0000     Q: red
0x0007001a input_state_binary 0x20FF7F00     W: orange
0x00070008 input_state_binary 0x20FFFF00     E: yellow
0x00070015 input_state_binary 0x2000FF00     R: green
0x00070017 input_state_binary 0x200000FF     T: blue
0x0007001c input_state_binary 0x204B0082     Y: indigo
0x00070018 input_state_binary 0x208B00FF     U: violet
32 recall                                    otherwise keep the current colour
ifte ifte ifte ifte ifte ifte ifte
32 store

Expression 3: keys A S D F G H J, same colours at full intensity (0xFF)
0x00070004 input_state_binary 0xFFFF0000     A: red
0x00070016 input_state_binary 0xFFFF7F00     S: orange
0x00070007 input_state_binary 0xFFFFFF00     D: yellow
0x00070009 input_state_binary 0xFF00FF00     F: green
0x0007000a input_state_binary 0xFF0000FF     G: blue
0x0007000b input_state_binary 0xFF4B0082     H: indigo
0x0007000d input_state_binary 0xFF8B00FF     J: violet
32 recall
ifte ifte ifte ifte ifte ifte ifte
32 store
```

Expressions 4-6: **Z** toggles a test animation (any layer), all in config:

```
Expression 4: Z toggles register 29 (animation on/off); saves the LED colour in
register 28 when it starts and restores it when it stops.

Expression 5: precomputes two effects
  rainbow   R,G,B = round((sin(phase + 0/120/240 deg) + 1) * 127.5), 1 cycle/s,
            intensity 0x60                                        -> register 27
  breathing blue, intensity round((sin + 1) * 127 + 1), 2 s/breath -> register 26

Expression 6: while register 29 is on, every 10 s:
  0-2 s  red strobe 10 Hz          (time_sec 0.1 mod 0.05 lt ...)
  2-4 s  green blink 2 Hz, full intensity
  4-6 s  police red/blue 4 Hz
  6-8 s  blue breathing            (register 26)
  8-10 s rainbow                   (register 27)
  -> register 32; when off it writes register 32 back unchanged.
```

Timing uses `time_sec` (milliseconds on the ×1000 scale, wraps after ~24
days); `sin` takes degrees; `round` gives whole numbers, so
`round(x) 0x10000 mul` places a 0-255 value in the red byte (and
`0x1000000 mul` in the intensity byte). Z still types normally.

(The notes are explanation only.) The last event wins: a colour key sets the
LED until the next colour key or layer change; changing layer sets the layer
colour. The colour keys work on every layer and still type normally (map them
to Nothing if you don't want that). `layer_state` is the active-layer bit mask
(layer 0 = 0x01, 1 = 0x02, 2 = 0x04, 3 = 0x08). Colours are `0xIIRRGGBB`.
Expressions 2 and 3 each use 15 of the 16 stack slots, so each holds at most 7
keys; put more keys in another expression.

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
   Red/green swapped → build with `WS2812_RGB_ORDER=1`.
2. Flash `remapper.uf2`: LED off; USB device works as normal. Then save a
   config that writes register 32 (e.g. the example) and the LED follows it.
