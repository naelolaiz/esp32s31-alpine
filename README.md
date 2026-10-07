# esp32s31-alpine

Alpine Linux (`riscv32`, musl, OpenRC, apk) on the Espressif
[ESP32-S31 Function-CoreBoard-1](docs/hardware/function-coreboard-1.md),
booted by Espressif's Linux BSP (U-Boot SPL, OpenSBI, U-Boot, Linux 6.18).

This repository holds everything specific to the board. Generic Alpine
`riscv32` work lives in the sibling `alpine-riscv32` repository,
so it stays upstreamable.

It is also a learning log: every step's notes go into [`docs/journal/`](docs/journal/)
as the work happens.

## Where to start

- [Plan](docs/plan.md): 24 steps in six phases, in dependency order.
- [Ground truth](docs/ground-truth.md): verified facts about the chip, the BSP and Alpine, with sources.
- [Journal](docs/journal/): dated notes, newest last.
- [BSP versions](bsp/versions.md): the Espressif branches and commits we build from.

## Layout

| Path | Holds |
| --- | --- |
| `docs/` | Plan, ground truth, hardware notes, journal |
| `bsp/` | Pinned Espressif BSP versions and how to build the baseline |
| `kernel/fragments/` | Kconfig fragments applied on top of `esp32s31_minimal_defconfig` |
| `kernel/patches/` | Patches against Espressif's `integration/v6.18-esp32s31` kernel |
| `buildroot/rootfs-overlay/` | Files added to Espressif's Buildroot root file system (the `/sbin/init` that boots Alpine from the stick) |
| `dts/` | Device tree changes (for example the SDMMC node) |
| `scripts/` | Rootfs, image and flash scripts |
| `ci/` | QEMU and hardware test automation |

## Status

Phase 1 done. Steps 4 and 6 done: our own kernel with the block layer, ext4, vfat, exFAT and USB pendrives. Steps 7 to 11 done: riscv32 cross toolchain, the Alpine base system built from our own repository, Alpine riscv32 booting to an OpenRC login in qemu-system-riscv32 with Linux 6.18, and a native riscv32 build container that built nano and dropbear, with SSH logins working in the VM ([journal](docs/journal/)). Step 5 (microSD) is parked. Step 12 done: Alpine's static and dynamic BusyBox run on the board under Buildroot. Step 13 done: Espressif's kernel boots Alpine's root file system from an ext4 partition on a USB stick, chosen at the U-Boot prompt, with Alpine's shell as process 1. Step 14 done: OpenRC boots Alpine on the board to a login prompt on the serial console, with about 9.6 MiB of RAM available. Step 14b done: with the stick plugged in, a reset boots Alpine by itself; without it, Buildroot. Step 15 done: Ethernet comes up at boot with DHCP, and `apk add nano` installs from our repository, served by the PC over HTTP, with the signature checked. Next: step 16, pendrive hotplug.
