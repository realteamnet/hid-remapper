# vdisk: configuration drive

Adds a small USB flash drive to HID Remapper (the `remapper` target, RP2040 and RP2350). The drive is not stored anywhere: every sector is generated when the host reads it.

| On the drive | What it is |
| --- | --- |
| `config.json` | The current configuration, in the same JSON format as the web config tool. It is generated from the live configuration when it is read. Copy it off to make a backup. |
| `config.html` | The web config tool as one self-contained file. Open it in Chrome or Edge; it needs no internet connection. Its Examples list starts with the RGB LED demo (`firmware/ws2812/ws2812_example.json`) and the Logitech K400 Fn lock example (`firmware/hidout/hidout_example.json`). |
| `DEBUG.TXT` | Debug builds only (`-DVDISK_DEBUG=ON`). Debug log, the last 4 KB (e.g. from the `hidout` module). Always exactly 4096 bytes (blank-padded), so the drive never remounts for it; read it uncached (e.g. `FILE_FLAG_NO_BUFFERING`) to see new lines, because a normal read may return Windows' cached copy. |
| `autorun.inf` | Hidden, system, read-only. Only `label=HID-REMAPPER`, so Windows shows the drive as HID-REMAPPER (the FAT volume label, `HIDREMAPPER`, is limited to 11 characters). It starts nothing. |
| `readme.txt` | A short description of the above. |

## Using it

- **Back up:** copy `config.json` from the drive.
- **Restore or load:** copy a `.json` file onto the drive, under any name or over `config.json`. About 1.5 seconds after the copy finishes, the drive disappears for 2.5 seconds and comes back. If the file was a valid configuration it is loaded and saved to flash, as "Save to device" does in the web tool; otherwise the configuration is left as it was. Either way the copied file is gone and `config.json` shows the configuration that is now running, so compare it to see whether it was taken. On a debug build `DEBUG.TXT` also says `configuration loaded and saved` or `configuration not loaded: <reason>`.
- The drive also reloads when the configuration is saved from the web tool, so `config.json` is never stale.
- A change of the emulated device type takes effect after the device is replugged, as with the web tool.

## Limits

- Only files of the current config version (18) are accepted. Older files must be opened and saved in the web tool first.
- Comments in expressions are not stored on the device, so `config.json` has none.
- A file is only applied if it is complete, valid and small enough to be saved. Otherwise the running configuration is left untouched.
- Nothing else written to the drive is kept. Only files whose name ends in `.json` are applied.
- The file's data must be written in ascending order in one piece. Windows, Linux and macOS do this for a normal copy.
- The drive is only added for the two default device types (mouse and keyboard, absolute mouse and keyboard). The gamepad types (Switch, PS4, Stadia, XAC/Flex) keep their exact USB descriptors.
- `config.html` is built from `config-tool-web` when the firmware is built, so it always matches the firmware. `config-tool-web` itself is not changed: the RGB LED demo is only added to the drive's copy. The drive's copy is also patched to show layers 4-7 (upstream's `index.html` hides them with `d-none` in the mapping rows and in "Unmapped inputs passthrough"; this fork's firmware has 8 layers), in the stock column layout: the 8 layer boxes are two rows of 4 the height of the Input/Output buttons, with the layer number inside each box (bold; white on blue when checked) instead of below it, added as a small <style> block. Sticky/Tap/Hold are kept on one line (`flex-nowrap`). `highlight.js` (in this folder) is added to the page and colours the expression boxes like an IDE: comments, register numbers (a number before `store`/`recall`), values, hex, `store`/`recall`, logic, maths, inputs, stack ops, debug ops, LED colour names and unknown words, with the box growing to fit its text. Each patch is a regex in `webtool.cmake` that must match exactly (8 checkboxes, otherwise one place), or the build stops.
- The web tool adds about 106 KB to the firmware image. The file is a small loader holding the gzipped page; the browser unpacks it when it is opened (Chrome or Edge 80+, which WebHID needs anyway).

## Build

`vdisk.cmake` adds the module to the `remapper` target on every board. Turn it off with `-DVDISK_ENABLE=OFF`; the firmware is then identical to one without this folder.

| CMake setting | Default | Meaning |
| --- | --- | --- |
| `VDISK_ENABLE` | `ON` | Build the drive into `remapper`. |
| `VDISK_WEB_TOOL` | `ON` | Put the web config tool on the drive. Needs internet access at configure time (to download Bootstrap). |
| `VDISK_DEBUG` | `OFF` | Add `DEBUG.TXT` and `vdisk_log()`. Without it the log code is not built and other modules' log calls do nothing. |

`VDISK_EP_OUT` / `VDISK_EP_IN` (`0x04` / `0x84`) in `vdisk.h` are the bulk endpoints.

The build stops with an error if `src/config.cc` has a different `CONFIG_VERSION` than `VDISK_CONFIG_VERSION` in `vdisk.h`, or if the `Op` enum in `src/types.h` has a different number of ops than `op_names` in `vdisk.cc`. Either means upstream changed the config format, and `vdisk.cc` must be updated before the drive can read or write it.

## Changes to hid-remapper's own files

| File | Change |
| --- | --- |
| `firmware/CMakeLists.txt` | `include(vdisk/vdisk.cmake OPTIONAL)` after the `remapper` target. |
| `firmware/src/tusb_config_both/tusb_config.h` | `#ifndef` around `#define CFG_TUD_MSC 0`, so the build can set it to 1. |
| `firmware/src/tinyusb_stuff.cc` | Inside `#ifdef VDISK_ENABLED`: the include, and `tud_descriptor_configuration_cb()` returns `vdisk_configuration_descriptor(...)`. |
| `firmware/src/main.cc` | Inside `#ifdef VDISK_ENABLED`: the include, and `vdisk_task()` after `tud_task()`. |

## How it works

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
  2. `webtool.cmake` joins `crc.js`, `usages.js`, `examples.js` and `code.js` with their `import`/`export` lines removed, adds the two example configs as the first examples. It then strips the indentation from `index.html` and puts Bootstrap and the minified script into it inline (about 740 KB).
  3. It strips indentation and blank lines from the joined scripts, gzips the page with CMake's own `file(ARCHIVE_CREATE ... FORMAT raw COMPRESSION GZip)`, encodes the result as base64 (in CMake, from the hex it reads), and writes `config.html` as a short loader that decodes it with the browser's own base64 decoder and `DecompressionStream` and replaces itself with the page. About 106 KB (the page itself is about 540 KB). With CMake older than 3.19 the page is written unpacked.
  `vdisk.cmake` links the file into the `.flashdata` section, so it stays in flash and is read from there; `config.json` follows it on the volume.
- The scripts are not minified: gzip removes almost the same redundancy (minifying first would save about 10 KB of the final file).
- **Reload:** after a change, test-unit-ready reports "medium not present" for 2.5 seconds, which makes the host mount the drive again.
