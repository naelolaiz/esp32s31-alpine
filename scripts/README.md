# Scripts

Planned, added as the steps that need them land:

- `build-baseline.sh`: build the pinned Espressif Buildroot image (step 2).
- `build-kernel.sh`: Espressif kernel plus `kernel/fragments` and `kernel/patches` (step 4).
- `build-rootfs.sh`: Alpine rootfs from a local riscv32 repository with
  `apk --root --arch riscv32 --initdb`; outputs an SD image and a rescue cramfs (steps 13, 23).
- `build-esp32s31-image.sh`: full flash image, reusing esp-linux-bsp packaging (step 23).
- `flash.sh`: esptool wrapper (step 2).
