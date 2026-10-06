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
| `dts/` | Device tree changes (for example the SDMMC node) |
| `scripts/` | Rootfs, image and flash scripts |
| `ci/` | QEMU and hardware test automation |

## Status

Phase 1, step 1 (repositories) done. Next: step 2, build and boot the Espressif baseline.
