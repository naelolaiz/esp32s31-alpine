# 2026-10-07: Alpine boots from the stick without the U-Boot prompt (step 14b)

Stopping U-Boot with a key while tapping RST did not always work, so Alpine
now boots by itself when the stick is plugged in. A Buildroot overlay
replaces `/sbin/init` in the flash cramfs with a script that switches to the
stick's root when it holds Alpine, and starts Buildroot's BusyBox init
otherwise. U-Boot, the kernel, the device tree and the kernel command line
are unchanged. Guide:
[`docs/steps/14b-boot-from-stick.md`](../steps/14b-boot-from-stick.md).

## Build

- Script: [`buildroot/rootfs-overlay/sbin/init`](../../buildroot/rootfs-overlay/sbin/init),
  838 bytes, mode 755.
- `BR2_ROOTFS_OVERLAY` got this repository's `buildroot/rootfs-overlay` as a
  second entry, after Espressif's, via `make menuconfig` (System
  configuration → Root filesystem overlay directories). A plain `make`
  repacked the cramfs and `s31_full_flash.bin`; the full image was flashed.
- Before the board, the script's three paths ran on a PC in a mount namespace
  (a tmpfs for the cramfs, a loop device for the stick, static BusyBox
  1.36.1). With an Alpine image the test init ran as process 1 and saw only
  the stick on `/`, devtmpfs on `/dev` and its own `/proc`. An ext4 image
  without `/etc/alpine-release`, and no device at all, both ended in BusyBox
  init with nothing left mounted on `/mnt`; the second after the 5 s wait.

## Boot with the stick

Reset with the stick plugged in, no key pressed. U-Boot used the device
tree's line, so the kernel mounted the cramfs as before:

```
[    0.000000] Kernel command line: earlycon=sbi console=ttyS0 root=mtd:rootfs rootfstype=cramfs ro swiotlb=128
...
[    1.422859] cramfs: linear cramfs image on mtd:rootfs appears to be 3416 KB in size
[    1.431356] VFS: Mounted root (cramfs filesystem) readonly on device 31:0.
[    1.438150] devtmpfs: mounted
...
[    1.452301] Run /sbin/init as init process
[    1.687000] usb 1-1: new high-speed USB device number 2 using dwc2
...
[    3.036081]  sda: sda1
[    3.041954] sd 0:0:0:0: [sda] Attached SCSI removable disk
[    4.611563] EXT4-fs (sda1): mounted filesystem 1c890aa0-c025-4720-895d-58cc71c59e6e r/w with ordered data mode. Quota mode: disabled.
init: Alpine found on /dev/sda1, switching to it

   OpenRC 0.63.2 is starting up Linux 6.18.0 (riscv32)
...
esp32s31 login:
```

The OpenRC part is identical to step 14's second boot. After logging in:

```
esp32s31:~# cat /proc/mounts
/dev/sda1 / ext4 rw,relatime 0 0
devtmpfs /dev devtmpfs rw,nosuid,noexec,relatime,size=10240k,nr_inodes=1838,mode=755 0 0
proc /proc proc rw,nosuid,nodev,noexec,relatime 0 0
tmpfs /run tmpfs rw,nosuid,nodev,size=2948k,nr_inodes=819200,mode=755 0 0
devpts /dev/pts devpts rw,nosuid,noexec,relatime,gid=5,mode=620,ptmxmode=000 0 0
shm /dev/shm tmpfs rw,nosuid,nodev,noexec,relatime 0 0
sysfs /sys sysfs rw,nosuid,nodev,noexec,relatime 0 0
```

- No cramfs in the list: the lazy unmount after `pivot_root` released the
  flash root once the script's shell was replaced by Alpine's init.
- The root shows as `/dev/sda1` instead of step 13's `/dev/root`, because the
  script mounted it by that name, not the kernel.
- No `EXT4-fs (sda1): recovery complete`: the previous session ended with
  `poweroff`.

| Event | Step 13 (`rootwait`) | Step 14b (script) |
| --- | --- | --- |
| init starts | 3.965 s, after the mount | 1.452 s, before the stick appears |
| `sda: sda1` | 3.037 s | 3.036 s |
| ext4 mounted | 3.937 s | 4.612 s |

The stick appears at the same time. The root is mounted 0.67 s later than
with `rootwait`: the script checks `/dev/sda1` once a second (at about 1.45,
2.45 and 3.45 s), so it found the partition about 0.4 s after it appeared,
and the mount itself took longer than in step 13 (inferred from the
timestamps; not measured separately).

## Boot without the stick

Stick unplugged, reset:

```
[    1.451390] Run /sbin/init as init process
init: no Alpine stick, starting Buildroot
```

The message came after the script's five checks, and Enter on the console
gave Buildroot's `#` prompt, so BusyBox init ran Espressif's `inittab` as
before. Both paths work on the board.

## Faster flashing

The BSP's packaging (`tools/gen_esp_flash_image.sh` in esp-linux-bsp) builds
`s31_full_flash.bin` with `esptool merge-bin --format raw`, which pads the
gaps between the five slots with 0xFF; esptool prints a hint about it during
`make`. Two ways to write less:

- `merge-bin --format hex` from the same five files. `write-flash` splits a
  HEX file into its regions and writes only those. With dummy files of about
  the board's file sizes, the regions matched the raw image byte for byte and
  added up to 8.3 MB of the 16.1 MB raw file.
- Flashing only the slot that changed, for example `rootfs.cramfs` at
  0xC00000 after a root file system change, or `xipImage` at 0x500000 after a
  kernel change.

The commands are in step 2's guide,
[`docs/steps/02-baseline.md`](../steps/02-baseline.md).

## Next

Step 15: Ethernet and apk.
