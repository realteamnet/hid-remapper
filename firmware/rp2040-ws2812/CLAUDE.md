# CLAUDE.md — rp2040-ws2812

Guidance for working in this folder. (The project-root `CLAUDE.md` at
`D:\Devkit\hid-remapper-led\` is a symlink to this file.)

## What this is

WS2812 RGB LED support for the hid-remapper RP2040/RP2350 firmware. Intended to
become its own git submodule, so all module files stay flat in this folder.

- **Any board:** built into the `remapper` target on every board; nothing board
  specific in the code or build.
- **Dormant by default:** while expression **register 31** is 0, no LED code
  runs (no PIO state machine, no pin).
- **Config-driven:** register 31 = settings `0x00LOOOPP` (PP = GPIO, OOO =
  colour order nibbles 1=R 2=G 3=B with 000 = GRB, L = master level 1-F with
  0 = full). Register 32 = colour `0xIIRRGGBB` (II = intensity, 00 = default).
- **Nothing hardcoded:** no colours, animations or layer logic in firmware.
  Those live in the config; see `ws2812_rgb_led.json` and
  `README.md`.

Test hardware: Waveshare RP2040-Zero clone, WS2812 on GPIO16, RGB order
(settings `0x00012310`), running the `pico` build.

Docs outside the repo, in `D:\Devkit\hid-remapper-led\.claude\`:
`ws2812-implementation.md` (how it works), `ws2812-changelog.md` (every change
and why — keep it updated), `roadmap.md` (planned features and open to-dos),
`bug-nlayers.md` and `bug-hubs.md` (fork bug fixes), `vdisk-implementation.md` (configuration drive).

Sister module: `firmware/rp2040-vdisk/` (USB configuration drive: live
`config.json`, `APPLY` drop folder). It has its own `README.md`; the rules
below apply to it too. Its upstream edits: one include line in
`CMakeLists.txt`, `#ifndef` around `CFG_TUD_MSC` in
`src/tusb_config_both/tusb_config.h`, and `#ifdef VDISK_ENABLED` blocks in
`src/tinyusb_stuff.cc` and `src/main.cc`.

## Rules

- Keep module files in this folder, flat.
- Nothing hardcoded in firmware: no built-in colours, animations, layer toggles,
  pins or board lists.
- Upstream edits are minimal and gated. Document each one in `README.md`:
  - `firmware/CMakeLists.txt`: one `include(... OPTIONAL)` line.
  - `firmware/src/main.cc`: two `#ifdef WS2812_ENABLED` blocks (include,
    `ws2812_led_task()`).
  - `config-tool-web/code.js`: LED colour names in expressions (`led_colors`,
    `led_color_to_hex()`, 4 lines in `expression_onchange()`).
- Separate fork bug fix (not part of this module, not gated):
  `firmware/src/remapper.cc:31` `NLAYERS = 4` → `8`. See
  `D:\Devkit\hid-remapper-led\.claude\bug-nlayers.md`.
- Separate fork fix: `CFG_TUH_HUB` 1 → 4 in `src/tusb_config_both/tusb_config.h`
  and `src/tusb_config_host/tusb_config.h` (hub behind a hub). See
  `D:\Devkit\hid-remapper-led\.claude\bug-hubs.md`.
- Don't change hid-remapper's layer/expression engine for LED features without
  asking the user.
- Two separate registers for settings and colour (decided; don't multiplex).
  Their numbers are CMake settings, defaults 31/32.
- Match hid-remapper style: 4-space indent, repo `.clang-format`.
- Keep the code small and almost comment-free, like upstream's. Explanations
  go in `README.md` or the `.claude` docs, not in code comments.

## Files

| File | Purpose |
| --- | --- |
| `ws2812.pio` | PIO program (from the MicroPython `@rp2.asm_pio` sample) + `ws2812_program_init()`. |
| `ws2812_led.h/.cc` | Driver; the only entry point is `ws2812_led_task()`. |
| `ws2812.cmake` | All build logic. `WS2812_ENABLE`, `WS2812_SETTINGS_REGISTER`, `WS2812_COLOR_REGISTER`. |
| `ws2812_rgb_led.json` | Example config (import in config-tool-web). It is the only test aid: the user wants no test firmware or helper scripts kept. |
| `README.md` | Usage, register formats, example explanation, exact upstream edits. |
| `message.txt` | Commit message scratch file. |

## Repo / CI

- Fork: `origin` = github.com/realteamnet/hid-remapper (`master`);
  `upstream` = github.com/jfedor2/hid-remapper (remote added locally).
- Docker Desktop is installed: code that doesn't need the Pico SDK (e.g.
  `vdisk.cc` with the real `config.cc`) can be compiled and tested in a
  container with stub headers. Keep such tests in the scratchpad, not the repo.
- No local cross-compiler (Windows cross builds failed; WSL Ubuntu has no ARM
  toolchain) — build on GitHub Actions (`build-rp2040`). Triggers on
  `firmware/**` or `.github/workflows/*.yml` pushes. Builds the `pico` target
  `remapper` (published to the `latest` release) plus a
  `pico2` (RP2350) compile check of `remapper` (not published). ccache is
  cached with `actions/cache`. Other boards are commented out; old
  dual/serial/PiZero files in the release are stale.
- Commit: write `message.txt` here, `git commit -F firmware/rp2040-ws2812/message.txt`,
  prefix `CLAUDE-`, end with the Co-Authored-By line. Fetch/pull first (the
  user also pushes).
- One push at a time; wait for the build result before pushing again.
- Read results with the token in `D:\Devkit\hid-remapper-led\GH_TOKEN.txt`
  (REST: `actions/runs`, `actions/runs/{id}/jobs`, `actions/jobs/{id}/logs`).
- After a green build download `remapper.uf2` from
  `https://github.com/realteamnet/hid-remapper/releases/download/latest/<name>`
  to `D:\Devkit\hid-remapper-led\`.
- Flashing: the board in BOOTSEL mode appears as drive **RPI-RP2** (usually
  `F:`); copy the `.uf2` onto it. During testing, watch for that drive and flash
  without being asked.

## Design notes

- `ws2812_led_task()` (main loop, after `activity_led_off_maybe()`) compares
  register 31 with the last applied value and calls `configure()`
  on change; while inactive it returns right after that compare.
- Activation: `pio_claim_free_sm_and_add_program_for_gpio_range()` (Pico-PIO-USB
  holds pio0/sm0 and pio1/sm0+1), remember the pin's current function,
  `ws2812_program_init()`. Deactivation: send off, drain FIFO + 350 us latch,
  disable SM, `pio_remove_program_and_unclaim_sm()`, restore the pin function.
- Registers are read via `extern int32_t registers[]` (global in
  `remapper.cc`). Same core and loop as the expression engine, so no locking.
- No DMA/IRQ; one blocking FIFO word per LED, only when the colour changes.
- On `pico`, GPIO16 is the debug UART TX until the LED takes it (and again
  after it is released).
- A runtime-chosen pin is not removed from hid-remapper's remappable GPIOs
  (that would need an upstream edit); the README tells users not to map it.

## Expression facts (for configs)

- Decimal literals are ×1000 internally; hex literals are raw (sent as UINT32,
  so `0xFF......` is fine). JSON exports store decimals already ×1000.
- `store`/`recall` index registers 1-32; `layer_state` is a raw bit mask.
- Limits: **16-deep stack**; 8 expressions, all evaluated every 1 ms in order,
  after layer computation. No per-expression length limit in config v18 (the
  255 figure in older notes came from the v6 loader); the whole config must fit
  `PERSISTED_CONFIG_SIZE` (4096): 1 byte per op, 5 per number/hex.
- `N port` sets the hub port for later inputs and `plugged_in`; it is reset only
  once per tick, so end an expression that uses it with `0 port`.
- `time_sec` = ms frame counter (seconds ×1000, wraps ~24 days); `time` wraps
  ~35 min; `sin` takes degrees; `round` → whole units (still ×1000, so multiply
  by a hex constant such as `0x10000` or `0x1` to get a raw value).
- Only a mapping with a "Layer N" target can activate a layer (non-zero input).
- A mapping can output to a register ("Register N" target: 1000 while pressed).

## Status

- Old attempt backed up to `D:\Devkit\hid-remapper-led\backup\2026-11-12_pre-reset\`.
- Confirmed on hardware (older builds): PIO driver in `remapper.uf2`, RGB order,
  USB enumeration.
- **Not yet confirmed on hardware:** the dormant/any-board driver, register
  31/32 control, the example config (8-layer cycle; F1-F5 modes with keys 1-0;
  hub-port and gamepad modes — checked only in a throw-away simulator), the
  palette values, the NLAYERS fix, colour names in the web tool, and the whole
  configuration drive (checked only on a PC against mtools/fsck.fat).
