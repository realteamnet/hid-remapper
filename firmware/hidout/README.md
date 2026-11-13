# hidout

Lets a hid-remapper config **send HID output reports to the devices plugged into the remapper** (out of its USB host port, not to the computer), for example a vendor command that the device's own software would normally send. The config holds a table of messages, each with its own target devices, and an expression picks which ones are sent. The firmware has nothing device-specific in it.

- **Dormant by default.** While the selector register is 0, nothing is sent. That is always the case on upstream firmware, which ignores the register and never runs the table as anything but a list of numbers, so configs that use it still load there.
- **Optional.** `-DHIDOUT_ENABLE=OFF` leaves it out of the build.

## The table

Table lines are the expression lines (ended by a line break) that hold **only numbers**, in any expression. They can sit next to the logic in the same expression. Each one is one message:

```
<length> <report words...> <targets...>
```

| Part | Meaning |
| --- | --- |
| `<length>` | Length of the report in bytes, **including the report ID**, as a plain decimal (e.g. `7` for a HID++ short report, `20` for a long one). At most 64. |
| `<report words>` | The report as 32-bit hex words, first byte first, **report ID first**: `10 01 0F 1A 00 00 00` is written `0x10010F1A 0x00000000`. As many words as the length needs (`7` → 2 words); unused bytes of the last word are ignored. |
| `<targets>` | Any number of devices, any mix of: `0xVVVV` = every product of vendor VVVV; `0xVVVVPPPP` = exactly that VID:PID. **No targets = any device** that declares an output report with that report ID. |

- **Line numbers** count table lines only, from 0, starting in expression 1 and continuing through expression 8. Comment-only lines and lines with any operation (`store`, `recall`, ...) don't count, so each message can have a comment line above it, and a line commented out with `/* */` is not a table line.
- **Comments** are not stored on the device, so a config read back from the device shows the table without them.
- **Malformed lines** (too few words for the length, a length below 2, or report ID 0) keep their line number but are never sent.
- **Evaluation:** hid-remapper also evaluates the table lines as part of their expression, so each line's values are pushed and stay on the stack. Keep the expression within the 16-deep stack (a 7-byte line with one target pushes 4 values). If it overflows, hid-remapper skips the whole expression, including any logic in it; `hidout` still reads the table directly.

## The selector register

`HIDOUT_REGISTER` (default **register 1**) selects the lines: **bit n = line n** (`0x1` = line 0, `0x2` = line 1, `0x4` = line 2, …), up to 32 lines. Several bits may be set at once.

- **A line is sent** to each matching device when its bit turns on.
- **It is sent again** when a matching device appears later (e.g. the receiver is plugged back in; checked 4 times a second), and when the table is edited.
- **It is not sent on every tick**, so writing the register every tick is fine.
- **Forcing a resend:** writing 0 for one tick and then the bits again sends the selected lines again. Use this for devices that forget a setting when they are switched off.
- **Length sent:** the report goes out with the length the device declares for that report ID (padded with zeros, or cut at 63 bytes after the ID). If the device doesn't declare it, the length from the table is used.
- **Report types:** only output reports (HID SET_REPORT, type Output); feature reports are not sent.
- **Not supported:** reading replies. If a command needs a value the device reports first, this module can't do it.

## Example: Logitech K400 family, Fn lock on Fn+Esc

`hidout_example.json` in this folder. It is also the second entry under Examples in the config drive's web tool.

The K400's Fn lock (F1-F12 or media keys first) is normally switched by Logitech software. It is a HID++ 2.0 command to the keyboard through its Unifying receiver (`046D:C52B`), feature FN_INVERSION (`0x40A0`). The feature's index differs per model:

| Model | Feature index | F-keys first | Media keys first | Source |
| --- | --- | --- | --- | --- |
| K400r (and K400) | `0F` | `10 01 0F 1A 00 00 00` | `10 01 0F 1A 01 00 00` | Read from a K400r (wireless PID `4024`) and confirmed on it, 2026-11-13. Then confirmed through the remapper with this example: the receiver answers `11 01 0F 1A 00 01 ...` / `... 01 01 ...`. While the keyboard sleeps it answers `10 01 8F 0F 1A 09 00` (HID++ error 09); the 5 s resend gets through once it wakes. The original K400 shares that wireless PID family; assumed the same, not tested. |
| K400 Plus | `09` | `10 01 09 19 00 00 00` | `10 01 09 18 01 00 00` | From github.com/h8man/k400p-fn-lock-win and github.com/sginne/fn_key_k400_for_logitech; not tested here. |

`01` is the keyboard's slot on its receiver. The receiver is the same (`C52B`) for all models, so the firmware can't tell which model is behind it. The config says so.

Everything is in expression 1, the table and the logic:
```
/* line 0: K400 / K400r, F-keys first (FN_INVERSION at index 0F, confirmed on a K400r) */
7 0x10010F1A 0x00000000 0x046DC52B
/* line 1: K400 / K400r, media keys first */
7 0x10010F1A 0x01000000 0x046DC52B
/* K400 Plus (index 09, not tested): use these two lines instead of lines 0 and 1, without the comment marks */
/* 7 0x10010919 0x00000000 0x046DC52B */
/* 7 0x10010918 0x01000000 0x046DC52B */
/* Fn lock at boot: 0x1 = F-keys first, 0x2 = media keys first */
6 recall 0 eq 0x1 6 recall ifte 6 store
/* Fn+Esc (reported as Power) toggles it */
0x00070066 input_state_binary 0x00070066 prev_input_state_binary not mul
0x3 6 recall sub 6 recall ifte 6 store
/* send line 0 or 1; 0 for one tick every 5 s so it is sent again */
6 recall 0x1 eq 0x1 0x2 ifte time_sec 5 mod 0.001 lt not mul 1 store
```

- **Model:** keep the two lines for your keyboard and leave the other model's lines commented out. Comments are not stored on the device, so the commented lines are gone when the config is read back.
- **Settings:** the Fn lock at boot is the `0x1` after `0 eq`. Register 6 holds the current state, register 1 is the selector. The table lines leave 8 values on the stack (peak 11 of 16).
- **Map Power (`0x00070066`) → Nothing** on every layer (the example does). On the K400 family Fn+Esc reaches the host as Power, and the computer would otherwise sleep or shut down with every press.
- **Resend every 5 s:** the keyboard forgets the setting when it is switched off. The selector is 0 for one tick every 5 s, so the setting comes back within 5 s after the keyboard wakes. While the keyboard is off, the receiver just answers with an error. Change `5` to resend less often.
- **Raw values:** use hex (`0x1`) for raw values; plain decimals are stored ×1000. The table's length is the one decimal: the firmware divides it by 1000.
- **Other slots or models:** if the keyboard is in another receiver slot, change the second byte (`01`). For another model, the index and slot can be read from the keyboard with a HID++ root lookup (`10 <slot> 00 0A 40 A0 00` → reply byte 4 is the index).
- **Two receivers of the same type** (e.g. a second Unifying receiver with mice) both match `0x046DC52B`. The command then also reaches slot 1 of the other receiver, which would run function 1 of whatever feature that device has at the same index. Plug only the keyboard's receiver into the remapper, or make sure slot 1 of the other receiver is not affected.

## Debugging

- **`DEBUG.TXT`** on the config drive (debug builds of the `vdisk` module, `-DVDISK_DEBUG=ON`): the table (line count, malformed lines), every change of the selector, the connected interfaces with their VID:PID and output report IDs, and per line how many interfaces have the report, how many match, and where it was sent. Repeated resends with the same result are not logged again. For 2 s after each send, up to 8 input reports from the interface it was sent to are logged too (`reply from interface ...`): a HID++ device answers with its result or an error there.
- **Monitor tab** of the web tool (while monitoring is on): `0xFFFE00nn` = line `nn` sent (value: interface), `0xFFFE01nn` = interfaces with its report ID, `0xFFFE02nn` = interfaces that also match its targets.

## Build

| CMake setting | Default | Meaning |
| --- | --- | --- |
| `HIDOUT_ENABLE` | `ON` | Build the module into `remapper`. |
| `HIDOUT_REGISTER` | `1` | The selector register (1-32). |

## Changes to hid-remapper's own files

| File | Change |
| --- | --- |
| `firmware/CMakeLists.txt` | `include(hidout/hidout.cmake OPTIONAL)` |
| `firmware/src/main.cc` | Inside `#ifdef HIDOUT_ENABLED`: the include, and `hidout_task()` at the end of the main loop. |
| `firmware/src/remapper_single.cc` | Inside `#ifdef HIDOUT_ENABLED`: the include, and `hidout_report_received()` at the start of `report_received_callback()`, so replies from a device can be logged. |

## How it works

- `hidout_task()` reads the selector (`extern int32_t registers[]`, `remapper.cc`).
- When it changes, or every 250 ms while it is not 0, `hidout_task()` does the following:
  - It reads the table from `expressions[]` under the `EXPRESSIONS` mutex: values grouped by `eol`, the first value of a line divided by 1000. A hash of the table detects edits.
  - For each selected line, it walks `their_out_usages` (all connected interfaces' output reports) under the `THEIR_USAGES` mutex.
  - It checks targets with TinyUSB's `tuh_vid_pid_get()`.
  - It calls `queue_out_report()` for each matching interface the line has not been sent to yet.
- Per line it remembers which interfaces got it; interfaces that disappear drop out of that list, so they get it again when they return.
- A device that rejects a report does not block hid-remapper's output queue (TinyUSB still reports completion). The queue holds 8 reports, so with more than about 8 sends at once the rest are skipped until the next change.
