# 2026-10-06: USB pendrives (step 6)

Enabled USB mass storage by hand with `make linux-menuconfig` (see the
[step 6 guide](../steps/06-usb-storage.md)), compared the result with
`make linux-diff-config`, rebuilt with `make linux-rebuild all`, and tested a
62 GB Kingston DataTraveler 3.0 on the USB-A port.

## Options

`SCSI`, `BLK_DEV_SD`, `USB_STORAGE`, `EXFAT_FS`, `NLS_UTF8`; stored as
[kernel/fragments/20-usb-storage.config](../../kernel/fragments/20-usb-storage.config).

Lessons from doing it by hand:

- `MSDOS_PARTITION` and `EFI_PARTITION` have no menu entry unless
  `PARTITION_ADVANCED` is on: their prompt is `if PARTITION_ADVANCED`, but
  `default y` still applies, so they are on anyway. Hidden is not disabled;
  the `.config` is the reliable check.
- `BLK_DEV_INITRD` comes back after disabling it: Buildroot's kernel fixups
  (`linux/linux.mk`, line 460 in 2025.02) force it because the defconfig sets
  `BR2_TARGET_ROOTFS_CPIO=y`. The same mechanism forces `DEVTMPFS`.
- `linux-diff-config` compares against defconfig plus fragments without
  Buildroot's fixups, so forced options show up as changes.
- The root is a read-only cramfs: mount points must already exist (`/mnt`).

## What the log shows

Each layer of the stack in order: `dwc2` enumerates the stick at high speed,
`usb-storage` claims it and registers `scsi host0`, the SCSI layer identifies a
Direct-Access device, `sd` creates `/dev/sda` (121,077,760 blocks of 512 B),
and the partition code finds `sda1 sda2`. The stick is a Ventoy stick:
`sda1` is the exFAT data partition, `sda2` Ventoy's 32 MiB EFI partition.

## Test

`mount -t exfat /dev/sda1 /mnt`, 1 MiB from `/dev/urandom` to `board.bin`,
`sha256sum`, unmount, remount, `sha256sum` again: both
`b109b263a50fdcb30262d6843ae708864c460b0e3820ea093fd8756f0229c5af`.
Unmounting drops the cached pages, so the second read came from the stick.

## Cost

| Item | Step 4 | Step 6 | Change |
| --- | --- | --- | --- |
| `xipImage` (flash) | 3,631,269 B | 3,828,085 B | +196,816 B (+192 KiB) |
| `free` available, after boot, no stick | 11056 KiB | 11128 KiB | +72 KiB (boot-to-boot noise) |
| `free` available, stick plugged in | n/a | 11016 KiB | -112 KiB against no stick |

So USB storage costs flash but no measurable RAM until a stick is plugged in,
and about 112 KiB while one is (SCSI host, `usb-storage` thread, request queue
for `/dev/sda`). A `free` taken right after the mount/write/unmount test showed
9348 KiB available: about 1.7 MiB more is held after using a filesystem, most
likely in slab caches (inferred, not measured).

The kernel slot is 7 MiB (flash layout in [ground-truth.md](../ground-truth.md)),
so 3.65 MiB used leaves room for microSD.

## Next

Step 5: microSD, which needs a 3.3 V breakout wired to GPIO20 to 25.
