# real-team

The real-team additions to [hid-remapper](https://github.com/jfedor2/hid-remapper), in one folder (`firmware/real-team/`). hid-remapper includes one file from here (`real-team.cmake`) and calls three functions (`real_team.h`); everything else is inside this folder, so it can later become a single git submodule.

| File | What it is |
| --- | --- |
| `real-team.cmake` | The one file upstream includes. Build settings, and the sources of the features that are switched on. |
| `real_team.h`, `real_team.cc` | The functions hid-remapper calls (main loop, device reports, USB descriptor). |
| `cmd_table.h`, `cmd_table.c` | The [command table](#command-table): numbers-only config lines, handed to the feature that owns them. Plain C99, reusable in other code bases. |
| `cmd_table_remapper.h`, `cmd_table_remapper.cc` | The hid-remapper side of the table: reads the lines from the config's expressions and passes them on when they changed. |
| `ws2812.h`, `ws2812.cc`, `ws2812.pio` | [RGB LED](#rgb-led-ws2812): WS2812, WS2812B, SK6812 RGB, NeoPixel, driven by registers 31 (settings) and 32 (colour). |
| `hidout.h`, `hidout.cc` | [HID output reports](#hid-output-reports-hidout) to devices plugged into the remapper (a command-table user). |
| `vdisk/` | [USB configuration drive](#configuration-drive-vdisk): `config.json`, apply by copying a `.json`, `config.html`, `readme.txt`, `autorun.inf`. |
| `example_ws2812.json`, `example_hidout.json` | The RGB LED demo and the Logitech K400 Fn-lock example (both also in the drive's `config.html` Examples). |
| `message.txt` | Commit-message scratch file. |

## Build settings

CMake variables (`-D...`):

| Setting | Default | Meaning |
| --- | --- | --- |
| `WS2812_ENABLE` | `ON` | RGB LED. More settings under "RGB LED (ws2812)", "Build settings". |
| `HIDOUT_ENABLE` | `ON` | HID output reports. `HIDOUT_REGISTER` (default `1`) is the selector register. |
| `VDISK_ENABLE` | `ON` | Configuration drive. `VDISK_WEB_TOOL` (default `ON`), `VDISK_DEBUG` (default `OFF`). |

With all three off, or without this folder, the firmware is upstream's (plus the fixes listed under "Changes to hid-remapper's own files"). The drive's `config.html` only lists an example when its feature is in the build. The command table is built when a feature that uses it is on (currently hidout).

## Command table

Config lines made of **numbers only** are table lines. They're ordinary hid-remapper expression text, so configs that use them still load on upstream firmware and in upstream's web tool, which simply push the numbers. The firmware reads them and hands each line to the feature that owns it.

- **A line** is an expression line (ended by a line break) holding only numbers, in any of the 8 expressions. Comment-only lines and lines with any operation (`store`, `recall`, …) are not table lines. Comments are fine but aren't stored on the device.
- **Values:** a plain decimal is fixed point ×1000 (`7` = 7000, `0.25` = 250), as everywhere in hid-remapper; a `0x…` value is a raw 32-bit word.
- **Owner by the first value:**

| First value | Owner | Example |
| --- | --- | --- |
| decimal ≥ 2 | hidout send line | `7 0x10010F1A 0x00000000 0x046DC52B` |
| `0` | hidout watch line | `0 3 0x10014100 0x046DC52B` |
| hex | reserved for other features: a tag word, e.g. `0x4C4541nn` | (planned LED lines) |

  A line no feature claims is ignored.
- **Stack:** hid-remapper also evaluates the lines, pushing their values; keep each expression within the 16-deep stack, or hid-remapper skips that expression (the firmware still reads the table).
- **When it is read:** 4 times a second, and right away when hidout's selector changes; features get the new table only when it changed.

### Using `cmd_table` in another code base

`cmd_table.h/.c` only need a C99 compiler. Register one handler per feature, then feed the lines from wherever your config lives:

```c
#include "cmd_table.h"

static void my_begin(void* ctx) { /* a new table follows: forget the old lines */ }
static bool my_line(const cmd_value_t* vals, size_t n, void* ctx) {
    if (!vals[0].hex || (((uint32_t) vals[0].v >> 8) != 0x4C4541)) {
        return false;                 /* not mine: the next handler gets it */
    }
    /* vals[1..n-1]: .v is the value (decimal x1000 or raw hex), .hex says which */
    return true;                      /* claimed */
}

cmd_table_add_handler(my_begin, my_line, NULL);   /* up to CMD_TABLE_MAX_HANDLERS (8) */

/* whenever the config changes: */
uint32_t h = CMD_TABLE_HASH_INIT;     /* optional: hash the lines to skip unchanged tables */
cmd_table_begin();
cmd_table_line(line_values, line_count);          /* once per line, in order */
```

`cmd_table_remapper.cc` (`cmd_table_remapper_read()`) is the hid-remapper side: it walks the expressions, turns `PUSH` into decimal values and `PUSH_USAGE` into hex values, and calls the table when the hash changes.

## RGB LED (ws2812)

RGB LED support for the
[hid-remapper](https://github.com/jfedor2/hid-remapper) RP2040/RP2350 firmware,
driven entirely by the hid-remapper config. It drives single-wire addressable
LEDs: WS2812, WS2812B, SK6812 (RGB) and Adafruit NeoPixels.

- **Any board.** The module is built into the `remapper` target on every board.
  The config can name any GPIO, or ask for the board's own LED.
- **Boot colour.** On a board whose header names its LED pin
  (`PICO_DEFAULT_WS2812_PIN`: `waveshare_rp2040_zero`, `feather_host`,
  `waveshare_rp2350_usb_a`), the board LED shows `0x200000FF` (dim blue) from
  power-on until the config writes the settings or colour register; then the
  config takes over. On other boards no LED code runs and no pin is touched
  until the config writes the settings register. `WS2812_BOOT_SETTINGS` and
  `WS2812_BOOT_COLOR` change this (see "Build settings").
- **Nothing hardcoded.** The firmware has no colours, animations or layer
  logic. The config says which LED to use and what colour to show, using
  two expression registers.

### Quick start

In any expression (web config tool, "Expressions" tab):

```
0x00012310 31 store      /* LED on GPIO16, RGB order */
0x20FF0000 32 store      /* red at intensity 0x20 */
```

Save to device and the LED turns red. Developed on a Waveshare RP2040-Zero
clone (WS2812 on GPIO16, RGB order) running the `pico` build.

On a board whose definition names its LED, `0x000000FF 31 store` uses that LED
without knowing the pin (see "Boards" below).

### Boards

| hid-remapper build (`PICO_BOARD`) | LED | Settings to use |
| --- | --- | --- |
| `waveshare_rp2040_zero` (Waveshare RP2040-Zero, board header added by this fork) | WS2812 on GPIO16 | `0x000000FF` (GRB). Some clones are RGB: `0x000123FF`. Shows the boot colour. |
| `pico` on a Waveshare RP2040-Zero (or clone) | WS2812 on GPIO16 | `0x00000010` (GRB). Some clones are RGB: `0x00012310`. No boot colour (the `pico` header names no LED); use `waveshare_rp2040_zero` instead. |
| `pico2` on a Waveshare RP2350-Zero | WS2812 on GPIO16 | `0x00000010`; try `0x00012310` if red and green are swapped. |
| `feather_host` (Adafruit Feather RP2040 with USB Type A Host) | NeoPixel on GPIO21 | `0x000000FF`. If it stays dark, the NeoPixel may need its power pin switched on: `0x940000FF` (GPIO20). |
| `waveshare_rp2350_usb_a` (Waveshare RP2350-USB-A) | WS2812 on GPIO16 | `0x000000FF`; `0x000123FF` if red and green are swapped. |
| Other builds (`remapper`, `remapper_v7`, `remapper_v8`, `flatbox_rev4/8`, `meisterconverter`, `rp2040abb`, `waveshare_rp2040_pizero`, `waveshare_rp2350_pizero`) | No addressable LED on the board | An external LED strip or module on any free GPIO works, e.g. `0x00000005` for GPIO5. |

- `FF` takes the pin from `PICO_DEFAULT_WS2812_PIN` in the board definition
  (`firmware/src/boards/*.h` or the Pico SDK's). Boards without it ignore `FF`.
- None of these other boards has been tested. Only the RP2040-Zero clone has.
  The colour order and the Feather's power pin are from the boards'
  documentation, not tested.
- Plain (non-addressable) RGB LEDs, driven by three PWM pins, and RGBW LEDs
  (SK6812 RGBW, 4 bytes per pixel) are not supported.

### The two registers

#### Register 31: settings, `0xWWLOOOPP`

| Field | Meaning |
| --- | --- |
| `PP` | Data GPIO in hex (`10` = GPIO16). `FF` = the board's own LED (see "Boards"). Required. |
| `OOO` | Colour order: the channel sent first, second, third (1 = R, 2 = G, 3 = B). `213` = GRB (standard WS2812), `123` = RGB, or any other permutation. `000` = GRB. |
| `L` | Master brightness, `1` (dimmest) to `F` (full); `0` = full. |
| `WW` | LED power pin: `00` = none. `80` + the GPIO number = that GPIO is driven high while the LED is in use, for boards that switch the LED's power (`94` = GPIO20). With `PP` = `FF` and `WW` = `00`, the board's `PICO_DEFAULT_WS2812_POWER_PIN` is used if it has one. |

- `0` means **no LED**: nothing is claimed or touched (before the config first
  writes register 31 or 32, the boot settings are used instead).
- An invalid value also means no LED: a pin that doesn't exist, one of the
  PIO-USB host port pins (or its VBUS-enable pin), `FF` on a board without an
  LED of its own, a power pin equal to the data pin, `WW` without bit 7 set, or
  an order that isn't a permutation of 1, 2, 3.
- Examples: `0x00012310` = GPIO16, RGB, full brightness. `0x00812310` = the same
  at level 8. `0x00000005` = GPIO5, GRB, full. `0x948000FF` = the board's LED,
  GRB, level 8, power on GPIO20.
- Writing it every tick is fine. The driver only acts when the value changes.
- Registers keep their values until the device is power-cycled. Removing the
  line from the config does not release the LED until then; store `0` to
  release it straight away.
- When the value becomes valid the driver claims a free PIO state machine and
  the pin (and drives the power pin high). When a pin changes it moves. When
  the value goes back to 0 it turns the LED off, releases the state machine and
  program space, and restores both pins to the function they had before.
- The master level multiplies every colour's intensity, so one value dims
  everything. The 15 steps rise about 1.35x each (2, 4, 6, 9, 13, 18, 25, 34,
  46, 62, 84, 113, 152, 205, 255 out of 255). The result is rounded up, so a lit
  colour never goes fully dark.
- Don't use the LED pin or its power pin in GPIO mappings. hid-remapper still
  treats them as remappable GPIOs (it may set a pull-up on them and read them
  as inputs). That doesn't disturb the LED, but a mapping that reads or drives
  the same pin would misbehave.

#### Register 32: colour, `0xIIRRGGBB`

- `II` = intensity, `01`-`FF`. `00` = the default (`WS2812_DEFAULT_INTENSITY`,
  32), so plain `0xRRGGBB` also works.
- `RR GG BB` = the colour. `0` = off.
- The LED is updated only when the value changes.
- Intensity scales R, G and B before they are sent. RGBW (SK6812) LEDs are not
  supported.

#### Colour names in the web config tool

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

### Files

| File | Purpose |
| --- | --- |
| `ws2812.pio` | PIO program, converted line by line from the MicroPython `@rp2.asm_pio` sample, plus `ws2812_program_init()`. |
| `ws2812.h` | Compile-time settings and `ws2812_led_task()`. |
| `ws2812.cc` | Driver: follows registers 31 and 32. |
| `example_ws2812.json` | Example config, described below. |

### Build settings

CMake variables (`-D...`, or set before the include):

| Variable | Default | Meaning |
| --- | --- | --- |
| `WS2812_ENABLE` | `ON` | `OFF` leaves the module out of the build. |
| `WS2812_SETTINGS_REGISTER` | `31` | Expression register (1-32) holding the settings. |
| `WS2812_COLOR_REGISTER` | `32` | Expression register (1-32) holding the colour. |
| `WS2812_BOOT_SETTINGS` | `0x000000FF` if the board header has `PICO_DEFAULT_WS2812_PIN`, else `0` | Settings used from power-on until the config writes register 31 or 32. `0` = LED off until then. |
| `WS2812_BOOT_COLOR` | `0x200000FF` | Colour shown with the boot settings (dim blue; blue is the last byte in both GRB and RGB order). |

A project that needs registers 31/32 for something else can move them, e.g.
`cmake -DWS2812_SETTINGS_REGISTER=7 -DWS2812_COLOR_REGISTER=8 ..`. Configs must
then use the new numbers.

C defines with defaults in `ws2812.h`: `WS2812_DEFAULT_INTENSITY` (`32`)
and `WS2812_NUM_LEDS` (`1`; all LEDs show the same colour).

### Example config

`example_ws2812.json`: import it in the web config tool (the copy on the config drive, `config.html`, lists it first under Examples), then
"Save to device". It **replaces** the current config. It is written for GPIO16
with RGB order; change the first line of expression 1 for another board.

#### Power-on colour

The second line of expression 1 sets the colour shown at power-on and how long
it stays:

```
0x200000FF 8 store 2 7 store
```

- `0x200000FF` (register 8) is the colour, in the usual `0xIIRRGGBB` form
  (here blue at intensity 0x20, the same as the firmware's boot colour). Colour
  names work too, e.g. `blue:20`.
- `2` (register 7) is the time in seconds. After it, the LED shows the normal
  colour for the current mode (white at power-on).
- It appears about 1 ms after the firmware starts, whether or not a computer is
  connected. Before that, the LED shows whatever it latched at power-up.
- `0 7 store` turns it off. A very large time keeps the power-on colour until
  the device is unplugged.

#### What the keys do

All keys still reach the computer, except Scroll Lock (including Ctrl+Alt+F1-F8).

Hold **Ctrl and Alt** (either side) and press F1-F8 to pick the mode:

| Ctrl+Alt+ | Mode | In that mode |
| --- | --- | --- |
| F1 | Colour map, full brightness | Keys 1-9 pick a colour, 0 turns the LED off. |
| F2 | Colour map, low brightness (the power-on mode) | The same, at intensity 0x20. |
| F3 | Hub ports | Shows which hub ports (1-8) have a device, 0.6 s each, in turn. Port 1 is colour 1, port 2 is colour 2, and so on. |
| F4 | Animations | Keys 1-5 start an animation, 0 stops it. |
| F5 | Gamepad | Each button lights its own colour while it is held. With no button held, the sticks set the colour. |
| F6-F8 | Unused | The LED keeps the picked colour; the number keys do nothing. |

- Plain F1-F8 do not change the mode.
- Ctrl+Alt+F1-F8 also reach the computer. Blocking them would need one
  register and two mappings per key, so the example does not.
- **Brightness.** F1 and F2 also set the brightness used by modes 3 and 5 and
  by F6-F8.
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

#### How it is built

**Layers.** Seven sticky mappings, Scroll Lock → Layer k+1 on layer k
(k = 0-6). Each press latches the next layer and releases the current one. On
layer 7 nothing latches, the release clears it, and layer 0 is back.

Every colour source produces a palette **index**, and one expression turns the
chosen index into a colour. The expressions run in order, every millisecond:

| Expression | Elements | Stack | Job |
| --- | --- | --- | --- |
| 1 | 262 | 7 | LED settings; power-on colour; Ctrl+Alt+F keys → mode and brightness; number keys; layer change or number key → picked index. |
| 2 | 168 | 13 | Animation state and the animation colour. |
| 3 | 235 | 3 | Hub ports: counts the ports in use, then finds the n-th one for the current 0.6 s slot. Ends with `0 port`. |
| 4 | 201 | 4 | Gamepad: button index, and the stick colour. |
| 5 | 174 | 5 | Picks the index for the mode, looks it up in the palette, adds the brightness, and lets the stick colour, animation or power-on colour override it. Writes register 32. |

| Register | Meaning |
| --- | --- |
| 7, 8 | power-on colour: seconds shown, colour |
| 9, 10, 11 | scratch values, reused by every expression (F key and Ctrl+Alt state; animation time and angle; hub port counters; stick X, Y, hue; index being shown) |
| 16 | gamepad button index (0 = none) |
| 18 | number key held (1-9, 10 for the 0 key, 0 = none) |
| 19 | brightness as `0xII000000` |
| 20 | mode (1-8) |
| 22 | stick colour |
| 25 | animation start time |
| 26 | hub port being shown |
| 27 | animation colour |
| 28 | picked index (number key or layer) |
| 29 | running animation (0 = none) |
| 30 | last `layer_state` |
| 31, 32 | LED settings and colour |

**Limits when editing.** The stack is **16** deep. There is no limit on the
length of one expression, but the whole config must fit in 4096 bytes when
saved: each word costs 1 byte and each number or hex value 5 bytes (this
example's expressions use 2669; the whole example is about 3 KB). Decimal numbers are x1000 internally; hex
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

### How it works

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

### Resource use

- Idle (no LED in use): two register reads and a compare per main-loop iteration.
- Active: one PIO state machine and 4 instruction slots on whichever PIO block
  has room (`pio_claim_free_sm_and_add_program_for_gpio_range()`; Pico-PIO-USB
  uses pio0/sm0 and pio1/sm0+sm1). No DMA, no IRQ.

### Verify

1. Flash `remapper.uf2` with no LED lines in the config: the LED pin is
   untouched and the device works as stock.
2. Save a config that writes registers 31 and 32 (e.g. the example): the LED
   follows it. Red and green swapped means the order is wrong; change `OOO`.

## HID output reports (hidout)

Lets a hid-remapper config **send HID output reports to the devices plugged into the remapper** (out of its USB host port, not to the computer), for example a vendor command that the device's own software would normally send. The config holds a table of messages, each with its own target devices, and an expression picks which ones are sent. The firmware has nothing device-specific in it.

- **Dormant by default.** While the selector register is 0, nothing is sent. That is always the case on upstream firmware, which ignores the register and never runs the table as anything but a list of numbers, so configs that use it still load there.
- **Optional.** `-DHIDOUT_ENABLE=OFF` leaves it out of the build.

### The table

hidout's messages are lines of the [command table](#command-table) whose first value is a decimal. They can sit next to the logic in the same expression. There are two kinds: **send lines** (a message to send) and **watch lines** (a report to watch for). A send line:

```
<length> <report words...> <targets...>
```

| Part | Meaning |
| --- | --- |
| `<length>` | Length of the report in bytes, **including the report ID**, as a plain decimal (e.g. `7` for a HID++ short report, `20` for a long one). At most 64. |
| `<report words>` | The report as 32-bit hex words, first byte first, **report ID first**: `10 01 0F 1A 00 00 00` is written `0x10010F1A 0x00000000`. As many words as the length needs (`7` → 2 words); unused bytes of the last word are ignored. |
| `<targets>` | Any number of devices, any mix of: `0xVVVV` = every product of vendor VVVV; `0xVVVVPPPP` = exactly that VID:PID. **No targets = any device** that declares an output report with that report ID. |

- **Line numbers** count send lines only, from 0, starting in expression 1 and continuing through expression 8. Watch lines have no number. Comment-only lines and lines with any operation (`store`, `recall`, ...) don't count, so each message can have a comment line above it, and a line commented out with `/* */` is not a table line.
- **Comments** are not stored on the device, so a config read back from the device shows the table without them.
- **Malformed lines** (too few words for the length, a length below 2, or report ID 0) keep their line number but are never sent.
- **Evaluation:** hid-remapper also evaluates the table lines as part of their expression, so each line's values are pushed and stay on the stack. Keep the expression within the 16-deep stack (a 7-byte line with one target pushes 4 values). If it overflows, hid-remapper skips the whole expression, including any logic in it; `hidout` still reads the table directly.

### Watch lines

```
0 <pattern length> <pattern words...> <targets...>
```

A leading `0` marks a watch line. When an input report from a target device **starts with the pattern** (report ID first, packed into 32-bit words like a send line, at most 16 bytes), every selected send line is sent again right away. Use it for devices that forget a setting when they reconnect: watch for the device's "connected" message instead of resending on a timer.

- **Targets** work as for send lines; no targets = any device.
- **No register** is used, and it does nothing while the selector is 0.
- **Malformed watch lines** (pattern length 0 or over 16, too few words) are ignored.
- Watch lines are checked against every input report the remapper receives, so keep the pattern specific (report ID and the first bytes that identify the message).

### The selector register

`HIDOUT_REGISTER` (default **register 1**) selects the lines: **bit n = line n** (`0x1` = line 0, `0x2` = line 1, `0x4` = line 2, …), up to 32 lines. Several bits may be set at once.

- **A line is sent** to each matching device when its bit turns on.
- **It is sent again** when a matching device appears later (e.g. the receiver is plugged back in; checked 4 times a second), and when the table is edited.
- **It is not sent on every tick**, so writing the register every tick is fine.
- **Forcing a resend:** a watch line (above), or writing 0 for one tick and then the bits again.
- **Length sent:** the report goes out with the length the device declares for that report ID (padded with zeros, or cut at 63 bytes after the ID). If the device doesn't declare it, the length from the table is used.
- **Report types:** only output reports (HID SET_REPORT, type Output); feature reports are not sent.
- **Not supported:** reading replies. If a command needs a value the device reports first, this module can't do it.

### Example: Logitech K400 family, Fn lock on Fn+Esc

`example_hidout.json`. It is also the second entry under Examples in the config drive's web tool.

The K400's Fn lock (F1-F12 or media keys first) is normally switched by Logitech software. It is a HID++ 2.0 command to the keyboard through its Unifying receiver (`046D:C52B`), feature FN_INVERSION (`0x40A0`). The feature's index differs per model:

| Model | Feature index | F-keys first | Media keys first | Source |
| --- | --- | --- | --- | --- |
| K400r (and K400) | `0F` | `10 01 0F 1A 00 00 00` | `10 01 0F 1A 01 00 00` | Read from a K400r (wireless PID `4024`) and confirmed on it, 2026-11-13. Then confirmed through the remapper with this example: the receiver answers `11 01 0F 1A 00 01 ...` / `... 01 01 ...`. While the keyboard is off the receiver answers `10 01 8F 0F 1A 09 00` (HID++ error 09). The original K400 shares that wireless PID family; assumed the same, not tested. |
| K400 Plus | `09` | `10 01 09 19 00 00 00` | `10 01 09 18 01 00 00` | From github.com/h8man/k400p-fn-lock-win and github.com/sginne/fn_key_k400_for_logitech; not tested here. |

`01` is the keyboard's slot on its receiver. The receiver is the same (`C52B`) for all models, so the firmware can't tell which model is behind it. The config says so.

Expression 1, the logic (registers: 1 = selector, 6 = Fn lock state 1 or 2):
```
/* Fn lock at boot: 0x1 = F-keys first, 0x2 = media keys first (register 6 starts at 0, so this only runs once) */
6 recall 0 eq 0x1 6 recall ifte 6 store
/* Fn+Esc (reported as Power) toggles it: on the press edge, 6 = 3 - 6, so 1 <-> 2 */
0x00070066 input_state_binary 0x00070066 prev_input_state_binary not mul
0x3 6 recall sub 6 recall ifte 6 store
/* selector: line 0 (0x1) or line 1 (0x2) for the state, plus line 2 (0x4) to turn on the receiver notifications */
6 recall 0x1 eq 0x1 0x2 ifte 0x4 bitwise_or 1 store
```

Expression 2, the table:
```
/* line 0: K400 / K400r F-keys first: 10 = HID++ short report, 01 = keyboard slot, 0F = FN_INVERSION index, 1A = function 1 (set) + software ID A, 00 = F-keys first */
7 0x10010F1A 0x00000000 0x046DC52B
/* line 1: K400 / K400r media keys first (01) */
7 0x10010F1A 0x01000000 0x046DC52B
/* line 2: receiver (FF), HID++ 1.0 set register 0x00 (80 00) to 00 01 00 = wireless notifications on */
7 0x10FF8000 0x00010000 0x046DC52B
/* watch line: 10 01 41 = the device in slot 1 connected, so send the selected lines again */
0 3 0x10014100 0x046DC52B
/* K400 Plus (index 09, not tested): use these two lines instead of lines 0 and 1, without the comment marks */
/* 7 0x10010919 0x00000000 0x046DC52B */
/* 7 0x10010918 0x01000000 0x046DC52B */
```

- **Model:** keep the two lines for your keyboard and leave the other model's lines commented out. Comments are not stored on the device, so the commented lines are gone when the config is read back.
- **Why two expressions:** the table lines push 16 values (4 per line), the whole 16-deep stack, so the logic needs its own expression.
- **Map Power (`0x00070066`) → Nothing** on every layer (the example does). On the K400 family Fn+Esc reaches the host as Power, and the computer would otherwise sleep or shut down with every press.
- **Resend on reconnect:** the keyboard forgets the setting when it is switched off. Line 2 turns on the receiver's wireless notifications (HID++ 1.0 register `0x00`, the same thing the Linux `hid-logitech-dj` driver does), so the receiver reports `10 01 41 ...` when the keyboard connects. The watch line catches that and the Fn lock is sent again within a few milliseconds (confirmed on a K400r, 2026-11-13: switched off and on, Fn lock was set again automatically). A disconnect message (same first bytes) also triggers a resend; that one just gets an error reply. No messages are sent while nothing changes.
- **Raw values:** use hex (`0x1`) for raw values; plain decimals are stored ×1000. The table's length is the one decimal: the firmware divides it by 1000.
- **Other slots or models:** if the keyboard is in another receiver slot, change the second byte (`01`). For another model, the index and slot can be read from the keyboard with a HID++ root lookup (`10 <slot> 00 0A 40 A0 00` → reply byte 4 is the index).
- **Two receivers of the same type** (e.g. a second Unifying receiver with mice) both match `0x046DC52B`. The command then also reaches slot 1 of the other receiver, which would run function 1 of whatever feature that device has at the same index. Plug only the keyboard's receiver into the remapper, or make sure slot 1 of the other receiver is not affected.

### Debugging

- **`DEBUG.TXT`** on the config drive (debug builds of the `vdisk` module, `-DVDISK_DEBUG=ON`): the table (line count, malformed lines), every change of the selector, the connected interfaces with their VID:PID and output report IDs, and per line how many interfaces have the report, how many match, and where it was sent. Repeated resends with the same result are not logged again. For 2 s after each send, up to 8 input reports from the interface it was sent to are logged too (`reply from interface ...`): a HID++ device answers with its result or an error there.
- **Monitor tab** of the web tool (while monitoring is on): `0xFFFE00nn` = line `nn` sent (value: interface), `0xFFFE01nn` = interfaces with its report ID, `0xFFFE02nn` = interfaces that also match its targets.

### Build

| CMake setting | Default | Meaning |
| --- | --- | --- |
| `HIDOUT_ENABLE` | `ON` | Build the module into `remapper`. |
| `HIDOUT_REGISTER` | `1` | The selector register (1-32). |

### How it works

- `hidout_task()` reads the selector (`extern int32_t registers[]`, `remapper.cc`).
- When it changes, or every 250 ms while it is not 0, `hidout_task()` does the following:
  - It uses the lines it claimed from the command table (`table_line()`: first value decimal; that value, and a watch line's pattern length, divided by 1000). A new table (only passed on when it changed) counts as an edit, so selected lines are sent again.
  - For each selected line, it walks `their_out_usages` (all connected interfaces' output reports) under the `THEIR_USAGES` mutex.
  - It checks targets with TinyUSB's `tuh_vid_pid_get()`.
  - It calls `queue_out_report()` for each matching interface the line has not been sent to yet.
- Per line it remembers which interfaces got it; interfaces that disappear drop out of that list, so they get it again when they return.
- A device that rejects a report does not block hid-remapper's output queue (TinyUSB still reports completion). The queue holds 8 reports, so with more than about 8 sends at once the rest are skipped until the next change.

## Configuration drive (vdisk)

Adds a small USB flash drive to HID Remapper (the `remapper` target, RP2040 and RP2350). The drive is not stored anywhere: every sector is generated when the host reads it.

| On the drive | What it is |
| --- | --- |
| `config.json` | The current configuration, in the same JSON format as the web config tool. It is generated from the live configuration when it is read. Copy it off to make a backup. |
| `config.html` | The web config tool as one self-contained file. Open it in Chrome or Edge; it needs no internet connection. Its Examples list starts with the RGB LED demo (`firmware/real-team/example_ws2812.json`) and the Logitech K400 Fn lock example (`firmware/real-team/example_hidout.json`), each only when its module (`ws2812`, `hidout`) is in the build. |
| `DEBUG.TXT` | Debug builds only (`-DVDISK_DEBUG=ON`). Debug log, the last 4 KB (e.g. from the `hidout` module). Always exactly 4096 bytes (blank-padded), so the drive never remounts for it; read it uncached (e.g. `FILE_FLAG_NO_BUFFERING`) to see new lines, because a normal read may return Windows' cached copy. |
| `autorun.inf` | Hidden, system, read-only. Only `label=HID-REMAPPER`, so Windows shows the drive as HID-REMAPPER (the FAT volume label, `HIDREMAPPER`, is limited to 11 characters). It starts nothing. |
| `readme.txt` | A short description of the above. |

### Using it

- **Back up:** copy `config.json` from the drive.
- **Restore or load:** copy a `.json` file onto the drive, under any name or over `config.json`. About 1.5 seconds after the copy finishes, the drive disappears for 2.5 seconds and comes back. If the file was a valid configuration it is loaded and saved to flash, as "Save to device" does in the web tool; otherwise the configuration is left as it was. Either way the copied file is gone and `config.json` shows the configuration that is now running, so compare it to see whether it was taken. On a debug build `DEBUG.TXT` also says `configuration loaded and saved` or `configuration not loaded: <reason>`.
- The drive also reloads when the configuration is saved from the web tool, so `config.json` is never stale.
- A change of the emulated device type takes effect after the device is replugged, as with the web tool.

### Limits

- Only files of the current config version (18) are accepted. Older files must be opened and saved in the web tool first.
- Comments in expressions are not stored on the device, so `config.json` has none.
- A file is only applied if it is complete, valid and small enough to be saved. Otherwise the running configuration is left untouched.
- Nothing else written to the drive is kept. Only files whose name ends in `.json` are applied.
- The file's data must be written in ascending order in one piece. Windows, Linux and macOS do this for a normal copy.
- The drive is only added for the two default device types (mouse and keyboard, absolute mouse and keyboard). The gamepad types (Switch, PS4, Stadia, XAC/Flex) keep their exact USB descriptors.
- `config.html` is built from `config-tool-web` when the firmware is built, so it always matches the firmware. `config-tool-web` itself is not changed: the RGB LED demo is only added to the drive's copy. The drive's copy is also patched to show layers 4-7 (upstream's `index.html` hides them with `d-none` in the mapping rows and in "Unmapped inputs passthrough"; this fork's firmware has 8 layers), in the stock column layout: the 8 layer boxes are two rows of 4 the height of the Input/Output buttons, with the layer number inside each box (bold; white on blue when checked) instead of below it, added as a small <style> block. Sticky/Tap/Hold are kept on one line (`flex-nowrap`). `highlight.js` (in this folder) is added to the page and colours the expression boxes like an IDE: comments, register numbers (a number before `store`/`recall`), values, hex, `store`/`recall`, logic, maths, inputs, stack ops, debug ops, LED colour names and unknown words, with the box growing to fit its text. Each patch is a regex in `webtool.cmake` that must match exactly (8 checkboxes, otherwise one place), or the build stops.
- The web tool adds about 106 KB to the firmware image. The file is a small loader holding the gzipped page; the browser unpacks it when it is opened (Chrome or Edge 80+, which WebHID needs anyway).

### Build

`vdisk/vdisk.cmake` (included by `real-team.cmake`) adds the drive to the `remapper` target on every board. Turn it off with `-DVDISK_ENABLE=OFF`.

| CMake setting | Default | Meaning |
| --- | --- | --- |
| `VDISK_ENABLE` | `ON` | Build the drive into `remapper`. |
| `VDISK_WEB_TOOL` | `ON` | Put the web config tool on the drive. Needs internet access at configure time (to download Bootstrap). |
| `VDISK_DEBUG` | `OFF` | Add `DEBUG.TXT` and `vdisk_log()`. Without it the log code is not built and other modules' log calls do nothing. |

`VDISK_EP_OUT` / `VDISK_EP_IN` (`0x04` / `0x84`) in `vdisk.h` are the bulk endpoints.

The build stops with an error if `src/config.cc` has a different `CONFIG_VERSION` than `VDISK_CONFIG_VERSION` in `vdisk.h`, or if the `Op` enum in `src/types.h` has a different number of ops than `op_names` in `vdisk.cc`. Either means upstream changed the config format, and `vdisk.cc` must be updated before the drive can read or write it.

### How it works

- **USB:** `vdisk_configuration_descriptor()` copies the active configuration descriptor and appends one mass-storage interface. TinyUSB's MSC class calls the `tud_msc_*_cb()` functions in `vdisk.cc`.
- **Volume:** FAT12, 8192 sectors of 512 bytes (4 MB), 4 KB clusters, two FATs, 64 root entries. Cluster 2 is `readme.txt`, 3 is `autorun.inf`, 4 is `DEBUG.TXT` on debug builds, then the web tool, then `config.json`.
- **Reading `config.json`:** `generate_json()` writes the JSON through a 512-byte window, so a sector is produced without holding the whole file in RAM. Its length is measured once per mount.
- **Writing:**
  - Sectors written to the root directory are scanned for file entries with the extension `JSO` (the short name of any `.json` file); the first cluster and size of each are remembered. The drive's own unchanged `config.json` entry is skipped.
  - A data sector that starts a cluster with `{` and a quoted key starts the JSON parser. It is fed the following sectors as they arrive.
  - The parser is a character-by-character state machine. It builds the configuration in the firmware's own persisted format, in a few small vectors.
  - When the parsed file's first cluster matches such an entry, `vdisk_task()` assembles the 4 KB image, adds the CRC and passes it to `load_config()`, then requests a save to flash.
  - All other writes are accepted and dropped.
- **Web tool:** built with CMake only.
  1. At configure time `vdisk.cmake` downloads Bootstrap 5.1.3 (CSS and JS) from the same CDN URLs as `index.html` and checks them against the same SHA-384 hashes. It stops if `index.html` refers to other files.
  2. `webtool.cmake` joins `crc.js`, `usages.js`, `examples.js` and `code.js` with their `import`/`export` lines removed, adds the example configs of the modules in the build as the first examples (`vdisk.cmake` passes each one only if `WS2812_ENABLED` / `HIDOUT_ENABLED` is among the `remapper` target's compile definitions, checked with a generator expression after all modules are configured). It then strips the indentation from `index.html` and puts Bootstrap and the minified script into it inline (about 740 KB).
  3. It strips indentation and blank lines from the joined scripts, gzips the page with CMake's own `file(ARCHIVE_CREATE ... FORMAT raw COMPRESSION GZip)`, encodes the result as base64 (in CMake, from the hex it reads), and writes `config.html` as a short loader that decodes it with the browser's own base64 decoder and `DecompressionStream` and replaces itself with the page. About 106 KB (the page itself is about 540 KB). With CMake older than 3.19 the page is written unpacked.
  `vdisk.cmake` links the file into the `.flashdata` section, so it stays in flash and is read from there; `config.json` follows it on the volume.
- The scripts are not minified: gzip removes almost the same redundancy (minifying first would save about 10 KB of the final file).
- **Reload:** after a change, test-unit-ready reports "medium not present" for 2.5 seconds, which makes the host mount the drive again.

## Changes to hid-remapper's own files

All module hooks are gated by `REAL_TEAM_ENABLED`, so without this folder they compile to nothing.

### `firmware/CMakeLists.txt`

After the `target_link_libraries(remapper ...)` block:

```cmake
include(real-team/real-team.cmake OPTIONAL)
```

### `firmware/src/main.cc`

```c
#include "tick.h"
#ifdef REAL_TEAM_ENABLED
#include "real_team.h"
#endif
...
        activity_led_off_maybe();
#ifdef REAL_TEAM_ENABLED
        real_team_task();
#endif
```

`real_team_task()` runs `vdisk_task()`, `ws2812_led_task()` and `hidout_task()` once per main-loop iteration.

### `firmware/src/remapper_single.cc`

```c
#ifdef REAL_TEAM_ENABLED
#include "real_team.h"
#endif
...
void report_received_callback(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
    if (len > 0) {
#ifdef REAL_TEAM_ENABLED
        real_team_report_received(dev_addr, instance, report, len);
#endif
```

Passes every input report to `hidout` (watch lines, reply logging). It doesn't change the report.

### `firmware/src/tinyusb_stuff.cc`

```c
#ifdef REAL_TEAM_ENABLED
#include "real_team.h"
#endif
...
uint8_t const* tud_descriptor_configuration_cb(uint8_t index) {
#ifdef REAL_TEAM_ENABLED
    return real_team_configuration_descriptor(configuration_descriptors[our_descriptor->idx]);
#endif
```

`vdisk` appends its mass-storage interface (default device types only); without `vdisk` the descriptor is returned unchanged.

### `firmware/src/tusb_config_both/tusb_config.h`

`#ifndef CFG_TUD_MSC` around `#define CFG_TUD_MSC 0`, so `vdisk.cmake` can set it to 1. Also the hub fix below.

### `config-tool-web/code.js`

LED colour names in expressions (`cyan` → `0x0000ffff`, `cyan:40` → `0x4000ffff`): a `led_colors` table, `led_color_to_hex()` and four lines in `expression_onchange()`. Configs still store hex.

### `firmware/src/boards/waveshare_rp2040_zero.h` (new)

Board header for the Waveshare RP2040-Zero, replacing the Pico SDK's header of the same name (`src/boards` is searched first). It adds what hid-remapper needs: USB host on GP0/GP1 (`PICO_DEFAULT_PIO_USB_DP_PIN 0`), debug UART0 on GP12/GP13 (Waveshare's alternate UART0 pins; the default GP0/GP1 is the USB host), the WS2812 on GP16, and the mappable GPIOs GP2-11, GP14, GP15, GP17-29. Build with `PICO_BOARD=waveshare_rp2040_zero`.

### Fixes (not part of a module, not gated)

- `firmware/src/remapper.cc`: `NLAYERS` 4 → 8 (the config format and web tool have 8 layers).
- `firmware/src/tusb_config_both/tusb_config.h`, `firmware/src/tusb_config_host/tusb_config.h`: `CFG_TUH_HUB` 1 → 4 (a hub behind a hub).

### CI (`.github/workflows/`)

- `build-rp2040.yml`: upstream's build steps unchanged, made reusable (runner choice), plus ccache, a "Build fork boards" step (`waveshare_rp2040_zero`) and the `latest` release upload.
- `build.yml` (new): builds on the owner's self-hosted runner, falls back to a GitHub runner.
- `sync-upstream.yml` (new): daily merge of jfedor2/hid-remapper `master`.
