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
| Userspace ABI | Toolchain `riscv64-esp-linux-musl` GCC 14.1.1; rv32 with M, A, C (no F/D); musl; headers 6.6 | [defconfig][brdefconfig] |
| Peripherals | Developer preview: only the UART console is reliable | [CNX][cnx] |

## Alpine and musl

| Area | Fact | Source |
| --- | --- | --- |
| musl | `riscv32` upstream since 1.2.5 (29 Feb 2024); time64-only ABI | [musl releases](https://musl.libc.org/releases.html) |
| abuild | `riscv32` ⇄ `riscv32-alpine-linux-musl` already mapped | [functions.sh.in](https://github.com/alpinelinux/abuild/blob/master/functions.sh.in) |
| apk-tools | Base arch `riscv32` when built for rv32; edge ships 3.0.8; mbedtls backend available | [apk_arch.h](https://github.com/alpinelinux/apk-tools/blob/master/src/apk_arch.h), [meson_options.txt](https://github.com/alpinelinux/apk-tools/blob/master/meson_options.txt) |
| aports | gcc lacks an rv32 `_arch_configure` case; musl, openssl, binutils handle only `riscv64`; bootstrap.sh libatomic special case is `riscv64` only | [aports](https://github.com/alpinelinux/aports) |
| Alpine | Ships `riscv64` only | aports |

## Community ports (Linux 7.1)

| Feature | Status | Source |
| --- | --- | --- |
| microSD | `dw_mmc` plus an esp32s31 glue patch (internal DMA, non-coherent descriptor ring); FAT32 and ext4 verified on Korvo-1 | [vanbuong][vanbuong], [platima][platima] |
| Native Wi-Fi | Hart 0 runs ESP-IDF as Wi-Fi firmware in M-mode; Linux on hart 1; full-MAC cfg80211 driver `esp32s31-wifi` over shared-SRAM rings; credentials via sysfs. vanbuong: associates; platima: not yet | [vanbuong][vanbuong], [platima][platima] |
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
so the flash window maps offset 0x100000 at 0x40000000 (inferred from these two
numbers; confirm with `CONFIG_XIP_PHYS_ADDR`, expected 0x40400000).

