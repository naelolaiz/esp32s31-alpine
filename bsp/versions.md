# Espressif BSP versions

Espressif's Linux BSP is a developer preview with breaking changes, so we build
from pinned commits. Fill in the commit column in step 2 and update it only on
purpose, with a journal entry.

| Component | Repository | Branch | Commit |
| --- | --- | --- | --- |
| Buildroot external | [espressif/esp-buildroot-external](https://github.com/espressif/esp-buildroot-external) | `buildroot/v2025.02-esp32s31` | fb8dca1 |
| BSP tools | [espressif/esp-linux-bsp](https://github.com/espressif/esp-linux-bsp) | `integration/v1.0-esp32s31` | TBD |
| Linux | [espressif/linux](https://github.com/espressif/linux) | `integration/v6.18-esp32s31` | TBD |
| U-Boot | [espressif/u-boot](https://github.com/espressif/u-boot) | `integration/v2024.07-esp32s31` | TBD |
| OpenSBI | [espressif/opensbi](https://github.com/espressif/opensbi) | `integration/v1.6-esp32s31` | TBD |
| Buildroot | [buildroot.org](https://gitlab.com/buildroot.org/buildroot) | `2025.02` | aa2d7ca5 |
| Toolchain | dl.espressif.com | `riscv64-esp-linux-musl` 14.1.1_20260922 | n/a |

Defconfig: `espressif_esp32s31_function_core_board_nor_defconfig`.
Flash tool: esptool 5.3.0 or newer. Console: 115200 baud.

## Baseline build (2026-10-06)

First baseline built locally on 2026-10-06.
Linux, U-Boot and OpenSBI were fetched by branch name; their exact commits are
still TBD (read them from Buildroot's `dl/*/git` caches).

| Artifact | Size | Note |
| --- | --- | --- |
| `s31_full_flash.bin` | 16,080,896 B (15.3 MiB) | full 16 MiB NOR image |
| `rootfs.cramfs` | 3,497,984 B (3.34 MiB) | 83% of the 4 MiB slot; 0.66 MiB free |
| `xipImage` | 3,086,101 B (2.94 MiB) | XIP kernel |
