# CLAUDE.md — rp2040-ws2812

Guidance for working in this folder. (The project-root `CLAUDE.md` at
`D:\Devkit\hid-remapper-led\` is a symlink to this file.)

## What this is

WS2812 RGB LED support for the hid-remapper RP2040 firmware, on a **Waveshare
RP2040-Zero clone** (WS2812 on GPIO16, **RGB** byte order) running the `pico`
build. Intended to become its own git submodule, so all module files stay flat
in this folder.

The firmware has **no colours, animations or layer logic of its own** (user
requirement). It only displays hid-remapper **expression register 32** as
`0xIIRRGGBB` (II = intensity, `00` = `WS2812_DEFAULT_INTENSITY`). Everything
else — per-layer colours, colour keys, animations — is done in the config with
expressions; see `example_scroll_lock_layers.json` and `README.md`.

Docs outside the repo, in `D:\Devkit\hid-remapper-led\.claude\`:
`ws2812-implementation.md` (how it works / how to integrate),
`ws2812-changelog.md` (every change and why — keep it updated),
`roadmap.md` (planned features), `bug-nlayers.md` (fork bug fix).

## Rules

- Keep module files in this folder, flat.
- Nothing hardcoded in firmware: no built-in colours, animations or layer
  toggles. Those belong in the config (expressions/mappings).
- Upstream edits are minimal and gated: one `include(... OPTIONAL)` line in
  `firmware/CMakeLists.txt`, three `#ifdef WS2812_ENABLED` blocks in
  `firmware/src/main.cc` (include, `ws2812_led_init()`, `ws2812_led_task()`).
  Document every upstream edit in `README.md`.
- Separate fork bug fix (not part of this module, not gated):
  `firmware/src/remapper.cc:31` `NLAYERS = 4` → `8` (stale after upstream's
  8-layer change 154fb33). See `D:\Devkit\hid-remapper-led\.claude\bug-nlayers.md`.
- Don't change hid-remapper's layer/expression engine for LED features.
- Match hid-remapper style: 4-space indent, repo `.clang-format`.

## Files

| File | Purpose |
| --- | --- |
| `ws2812.pio` | PIO program (converted line by line from the MicroPython `@rp2.asm_pio` sample) + `ws2812_program_init()`. |
| `ws2812_led.h/.cc` | Driver: claims a free PIO SM, `ws2812_led_task()` shows register 32 when it changes. |
| `ws2812_test.cc` | Standalone diagnostic firmware `ws2812_test.uf2` (R/G/B/W cycle, no USB); the only hardcoded colours. |
| `ws2812.cmake` | All build logic (remapper target hook + `ws2812_test`). Defaults: `WS2812_BOARDS=pico`, `WS2812_PIN=16`, `WS2812_RGB_ORDER=1`. |
| `example_scroll_lock_layers.json` | Example config (import in config-tool-web). |
| `README.md` | Usage, colour format, example explanation, exact upstream edits. |
| `message.txt` | Commit message scratch file. |

## Repo / CI

- Fork: `origin` = github.com/realteamnet/hid-remapper (`master`);
  `upstream` = github.com/jfedor2/hid-remapper (remote added locally). The fork
  differs from upstream in: this module, the 3 upstream edits above, the
  NLAYERS fix, and `.github/workflows/build-rp2040.yml`.
- No local cross-compiler (Windows cross builds failed; WSL Ubuntu has no ARM
  toolchain) — build on GitHub Actions (`build-rp2040`). Triggers on
  `firmware/**` or `.github/workflows/*.yml` pushes. Builds **only** the `pico`
  targets `remapper` and `ws2812_test` (`make -j$(nproc)`, ccache cached via
  `actions/cache`); other boards and the PiZero build are commented out.
  Publishes `.uf2`s to the `latest` release (old dual/serial/PiZero files there
  are stale).
- Commit: write `message.txt` here, `git commit -F firmware/rp2040-ws2812/message.txt`,
  prefix `CLAUDE-`, end with the Co-Authored-By line. Fetch/pull first (the
  user also pushes).
- One push at a time; wait for the build result before pushing again.
- Read results with the token in `D:\Devkit\hid-remapper-led\GH_TOKEN.txt`
  (REST: `actions/runs`, `actions/runs/{id}/jobs`, `actions/jobs/{id}/logs`).
- After a green build download `remapper.uf2` (and `ws2812_test.uf2`) from
  `https://github.com/realteamnet/hid-remapper/releases/download/latest/<name>`
  to `D:\Devkit\hid-remapper-led\`.
- Flashing: the board in BOOTSEL mode appears as drive **RPI-RP2** (usually
  `F:`); copy the `.uf2` onto it. During testing, watch for that drive and flash
  without being asked.

## Design notes

- `ws2812_led_init()` runs after `tusb_init()` (Pico-PIO-USB has claimed
  pio0/sm0 and pio1/sm0+1) and after `stdio_init_all()` (on `pico`, GPIO16 is
  UART TX; `pio_gpio_init()` takes the pin over). It turns the LED off, clearing
  the white the LED latches from boot/UART noise. Uses
  `pio_claim_free_sm_and_add_program_for_gpio_range()`.
- No DMA/IRQ; blocking FIFO writes of one word per LED, only when register 32
  changes.
- Reads `extern int32_t registers[]` (global in `remapper.cc`) — no remapper
  code changes needed for the colour.
- GPIO16 is already excluded from remappable GPIOs on `pico` (it's the UART TX
  pin), so `remapper_single.cc` is unchanged.

## Expression facts (for configs)

- Decimal literals are ×1000 internally; hex literals are raw (sent as UINT32,
  so `0xFF......` is fine). JSON exports store decimals already ×1000.
- `store`/`recall` index registers 1-32; `layer_state` is a raw bit mask.
- Limits: **255 elements per expression** (`uint8_t expr_len`; `eol` counts,
  comments don't), **16-deep stack**, 8 expressions, all evaluated every 1 ms in
  order, after layer computation.
- `time_sec` = ms frame counter (seconds ×1000, wraps ~24 days); `time` wraps
  ~35 min; `sin` takes degrees; `round` → whole units.
- Only a mapping with a "Layer N" target can activate a layer (non-zero input).
- A mapping can output to a register ("Register N" target: 1000 while pressed).

## Status

- Old attempt backed up to `D:\Devkit\hid-remapper-led\backup\2026-11-12_pre-reset\`.
- Confirmed on hardware: PIO driver works in `remapper.uf2`, RGB order, USB
  still enumerates (via the old power-on sweep build). **Not yet confirmed on
  hardware:** register-32 colour, the example config (8-layer cycle, colour
  keys, Z X C V B animations) and the NLAYERS fix — built/pushed, test pending.
- `ws2812_test.uf2` (now copy_to_ram) showed nothing when run from flash (XIP);
  retest pending — may indicate a flash/XIP issue on this clone.
