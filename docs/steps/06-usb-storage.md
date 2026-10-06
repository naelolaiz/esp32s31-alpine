# Step 6: USB pendrives

Goal: read and write a USB stick on the board's USB-A port under the Buildroot
shell. Step 3 showed the DWC2 host enumerates devices, so this step only adds
kernel options: [kernel/fragments/20-usb-storage.config](../../kernel/fragments/20-usb-storage.config)
(SCSI disk, USB mass storage, MBR and GPT partitions, exFAT, UTF-8).

This step runs before step 5 (microSD) because it needs no wiring.

## 1. Rebuild and flash

Update this repository, then rebuild exactly as in step 4. The script picks up
every fragment in `kernel/fragments/` and prints `ok` or `MISSING` for each option:

```sh
git pull
scripts/build-kernel.sh <buildroot-dir> <buildroot-output-dir>
```

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
