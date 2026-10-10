# Ground truth

Facts checked on 2026-10-06 against the sources linked below. Anything not
checked is marked "inferred". Update a row, with its date and source, when
something changes.

## Chip and Espressif BSP

| Area | Fact | Source |
| --- | --- | --- |
| MMU | Sv32 paging: normal MMU Linux and normal ELF userspace (no nommu, no FDPIC) | [GrieferPig port][grieferpig], [CNX][cnx] |
| Boot chain | ROM, U-Boot SPL, OpenSBI 1.6, U-Boot 2024.07 (FIT), Linux 6.18 | [defconfig][brdefconfig] |
| Kernel | `esp32s31_minimal`, XIP kernel at 0x40400000, RAM base 0x50000000, size-optimized | [kernel defconfig][kdefconfig] |
| Kernel gaps | `CONFIG_BLOCK` off, IPv6 off, seccomp off, no VT, no input; unaligned access trap-emulated | [kernel defconfig][kdefconfig] |
| Kernel drivers | UART, stmmac Ethernet + Motorcomm PHY, I2C, GPIO, pinctrl, USB DWC2 host + PHY, MTD physmap, cramfs, tmpfs, devtmpfs | [kernel defconfig][kdefconfig] |
| RAM | 16 MB PSRAM; about 12.7 MB free at runtime on the community port | [GrieferPig port][grieferpig] |
| Flash | 16 MiB NOR; rootfs slot at 0xC00000, 4 MiB, compressed cramfs mounted from the XIP window | [rootfs-cramfs.sh][cramfs] |
| Userspace ABI | Toolchain `riscv64-esp-linux-musl` GCC 14.1.1; userspace built rv32 with M, A, C, `ilp32` soft-float; musl; headers 6.6 | [defconfig][brdefconfig] |
| F extension | Likely present, without D (checked 2026-10-07, not yet on the board): Espressif's ESP32-S31 Wi-Fi libraries are `rv32imafc` with the single-float ABI; the kernel device tree lists no `f`, so `/proc/cpuinfo` cannot show it; stock Linux drops F when D is missing | [esp32-wifi-lib](https://github.com/espressif/esp32-wifi-lib) af55a0c, [step 19 journal](journal/2026-10-07-native-wifi-study.md) |
| Hart 1 | Unused by Linux (one `cpu@0`, no `CONFIG_SMP`; on the board `cpu/possible` is `0`). Espressif's OpenSBI can start it since 2026-10-06 (SBI HSM device, `hsm.c`); the build on our board predates that (no `esp32s31-hsm` in `u-boot.itb`, step 19) | [espressif/opensbi](https://github.com/espressif/opensbi/tree/integration/v1.6-esp32s31) 5395e03 |
| Internal SRAM | 512 KiB at 0x2F000000, not covered by the data cache (Espressif's `drivers/cache/esp32s31_cache.c`). OpenSBI 1.6: code 0x2F000000-0x2F01A900, data and BSS 0x2F020000-0x2F023B88, then stacks and heap (`readelf` of `fw_dynamic.elf`, step 19); it prints no banner. Kernel DMA pool 0x2F030000-0x2F070000 (256 KiB, `shared-dma-pool`), the only reservation; step 20 moves it to 0x2F073000 (20 KiB). U-Boot copies the DTB to the top of PSRAM (0x50FFB000) | OpenSBI `config.h`, `objects.mk`; kernel `esp32s31.dts` dc0e382; [step 19 board checks](journal/2026-10-10-native-wifi-board-checks.md) |
| Peripherals | Developer preview: only the UART console is reliable | [CNX][cnx] |

## Alpine and musl

| Area | Fact | Source |
| --- | --- | --- |
| musl | `riscv32` upstream since 1.2.5 (29 Feb 2024); time64-only ABI | [musl releases](https://musl.libc.org/releases.html) |
| abuild | `riscv32` ⇄ `riscv32-alpine-linux-musl` already mapped | [functions.sh.in](https://github.com/alpinelinux/abuild/blob/master/functions.sh.in) |
| apk-tools | Base arch `riscv32` when built for rv32; edge ships 3.0.8; mbedtls backend available | [apk_arch.h](https://github.com/alpinelinux/apk-tools/blob/master/src/apk_arch.h), [meson_options.txt](https://github.com/alpinelinux/apk-tools/blob/master/meson_options.txt) |
| aports | gcc lacks an rv32 `_arch_configure` case; musl, openssl, binutils handle only `riscv64`; bootstrap.sh libatomic special case is `riscv64` only | [aports](https://github.com/alpinelinux/aports) |
| Alpine | Ships `riscv64` only | aports |

## Community ports

vanbuong and platima build upstream Linux 7.1.3 and 7.2.7 with their own
ESP-IDF loader and OpenSBI 1.9; GrieferPig builds a 6.18 kernel whose first
commits are Espressif's, on SPL, U-Boot 2024.07 and an OpenSBI 1.9 fork
(checked 2026-10-07, step 19 journal).

| Feature | Status | Source |
| --- | --- | --- |
| microSD | `dw_mmc` plus an esp32s31 glue patch (internal DMA, non-coherent descriptor ring); FAT32 and ext4 verified on Korvo-1 | [vanbuong][vanbuong], [platima][platima] |
| Native Wi-Fi, design A | Hart 0 runs ESP-IDF as Wi-Fi firmware in M-mode; Linux on hart 1; full-MAC cfg80211 driver `esp32s31-wifi` over SRAM rings at 0x2F050000. vanbuong: keys via sysfs; platima: keys via `wpa_supplicant`, README lists Wi-Fi as working | [vanbuong][vanbuong], [platima][platima] |
| Native Wi-Fi, design B | ESP-IDF Wi-Fi libraries prelinked from flash and run inside Linux (S-mode) with a FreeRTOS emulation; soft-MAC mac80211 driver, plain `wpa_supplicant`, WPA2-CCMP station only; about 282 KiB SRAM | [GrieferPig][grieferpig] |
| USB storage | vanbuong: works; platima: disabled because it hangs boot | [vanbuong][vanbuong], [platima][platima] |

[cnx]: https://www.cnx-software.com/2026/08/22/espressif-systems-releases-a-linux-bsp-developer-preview-for-esp32-s31-risc-v-microprocessor/
[grieferpig]: https://github.com/GrieferPig/esp32-s31-linux
[brdefconfig]: https://github.com/espressif/esp-buildroot-external/blob/buildroot/v2025.02-esp32s31/configs/espressif_esp32s31_function_core_board_nor_defconfig
[kdefconfig]: https://github.com/espressif/linux/blob/integration/v6.18-esp32s31/arch/riscv/configs/esp32s31_minimal_defconfig
[cramfs]: https://github.com/espressif/esp-buildroot-external/blob/buildroot/v2025.02-esp32s31/board/espressif/esp32s31/rootfs-cramfs.sh
[vanbuong]: https://github.com/vanbuong/esp32-s31-linux
[platima]: https://github.com/platima/esp32-s31-linux

## Flash layout

Defined in esp-linux-bsp `tools/gen_esp_flash_image.sh` (branch
`integration/v1.0-esp32s31`, 22455bf), which `esptool merge-bin` turns into
`s31_full_flash.bin`. Each slot can be overridden with a `SLOT_*` variable.
esptool refuses to merge overlapping regions, so a kernel that outgrows its
slot fails the build instead of silently overwriting the rootfs.

| Slot | Flash offset | Size | Content |
| --- | --- | --- | --- |
| SPL | 0x002000 | to 0x100000 | `spl_app.bin`, U-Boot SPL as an ESP app image |
| U-Boot | 0x100000 | 2 MiB | `u-boot.itb` (FIT: OpenSBI, U-Boot, its DTB) |
| DTB | 0x300000 | 2 MiB | Linux device tree |
| Kernel | 0x500000 | 7 MiB | `xipImage`, executed in place |
| Rootfs | 0xC00000 | 4 MiB | `rootfs.cramfs` |

The rootfs at flash offset 0xC00000 appears at 0x40b00000 in Linux (physmap),
so the flash window maps offset 0x100000 at 0x40000000. Verified: the kernel
config has `CONFIG_XIP_PHYS_ADDR=0x40400000`, which is the kernel slot (0x500000)
through the same mapping.

