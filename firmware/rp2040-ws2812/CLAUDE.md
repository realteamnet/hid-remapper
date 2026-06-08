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
- **Do not modify hid-remapper's original files.** Any change the firmware needs
  to use this module is *documented* in `README.md` ("Changes required to
  integrate"), not applied to the upstream tree.
- Match hid-remapper's style: 4-space indent, see the repo `.clang-format`.

## Layout

| File            | Purpose                                                          |
| --------------- | ---------------------------------------------------------------- |
| `ws2812.pio`    | PIO program + `ws2812_program_init()` (BSD-3-Clause).            |
| `ws2812_led.h`  | Public C API + compile-time config (`WS2812_PIN`, `WS2812_NUM_LEDS`, …). |
| `ws2812_led.cc` | Framebuffer, PIO state-machine setup, optional activity blink.   |
| `README.md`     | Usage + the exact edits needed to wire the module into hid-remapper. |

## Design notes

- Uses the RP2040 **PIO** to generate the WS2812 waveform; claims one free state
  machine on `WS2812_PIO` (default `pio0`) via `pio_claim_unused_sm()`.
- Pixels are pushed with **blocking FIFO writes — no DMA** — so it never
  conflicts with the Pico-PIO-USB host (pio0/sm0 TX, pio1/sm0+sm1 RX/EOP, DMA ch0).
- The activity helpers (`ws2812_led_activity_on/off_maybe`) intentionally mirror
  the stock `activity_led` API so they can be wired in at the same call sites.
- Gotcha: the `WS2812_PIN` must be kept out of hid-remapper's GPIO management
  (`get_gpio_valid_pins_mask()`), or the data line is repurposed as a remappable
  GPIO. See README §3.
