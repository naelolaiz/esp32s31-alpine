# 2026-10-06: Plan review and repositories

## What happened

- Reviewed the preliminary plan (27 phases, Alpine userspace on Espressif's
  existing boot stack) against Espressif's BSP sources and Alpine's trees.
  Findings are in [ground-truth.md](../ground-truth.md); the result is
  [plan.md](../plan.md).
- Board confirmed: Function-CoreBoard-1 V1.0, the board Espressif's Buildroot
  defconfig targets.
- Created this repository and alpine-riscv32 (plan step 1).

## Decisions

- Kernel config changes and driver patches are allowed; the boot stack stays
  as shipped until Phase 5.
- microSD, USB pendrives and Wi-Fi are wanted as early as dependencies allow,
  step by step. Order: storage in the kernel (Phase 2) before Alpine boots on
  the board, Wi-Fi after Alpine is stable.
- ABI: rv32imac, ilp32, soft-float, musl, matching Espressif's toolchain.
- Generic Alpine patches live in alpine-riscv32; board work lives here.

## What surprised us

- Espressif's kernel has no block layer, so the original plan's SD, ext4,
  squashfs, overlayfs-on-disk and zram phases could not work as written.
- The rootfs slot is a 4 MiB compressed cramfs, too small for a comfortable
  Alpine base; microSD removes the limit.
- abuild and apk-tools already know `riscv32`; less tooling work than expected.

## Next

Step 2: build `espressif_esp32s31_function_core_board_nor_defconfig`, flash,
boot, and record the baseline numbers in a new journal entry.
