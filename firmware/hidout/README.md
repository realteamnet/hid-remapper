# hidout

Lets a hid-remapper **expression send a HID output report to a connected device**, for example a vendor command that the device's own software would normally send. The config decides what is sent and to which devices; the firmware has nothing device-specific in it.

- **Dormant by default.** While the first register's top byte (the report ID) is 0, nothing is sent. That is always the case on upstream firmware, which ignores these registers, so configs that use them still load there.
- **Optional.** `-DHIDOUT_ENABLE=OFF` leaves it out of the build.

## Registers

| Register (default) | Value |
| --- | --- |
| `HIDOUT_REGISTER` (1) | Bytes 0-3 of the report, written `0xB0B1B2B3`. **B0 is the report ID**; `0` = idle. |
| `HIDOUT_REGISTER + 1` (2) | Bytes 4-7, written `0xB4B5B6B7`. |
| next `HIDOUT_TARGETS` registers (3, 4, 5) | Target array. Each entry is `0xVVVV` = every product of vendor VVVV, or `0xVVVVPPPP` = exactly that VID:PID, or `0` = unused. **All 0 = any device.** |

- **Where it goes.** The report goes to every connected device interface that declares an output report with that report ID and matches any target entry.
- **Length.** The interface's declared size of that report, at most 7 bytes after the report ID; missing bytes are 0.
- **When it is sent.**
  - Once when the registers change (the report bytes or the targets).
  - Again when a matching device appears later, e.g. after the receiver is plugged back in. The module checks 4 times a second.
  - Not on every tick, so writing the registers every tick is fine.
- **Report types.** Only output reports (HID SET_REPORT, type Output); feature reports are not sent.
- **Not supported:** reading replies. If a command needs a value the device reports first, this module can't do it.

## Example: Logitech K400 Fn lock on Fn+Esc

The K400's Fn lock (F1-F12 or media keys first) is normally switched by Logitech software. That software sends a HID++ short report to the Unifying receiver (`046D:C52B`):

- F-keys first: `10 01 09 19 00 00 00`
- media keys first: `10 01 09 18 01 00 00`

(report ID `10`, keyboard in receiver slot `01`, feature index `09` = FN_INVERSION, function 1, then the value). On the K400, Fn+Esc reaches the host as **Power** (`0x00070066`), so it can be the trigger.

```
/* register 6 = 1 F-keys first, 2 media keys first; 1 at power-on */
6 recall 0 eq 0x1 6 recall ifte 6 store
/* Fn+Esc (Power) toggles it */
0x00070066 input_state_binary 0x00070066 prev_input_state_binary not mul
0x3 6 recall sub 6 recall ifte 6 store
/* the report and its target */
6 recall 0x1 eq 0x10010919 0x10010918 ifte 1 store
6 recall 0x1 eq 0x00000000 0x01000000 ifte 2 store
0x046DC52B 3 store
```

- Also map **Power (`0x00070066`) → Nothing** on every layer, or the computer gets a Power key press (sleep or shutdown) with every Fn+Esc.
- Use hex (`0x1`) for raw values; plain decimals are stored ×1000.
- `hidout_example.json` in this folder is a config with only this (one expression and the mapping). `firmware/ws2812/ws2812_example.json` also contains it (expression 6 and the mapping). Both are listed under Examples in the config drive's web tool.
- **Caveats:**
  - The bytes come from github.com/h8man/k400p-fn-lock-win (K400 Plus). Feature index `09` is not confirmed on the original K400; if nothing changes, the index differs on that model.
  - If the keyboard is in another receiver slot, change `01`.
  - The setting is sent again when the receiver is re-plugged, but not when only the keyboard is switched off and on.

## Build

| CMake setting | Default | Meaning |
| --- | --- | --- |
| `HIDOUT_ENABLE` | `ON` | Build the module into `remapper`. |
| `HIDOUT_REGISTER` | `1` | First of the two report registers. |
| `HIDOUT_TARGETS` | `3` | Length of the target array (one register each). `HIDOUT_REGISTER + 1 + HIDOUT_TARGETS` must be ≤ 32. |

## Changes to hid-remapper's own files

| File | Change |
| --- | --- |
| `firmware/CMakeLists.txt` | `include(hidout/hidout.cmake OPTIONAL)` |
| `firmware/src/main.cc` | Inside `#ifdef HIDOUT_ENABLED`: the include, and `hidout_task()` at the end of the main loop. |

## How it works

- `hidout_task()` reads the registers (`extern int32_t registers[]`, `remapper.cc`).
- When they change, or every 250 ms while active, it walks `their_out_usages` (all connected interfaces' output reports) under the `THEIR_USAGES` mutex.
- The VID:PID of each interface's device comes from TinyUSB's `tuh_vid_pid_get()`.
- For each match not yet sent to, it calls `queue_out_report()` with the length from `out_report_sizes`.
- It remembers which interfaces got the current report; interfaces that disappear drop out of that list, so they get it again when they return.
- A device that rejects the report does not block hid-remapper's output queue (TinyUSB still reports completion). The queue holds 8 reports, so with more than about 8 matching interfaces at once the rest are skipped.
