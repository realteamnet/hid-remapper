# real-team

The real-team additions to [hid-remapper](https://github.com/jfedor2/hid-remapper), in one folder. hid-remapper includes one file from here (`real-team.cmake`) and calls three functions (`real_team.h`); everything else is inside this folder, so it can later become a single git submodule.

| Folder / file | What it is |
| --- | --- |
| [`ws2812/`](ws2812/README.md) | RGB LED (WS2812, WS2812B, SK6812 RGB, NeoPixel) driven by the config through registers 31 (settings) and 32 (colour). Boot colour `0x200000FF` on boards whose header names their LED. |
| [`vdisk/`](vdisk/README.md) | USB configuration drive: live `config.json`, apply a config by copying any `.json` onto it, `config.html` (the web config tool), `readme.txt`, hidden `autorun.inf`. |
| [`hidout/`](hidout/README.md) | HID output reports to devices plugged into the remapper, defined in the config (send lines, watch lines; e.g. Logitech K400 Fn lock). |
| `real-team.cmake` | Includes the three modules and adds `real_team.cc` and the `REAL_TEAM_ENABLED` define to the `remapper` target. |
| `real_team.h`, `real_team.cc` | The three functions hid-remapper calls; each forwards to the modules that are built in. |
| `message.txt` | Commit-message scratch file. |

## Build settings

Each module is on by default; turn one off with `-D<NAME>_ENABLE=OFF`:

| Setting | Module |
| --- | --- |
| `WS2812_ENABLE` | `ws2812/` (more settings in its README) |
| `VDISK_ENABLE` | `vdisk/` (`VDISK_WEB_TOOL`, `VDISK_DEBUG`) |
| `HIDOUT_ENABLE` | `hidout/` (`HIDOUT_REGISTER`) |

With all three off, or without this folder, the firmware is upstream's (plus the two fixes below). The web tool on the drive only lists an example when its module is in the build.

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
