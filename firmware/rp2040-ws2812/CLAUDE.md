# CLAUDE.md — rp2040-ws2812

Guidance for working in this folder. (The repo-root `CLAUDE.md` is a symlink to
this file.)

## What this is

A self-contained WS2812 RGB LED driver for the hid-remapper RP2040/RP2350
firmware. It is intended to become its own git submodule, so everything stays
in this single flat folder — do not spread files into `firmware/src/` or other
parts of the hid-remapper tree.

## Rules

- **Keep all new files in this folder**, flat (no subdirectories).
- **Minimise upstream changes, and gate them.** Edits to hid-remapper's own
  files are kept small and wrapped in `#ifdef WS2812_ENABLED` so boards without
  the define are byte-identical to upstream. Every upstream edit is documented
  in `README.md` ("Changes required to integrate").
- Match hid-remapper's style: 4-space indent, see the repo `.clang-format`.

## Applied upstream edits (gated by `WS2812_ENABLED`)

- `firmware/CMakeLists.txt` — compile `ws2812_led.cc`, `pico_generate_pio_header`
  for `ws2812.pio`, add the include dir, and define `WS2812_ENABLED WS2812_PIN=16`
  for `pico`, `pico2`, `waveshare_rp2040_pizero`.
- `firmware/src/main.cc` — `#include "ws2812_led.h"`, `ws2812_led_init()` after
  `stdio_init_all()`, and `ws2812_led_activity_on()` / `ws2812_led_task()` (via
  `ws2812_led_activity_off_maybe()`) in the main loop. The stock
  `activity_led_on()` / `activity_led_off_maybe()` calls are **temporarily
  commented out** (suspected to interfere); to be replaced by ws2812 calls.
- `firmware/src/remapper_single.cc` — exclude `WS2812_PIN` from
  `get_gpio_valid_pins_mask()`.

## Layout

| File            | Purpose                                                          |
| --------------- | ---------------------------------------------------------------- |
| `ws2812.pio`    | PIO program + `ws2812_program_init()` (BSD-3-Clause).            |
| `ws2812_led.h`  | Public C API + compile-time config (`WS2812_PIN`, `WS2812_NUM_LEDS`, …). |
| `ws2812_led.cc` | Framebuffer, PIO state-machine setup, optional activity blink.   |
| `README.md`     | Usage + the exact edits needed to wire the module into hid-remapper. |

## Build / CI workflow

- Built on GitHub Actions by `.github/workflows/build-rp2040.yml`. It triggers on
  pushes under **`firmware/**` or `.github/workflows/*.yml`** — a workflow-only
  edit at the repo root does NOT trigger it, so to run CI after editing the YAML,
  push a `firmware/**` change (bumping `message.txt` is enough).
- **Commit via `message.txt`:** write the message to
  `firmware/rp2040-ws2812/message.txt`, then `git commit -F firmware/rp2040-ws2812/message.txt`.
  (User says `-f`; the working flag is `-F`.) Prefix the message with `CLAUDE-`.
  Always `git fetch`/pull before committing — the user also pushes commits.
- **Push pacing:** don't push more than once until the triggered build is done
  and its results checked.
- **Reading results:** no `gh` CLI; a **read token at `../GH_TOKEN.txt`** (i.e.
  `D:\Devkit\hid-remapper-led\GH_TOKEN.txt`, outside the repo) authenticates REST
  API calls — poll `actions/runs`, read job logs (`actions/jobs/{id}/logs`).
  Use the token in the header to avoid rate limits.
- **Releases:** a successful build publishes the `.uf2`s to the `latest` Release.
  After a successful build, download `remapper.uf2` from
  `https://github.com/realteamnet/hid-remapper/releases/download/latest/remapper.uf2`
  to the project root `D:\Devkit\hid-remapper-led\`.

## Design notes

- Uses the RP2040 **PIO** to generate the WS2812 waveform; claims one free state
  machine on `WS2812_PIO` (default `pio0`) via `pio_claim_unused_sm()`.
- Pixels are pushed with **blocking FIFO writes — no DMA** — so it never
  conflicts with the Pico-PIO-USB host (pio0/sm0 TX, pio1/sm0+sm1 RX/EOP, DMA ch0).
- `ws2812_led_show(lit_ms, flash_ms)` is **non-blocking**: it lights the LED and
  returns; `ws2812_led_task()` (pumped each loop via
  `ws2812_led_activity_off_maybe()`) handles auto-off and on/off flashing.
- The activity helpers mirror the stock `activity_led` API so they wire in at the
  same call sites.
- Gotcha: `WS2812_PIN` must be kept out of `get_gpio_valid_pins_mask()`, or the
  data line is repurposed as a remappable GPIO. See README §3.

## Current bring-up state (open items)

- `ws2812_led_init()` sets a **steady dim green** as a power-on/colour-order
  indicator (TODO: revert to off-at-idle once confirmed).
- **Colour order unconfirmed:** if the LED shows **red** where green is intended,
  the strip is RGB-ordered — swap the byte order in `ws2812_led_set()`.
- The earlier "always white" LED was the **uninitialised WS2812 on GPIO16**
  picking up UART/boot signals — stock hid-remapper has no white/LED-colour code.
- **Separate, pre-existing issue:** the `waveshare_rp2040_pizero` build does not
  enumerate over USB (the `pico` build does). Not caused by ws2812 (never ran in
  that build). Suspect the local board header's flash config
  (`PICO_FLASH_SPI_CLKDIV 2`, `PICO_FLASH_SIZE_BYTES 1MB` vs the real 16MB).
