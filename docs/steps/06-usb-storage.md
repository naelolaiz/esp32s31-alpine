# Step 6: USB pendrives

Goal: read and write a USB stick on the board's USB-A port under the Buildroot
shell. Step 3 showed the DWC2 host enumerates devices, so this step only adds
kernel options (SCSI disk, USB mass storage, MBR and GPT partitions, exFAT, UTF-8).

This step runs before step 5 (microSD) because it needs no wiring.

## 1. Turn the options on by hand

All commands run from the Buildroot output directory of the Espressif build
(the one with `images/` and `build/` in it).

```sh
make linux-menuconfig
```

In the menu, `/` searches an option by name (without `CONFIG_`) and shows its
location and dependencies; space toggles `[*]`.

| Option | Menu location | What it does |
| --- | --- | --- |
| `SCSI` | Device Drivers → SCSI device support → SCSI device support | USB sticks speak SCSI commands over USB. Depends on the block layer (step 4) |
| `BLK_DEV_SD` | same menu → SCSI disk support | disk driver behind `/dev/sda`, `/dev/sda1` |
| `USB_STORAGE` | Device Drivers → USB support → USB Mass Storage support | bridges USB to SCSI; visible only once `SCSI` is on |
| `EXFAT_FS` | File systems → DOS/FAT/EXFAT/NT Filesystems → exFAT filesystem support | large sticks ship formatted exFAT |
| `NLS_UTF8` | File systems → Native language support → NLS UTF-8 | non-ASCII file names |

`MSDOS_PARTITION` and `EFI_PARTITION` let the kernel read the stick's
partition table. They do not appear in the menu: their prompt has
`if PARTITION_ADVANCED`, which only hides it, while `default y` still applies.
Leave `PARTITION_ADVANCED` off; the `.config` check below shows both as `y`.
`BLK_DEV_INITRD` (General setup) comes back on after you save: Buildroot's
kernel fixups in `linux/linux.mk` force it when a cpio rootfs image is
enabled. `linux-diff-config` compares against the config without those
fixups, so it reports it as a change. Leave it for now. Save on exit.

Check, rebuild, and see the change against Espressif's defconfig:

```sh
grep -E '^CONFIG_(SCSI|BLK_DEV_SD|USB_STORAGE|EXFAT_FS|NLS_UTF8|MSDOS_PARTITION|EFI_PARTITION|PARTITION_ADVANCED)=' build/linux-integration_v6.18-esp32s31/.config
make linux-rebuild all
make linux-diff-config
```

`linux-rebuild` recompiles the kernel; `all` repacks `images/s31_full_flash.bin`.
menuconfig changes live only in the build directory and are lost on
`linux-dirclean` or `linux-reconfigure`, so the `linux-diff-config` output is
what gets kept in this repository as
[kernel/fragments/20-usb-storage.config](../../kernel/fragments/20-usb-storage.config).
[`scripts/build-kernel.sh`](../../scripts/build-kernel.sh) applies the stored
fragments, for rebuilding from scratch once the manual path is understood.

Flash and boot as in step 4. Check that `uname -v` shows the new build time.

One community port reported boot hangs with USB storage enabled. If the boot
stops, unplug the stick, press RST, and record where the log stopped.

## 2. Plug a stick and mount it

Use a stick formatted as FAT32 or exFAT with nothing you need on it. Boot
first, then plug it in:

```sh
dmesg | tail -20
cat /proc/partitions
mkdir -p /mnt/usb
mount /dev/sda1 /mnt/usb
mount | grep sda
```

If the stick has no partition table, use `/dev/sda` instead of `/dev/sda1`.

## 3. Write and read back

```sh
dd if=/dev/urandom of=/mnt/usb/board.bin bs=1024 count=1024
sha256sum /mnt/usb/board.bin
umount /mnt/usb
mount /dev/sda1 /mnt/usb
sha256sum /mnt/usb/board.bin
umount /mnt/usb
free
```

Both checksums must match. Then unplug the stick, plug it into the PC, open
a terminal in the stick's folder and run `sha256sum board.bin`: it must match too.

Done when: the checksums match on the board and on the PC, and the RAM cost is
recorded. Also note the size and RAM cost against step 4: 3,631,269 B
`xipImage`, 11056 KiB available.
