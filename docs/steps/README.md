# Steps

The [plan](../plan.md) has 24 steps in six phases. The guides are split
between two repositories:

- This repository holds the steps that need the board.
- [alpine-riscv32](https://github.com/naelolaiz/alpine-riscv32) holds steps 7
  to 11. That work is generic riscv32 Alpine, with no board involved, and is
  kept apart so it can go upstream to Alpine on its own.

A step with no guide was either short enough that its
[journal](../journal/) entry is the whole record, or has not started yet.

| Step | What | Guide | Journal | State |
| --- | --- | --- | --- | --- |
| 1 | Repositories | none (the repositories themselves) | [plan review](../journal/2026-10-06-plan-review.md) | done |
| 2 | Espressif baseline | [02-baseline.md](02-baseline.md) | [baseline](../journal/2026-10-06-baseline.md) | done |
| 3 | Probe Ethernet and USB | none (a few commands) | [probe](../journal/2026-10-06-probe-ethernet-usb.md) | done |
| 4 | First own kernel | [04-first-kernel.md](04-first-kernel.md) | [first kernel](../journal/2026-10-06-first-kernel.md) | done |
| 5 | microSD | none | none | parked: no 3.3 V breakout |
| 6 | USB pendrives | [06-usb-storage.md](06-usb-storage.md) | [USB storage](../journal/2026-10-06-usb-storage.md) | done |
| 7 | Patch aports | alpine-riscv32: [07-build-environment.md](https://github.com/naelolaiz/alpine-riscv32/blob/main/docs/steps/07-build-environment.md), [07-aports-edits.md](https://github.com/naelolaiz/alpine-riscv32/blob/main/docs/steps/07-aports-edits.md) | [cross toolchain](../journal/2026-10-06-cross-toolchain.md) | done |
| 8 | Cross toolchain | alpine-riscv32: [08-cross-toolchain.md](https://github.com/naelolaiz/alpine-riscv32/blob/main/docs/steps/08-cross-toolchain.md) | [cross toolchain](../journal/2026-10-06-cross-toolchain.md) | done |
| 9 | Base repository | alpine-riscv32: [09-base-system.md](https://github.com/naelolaiz/alpine-riscv32/blob/main/docs/steps/09-base-system.md) | [base system](../journal/2026-10-07-base-system.md) | done |
| 10 | Full-system QEMU | alpine-riscv32: [10-qemu-system.md](https://github.com/naelolaiz/alpine-riscv32/blob/main/docs/steps/10-qemu-system.md) | [QEMU boot](../journal/2026-10-07-qemu-boot.md) | done |
| 11 | Native builds | alpine-riscv32: [11-native-build.md](https://github.com/naelolaiz/alpine-riscv32/blob/main/docs/steps/11-native-build.md) | [native build](../journal/2026-10-07-native-build.md) | done |
| 12 | Alpine binaries on Buildroot | [12-alpine-binaries.md](12-alpine-binaries.md) | [Alpine binaries](../journal/2026-10-07-alpine-binaries.md) | done |
| 13 | Alpine root on a pendrive | [13-alpine-root-usb.md](13-alpine-root-usb.md) | [root on USB](../journal/2026-10-07-alpine-root-usb.md) | done |
| 14 | OpenRC | [14-openrc.md](14-openrc.md) | [OpenRC](../journal/2026-10-07-openrc.md) | done |
| 14b | Boot from the stick automatically | [14b-boot-from-stick.md](14b-boot-from-stick.md) | [boot from stick](../journal/2026-10-07-boot-from-stick.md) | done |
| 15 | Network and apk | [15-network-apk.md](15-network-apk.md) | [network and apk](../journal/2026-10-07-network-apk.md) | done |
| 16 | Pendrive hotplug | | | next |
| 17 | SSH | | | planned |
| 18 | Optional early Wi-Fi: USB dongle | | | planned |
| 19 | Study the native Wi-Fi design | [19-native-wifi-study.md](19-native-wifi-study.md) | [native Wi-Fi study](../journal/2026-10-07-native-wifi-study.md), [board checks](../journal/2026-10-10-native-wifi-board-checks.md) | done |
| 20 | SRAM and FPU for the radio | [20-sram-and-fpu.md](20-sram-and-fpu.md) (part 1: DMA pool) | [DMA pool](../journal/2026-10-10-dma-pool.md) | DMA pool done; FPU next |
| 21 | Radio driver | | | planned |
| 22 | Alpine Wi-Fi | | | planned |
| 23 | Reproducible builds | | | planned |
| 24 | Upstream | | | planned |

All journal entries are in this repository, the ones for steps 7 to 11
included.
