# Espressif BSP versions

Espressif's Linux BSP is a developer preview with breaking changes, so we build
from pinned commits. Fill in the commit column in step 2 and update it only on
purpose, with a journal entry.

| Component | Repository | Branch | Commit |
| --- | --- | --- | --- |
| Buildroot external | [espressif/esp-buildroot-external](https://github.com/espressif/esp-buildroot-external) | `buildroot/v2025.02-esp32s31` | TBD |
| BSP tools | [espressif/esp-linux-bsp](https://github.com/espressif/esp-linux-bsp) | `integration/v1.0-esp32s31` | TBD |
| Linux | [espressif/linux](https://github.com/espressif/linux) | `integration/v6.18-esp32s31` | TBD |
| U-Boot | [espressif/u-boot](https://github.com/espressif/u-boot) | `integration/v2024.07-esp32s31` | TBD |
| OpenSBI | [espressif/opensbi](https://github.com/espressif/opensbi) | `integration/v1.6-esp32s31` | TBD |
| Toolchain | dl.espressif.com | `riscv64-esp-linux-musl` 14.1.1_20260922 | n/a |

Defconfig: `espressif_esp32s31_function_core_board_nor_defconfig`.
Flash tool: esptool 5.3.0 or newer. Console: 115200 baud.
