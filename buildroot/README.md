# Buildroot

Changes to Espressif's Buildroot image, the root file system in flash.

- `rootfs-overlay/`: a Buildroot overlay, added after Espressif's own in
  `BR2_ROOTFS_OVERLAY`. Its `sbin/init` boots Alpine from the USB stick when
  one is plugged in, otherwise Buildroot's BusyBox init
  ([step 14b](../docs/steps/14b-boot-from-stick.md)).
