# Kernel

Changes on top of Espressif's `integration/v6.18-esp32s31` kernel
(`arch/riscv/configs/esp32s31_minimal_defconfig`). Pinned commit: see
[../bsp/versions.md](../bsp/versions.md).

- `fragments/`: Kconfig fragments, one per feature, applied in name order with
  `scripts/kconfig/merge_config.sh`. Planned: `10-block.config` (step 4),
  `20-mmc.config` (step 5), `30-usb-storage.config` (step 6).
- `patches/`: `git format-patch` output against the pinned commit, numbered in
  apply order, for Buildroot's `BR2_LINUX_KERNEL_PATCH`:
  - `0001-riscv-dts-esp32s31-shrink-the-DMA-pool.patch`: DMA pool from
    256 KiB at 0x2F030000 to 20 KiB at 0x2F073000, so the Wi-Fi radio gets
    its SRAM (step 20).
  Planned: the dw_mmc esp32s31 glue port (step 5, parked).

Each fragment or patch gets a journal entry recording its size and RAM cost.
