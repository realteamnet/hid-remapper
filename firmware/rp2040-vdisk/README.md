# rp2040-vdisk: configuration drive

Adds a small USB flash drive to HID Remapper (the `remapper` target, RP2040 and RP2350). The drive is not stored anywhere: every sector is generated when the host reads it.

| On the drive | What it is |
| --- | --- |
| `config.json` | The current configuration, in the same JSON format as the web config tool. It is generated from the live configuration when it is read. Copy it off to make a backup. |
| `APPLY\` | Copy a configuration `.json` file into this folder to load it. The device applies it and saves it to flash, as "Save to device" does in the web tool. |
| `STATUS.TXT` | Result of the last file copied into `APPLY`: `OK: ...` or `ERROR: ...`. |
| `WEBTOOL.HTM` | The web config tool as one self-contained file. Open it in Chrome or Edge; it needs no internet connection. |
| `README.TXT` | A short description of the above. |

## Using it

- **Back up:** copy `config.json` from the drive.
- **Restore or load:** copy a `.json` file into `APPLY`. About 1.5 seconds after the copy finishes, the drive disappears for 2.5 seconds and comes back. `config.json` then shows the new configuration, `STATUS.TXT` shows the result, and `APPLY` is empty again.
- **Firmware update:** copy a firmware `.uf2` file into `APPLY`. The device restarts in firmware flashing mode (the same as "Flash firmware" in the web tool) and the `RPI-RP2` drive appears. Copy the `.uf2` file to that drive to flash it. The file copied into `APPLY` is only the trigger; it is not flashed.
- The drive also reloads when the configuration is saved from the web tool, so `config.json` is never stale.
- A change of the emulated device type takes effect after the device is replugged, as with the web tool.

## Limits

- Only files of the current config version (18) are accepted. Older files must be opened and saved in the web tool first.
- Comments in expressions are not stored on the device, so `config.json` has none.
- A file is only applied if it is complete, valid and small enough to be saved. Otherwise the running configuration is left untouched.
- Nothing else written to the drive is kept. Files outside `APPLY` are ignored.
- The file's data must be written in ascending order in one piece. Windows, Linux and macOS do this for a normal copy.
- The drive is not added when the device emulates a console controller (Switch, PS4, Stadia), so those keep their exact USB descriptors.
- `WEBTOOL.HTM` is built from `config-tool-web` when the firmware is built. The build downloads Bootstrap to put it in the file; if that fails, the file keeps the CDN links and needs internet for its styling.
- The web tool adds about 740 KB to the firmware image.

## Build

`vdisk.cmake` adds the module to the `remapper` target on every board. Turn it off with `-DVDISK_ENABLE=OFF`; the firmware is then identical to one without this folder.

| CMake setting | Default | Meaning |
| --- | --- | --- |
| `VDISK_ENABLE` | `ON` | Build the drive into `remapper`. |
| `VDISK_WEB_TOOL` | `ON` | Put the web config tool on the drive. Needs Python 3 at build time. |

`VDISK_EP_OUT` / `VDISK_EP_IN` (`0x04` / `0x84`) in `vdisk.h` are the bulk endpoints. `VDISK_CONFIG_VERSION` in `vdisk.h` must match `CONFIG_VERSION` in `src/config.cc`, and the `op_names` table in `vdisk.cc` must follow the `Op` enum in `src/types.h`.

## Changes to hid-remapper's own files

| File | Change |
| --- | --- |
| `firmware/CMakeLists.txt` | `include(rp2040-vdisk/vdisk.cmake OPTIONAL)` after the `remapper` target. |
| `firmware/src/tusb_config_both/tusb_config.h` | `#ifndef` around `#define CFG_TUD_MSC 0`, so the build can set it to 1. |
| `firmware/src/tinyusb_stuff.cc` | Inside `#ifdef VDISK_ENABLED`: the include, and `tud_descriptor_configuration_cb()` returns `vdisk_configuration_descriptor(...)`. |
| `firmware/src/main.cc` | Inside `#ifdef VDISK_ENABLED`: the include, and `vdisk_task()` after `tud_task()`. |

## How it works

- **USB:** `vdisk_configuration_descriptor()` copies the active configuration descriptor and appends one mass-storage interface. TinyUSB's MSC class calls the `tud_msc_*_cb()` functions in `vdisk.cc`.
- **Volume:** FAT12, 8192 sectors of 512 bytes (4 MB), 4 KB clusters, two FATs, 64 root entries. Cluster 2 is `APPLY`, 3 is `README.TXT`, 4 is `STATUS.TXT`, the web tool starts at cluster 5 and `config.json` follows it.
- **Reading `config.json`:** `generate_json()` writes the JSON through a 512-byte window, so a sector is produced without holding the whole file in RAM. Its length is measured once per mount.
- **Writing:**
  - Sectors written to the `APPLY` directory are scanned for file entries; the first cluster and size of each are remembered.
  - A data sector that starts a cluster with `{` and a quoted key starts the JSON parser. It is fed the following sectors as they arrive.
  - The parser is a character-by-character state machine. It builds the configuration in the firmware's own persisted format, in a few small vectors.
  - When the parsed file's first cluster matches an entry in `APPLY`, `vdisk_task()` assembles the 4 KB image, adds the CRC and passes it to `load_config()`, then requests a save to flash.
  - All other writes are accepted and dropped.
- **Web tool:** `webtool.py` joins `index.html`, the four scripts and Bootstrap into one file (module imports removed, everything inline). `vdisk.cmake` links that file into the `.flashdata` section, so it stays in flash and is read from there; it follows `STATUS.TXT` on the volume and `config.json` follows it.
- **UF2:** a data sector that starts a cluster with the UF2 magic numbers is remembered. When a file in `APPLY` starts at that cluster and writing has stopped for 1.5 seconds, `reset_to_bootloader()` is called.
- **Reload:** after a change, test-unit-ready reports "medium not present" for 2.5 seconds, which makes the host mount the drive again.
