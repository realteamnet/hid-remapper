# CLAUDE.md — rp2040-ws2812

Guidance for working in this folder. (The project-root `CLAUDE.md` at
`D:\Devkit\hid-remapper-led\` is a symlink to this file.)

## What this is

WS2812 RGB LED support for the hid-remapper RP2040 firmware, targeting a
**Waveshare RP2040-Zero** (WS2812 on GPIO16) running the `pico` build. Intended
to become its own git submodule, so all module files stay flat in this folder.

Implementation steps and the change log live outside the repo in
`D:\Devkit\hid-remapper-led\.claude\` (`ws2812-implementation.md`,
`ws2812-changelog.md`, `roadmap.md`). Update the change log with every change
and its reason.

## Rules

- Keep module files in this folder, flat.
- Upstream edits are minimal and gated: one `include(... OPTIONAL)` line in
  `firmware/CMakeLists.txt`, and `#ifdef WS2812_ENABLED` blocks in
  `firmware/src/main.cc`. Document every upstream edit in `README.md`.
- Match hid-remapper style: 4-space indent, repo `.clang-format`.

## Repo / CI

- Fork: `origin` = github.com/realteamnet/hid-remapper (`master`);
  `upstream` = github.com/jfedor2/hid-remapper. The fork differs from upstream
  only in `.github/workflows/build-rp2040.yml` plus this module.
- No local cross-compiler (Windows cross builds failed before) — build on
  GitHub Actions (`build-rp2040`). It triggers on `firmware/**` or
  `.github/workflows/*.yml` pushes, builds the `pico` target (all targets, so
  `ws2812_test.uf2` too) and `waveshare_rp2040_pizero`, and publishes the
  `.uf2`s to the `latest` release.
- Commit: write `message.txt` here, `git commit -F firmware/rp2040-ws2812/message.txt`,
  prefix `CLAUDE-`. Fetch/pull first (the user also pushes).
- One push at a time; wait for the build result before pushing again.
- Read results with the token in `D:\Devkit\hid-remapper-led\GH_TOKEN.txt`
  (REST: `actions/runs`, `actions/jobs/{id}/logs`).
- After a green build download `remapper.uf2` and `ws2812_test.uf2` from
  `https://github.com/realteamnet/hid-remapper/releases/download/latest/<name>`
  to `D:\Devkit\hid-remapper-led\`.

## Design notes

- `ws2812.pio` is a line-by-line conversion of the MicroPython rp2 sample;
  `pioasm` assembles it at build time via `pico_generate_pio_header`.
- `ws2812_led_init()` runs after `tusb_init()` (Pico-PIO-USB has claimed its
  state machines) and after `stdio_init_all()` (on `pico`, GPIO16 is UART TX;
  the PIO takes the pin over). Before init the LED can show junk/white.
- No DMA/IRQ; blocking FIFO writes of one word per LED.
- Colour order: the board (an RP2040-Zero clone) needs **RGB** order, confirmed
  on hardware; `ws2812.cmake` defaults `WS2812_RGB_ORDER=1`.

## Status

- Old attempt backed up to `D:\Devkit\hid-remapper-led\backup\2026-11-12_pre-reset\`.
- `remapper.uf2` PoC confirmed working on hardware (power-on sweep shown;
  colour order fixed to RGB). `ws2812_test.uf2` showed solid white after the
  UF2 auto-reboot � retest after a power cycle.
