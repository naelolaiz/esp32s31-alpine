# 2026-10-07: Alpine's root file system on a USB stick (step 13)

The board booted Espressif's kernel with Alpine's root file system on an ext4
partition of a USB stick and ran Alpine's `/bin/sh` as process 1. Nothing was
rebuilt or flashed. Guide:
[`docs/steps/13-alpine-root-usb.md`](../steps/13-alpine-root-usb.md).

## Choosing the root at the U-Boot prompt

The kernel's command line comes from the device tree's `/chosen/bootargs`.
U-Boot's `booti` copies the device tree into RAM and, in `fdt_chosen()`
(`boot/fdt_support.c`), replaces `bootargs` with U-Boot's environment
variable of the same name when it exists. The environment is never saved
(`Loading Environment from nowhere... OK`), so a `setenv` lasts until the
next reset, and a reset boots Buildroot's cramfs: the rescue system needs no
reflashing.

`CONFIG_BOOTDELAY=0` makes U-Boot check the UART for a key once
(`Hit any key to stop autoboot:  0`). Holding the space bar while tapping RST
reached the `esp32s31>` prompt.

First test, on Buildroot's root, with `init=/bin/sh` added to Espressif's line:

```
esp32s31> printenv bootargs
## Error: "bootargs" not defined
esp32s31> setenv bootargs earlycon=sbi console=ttyS0 root=mtd:rootfs rootfstype=cramfs ro swiotlb=128 init=/bin/sh
esp32s31> booti 0x40400000 - 0x40200000
...
[    0.000000] Kernel command line: earlycon=sbi console=ttyS0 root=mtd:rootfs rootfstype=cramfs ro swiotlb=128 init=/bin/sh
...
[    1.452594] Run /bin/sh as init process
/bin/sh: can't access tty; job control turned off
~ # mount -t proc proc /proc
~ # cat /proc/cmdline
earlycon=sbi console=ttyS0 root=mtd:rootfs rootfstype=cramfs ro swiotlb=128 init=/bin/sh
~ # echo $$
1
```

- U-Boot's variable replaced the device tree's line, and the shell ran as
  process 1 with no `/proc` until mounted by hand.
- `software IO TLB: mapped [mem 0x50faa000-0x50fea000]` is 0x40000 bytes,
  the 256 KiB that `swiotlb=128` asks for (128 slots of 2 KiB; the default
  would be 64 MiB).
- The USB stick, plugged in at boot, enumerated at 1.68 s, after the root
  was already mounted at 1.43 s. A USB root needs `rootwait`.

## The stick

The only stick at hand was the 64 GB Kingston DataTraveler 3.0 used since
step 6, a Ventoy stick (exFAT `sda1` filling the stick, 32 MiB `sda2`). Linux
cannot shrink exFAT, so the stick was wiped:

```
$ sudo wipefs -a /dev/sda1 /dev/sda2 /dev/sda
/dev/sda1: 8 bytes were erased at offset 0x00000003 (exfat): 45 58 46 41 54 20 20 20
wipefs: /dev/sda1: ignoring nested "dos" partition table on non-whole disk device
wipefs: Use the --force option to force erase.
/dev/sda2: 8 bytes were erased at offset 0x00000036 (vfat): 46 41 54 31 36 20 20 20
...
/dev/sda: 2 bytes were erased at offset 0x000001fe (dos): 55 aa
$ printf 'label: dos\nsize=2GiB, type=83\n' | sudo sfdisk /dev/sda
...
Device     Boot Start     End Sectors  Size Id Type
/dev/sda1        2048 4196351 4194304    2G 83 Linux
$ sudo mkfs.ext4 -L alpine-root -E lazy_itable_init=0,lazy_journal_init=0 /dev/sda1
mke2fs 1.47.4 (6-Mar-2025)
/dev/sda1 contains `DOS/MBR boot sector' data
Proceed anyway? (y,N) y
Creating filesystem with 524288 4k blocks and 131072 inodes
...
Creating journal (16384 blocks): done
```

- An exFAT boot sector ends with `55 aa`, the MBR marker, so wipefs saw a
  "nested dos partition table" in `sda1` and kept it without `--force`.
  The new `sda1` starts at the same sector 2048, so mkfs found that leftover
  sector. It is harmless: ext4's superblock starts at byte 1024, and the
  kernel reads partition tables only from whole disks.
- Pasted together with the lines before it, the first mkfs took the next
  pasted line as the answer to `Proceed anyway?` and stopped. Run alone, it
  went through.
- The `-E` options zeroed the inode tables and the 64 MiB journal on the PC,
  so the board's `ext4lazyinit` thread has nothing to do after the first
  mount.

## The root tree

In the x86_64 container, as root, a new tree with the local signing key:

```
/work # apk add --root /var/tmp/alpine-board-root --initdb --arch riscv32 --repository /work/.local/share/abuild/main alpine-base
...
(25/25) Installing alpine-base (3.25.0_alpha20260805-r0)
Executing busybox-1.38.0-r7.trigger
OK: 7696 KiB in 25 packages
/work # du -sh /var/tmp/alpine-board-root
8.8M    /var/tmp/alpine-board-root
```

On the PC, streamed onto the mounted partition with
`podman exec -u root alpine-rv32 tar -C /var/tmp/alpine-board-root -cf - . | sudo tar -C /mnt/alpine-root --numeric-owner -xpf -`:

```
-rwxr-xr-x 1 0 0 832700 ... /mnt/alpine-root/bin/busybox
lrwxrwxrwx 1 0 0     12 ... /mnt/alpine-root/sbin/init -> /bin/busybox
3.25.0_alpha20260805
/dev/sda1       2.0G  9.4M  1.8G   1% /mnt/alpine-root
```

## The boot

```
esp32s31> setenv bootargs earlycon=sbi console=ttyS0 root=/dev/sda1 rootfstype=ext4 rootwait rw swiotlb=128 init=/bin/sh
esp32s31> booti 0x40400000 - 0x40200000
```

| Time | Kernel line |
| --- | --- |
| 1.420 | `Waiting for root device /dev/sda1...` |
| 1.679 | `usb 1-1: new high-speed USB device number 2 using dwc2` |
| 1.925 | `scsi host0: usb-storage 1-1:1.0` |
| 2.947 | `scsi 0:0:0:0: Direct-Access Kingston DataTraveler 3.0` |
| 3.037 | ` sda: sda1` |
| 3.937 | `EXT4-fs (sda1): mounted filesystem ... r/w with ordered data mode` |
| 3.940 | `VFS: Mounted root (ext4 filesystem) on device 8:1.` |
| 3.965 | `Run /bin/sh as init process` |

- The kernel first looked for the root at 1.42 s, more than a second and a
  half before `sda1` existed. Without `rootwait` it would have stopped with
  `VFS: Unable to mount root fs` (`init/do_mounts.c`).
- The one-second gap between `scsi host0` and the disk is `usb-storage`'s
  `delay_use` (1 s by default) before it scans a new device.
- `8:1`: major 8 is the SCSI disk driver, minor 1 the first partition.

In Alpine's shell:

```
~ # cat /etc/alpine-release
3.25.0_alpha20260805
~ # mount -t proc proc /proc
~ # cat /proc/mounts
/dev/root / ext4 rw,relatime 0 0
devtmpfs /dev devtmpfs rw,relatime,size=7352k,nr_inodes=1838,mode=755 0 0
proc /proc proc rw,relatime 0 0
~ # /lib/ld-musl-riscv32-sf.so.1 2>&1 | head -2
musl libc (riscv32-sf)
Version 1.2.6
~ # apk --version
apk-tools 3.0.8-r0, compiled for riscv32.
~ # apk info | wc -l
25
~ # free
              total        used        free      shared  buff/cache   available
Mem:          14736        4396        3460           0        6880        8880
Swap:             0           0           0
```

- `/dev/root` is the kernel's name for the device given with `root=`;
  devtmpfs was mounted on Alpine's empty `/dev` by the kernel
  (`CONFIG_DEVTMPFS_MOUNT=y`), so the tree needed no device nodes.
- musl 1.2.6 is now the system's C library, and apk sees its database with
  all 25 packages.

## RAM

| Item | Buildroot (step 6) | Alpine shell, ext4 root |
| --- | --- | --- |
| `free`: available | 11128 KiB | 8880 KiB |
| `free`: used | 1664 KiB (step 2) | 4396 KiB |
| `free`: buff/cache | | 6880 KiB |

About 2.2 MiB less is available with only one shell running.
`/proc/meminfo` in the same shell, a minute later:

```
MemTotal:          14736 kB
MemFree:            3396 kB
MemAvailable:       8836 kB
Buffers:             884 kB
Cached:             6012 kB
AnonPages:            96 kB
Mapped:             1084 kB
Slab:               1348 kB
SReclaimable:          0 kB
SUnreclaim:         1348 kB
KernelStack:         216 kB
PageTables:           40 kB
VmallocUsed:          60 kB
Percpu:               32 kB
```

| Part | KiB | What it is |
| --- | --- | --- |
| `Buffers` + `Cached` | 6896 | files and ext4 metadata read from the stick; reclaimable, mostly counted in `MemAvailable` |
| `Slab` | 1348 | kernel objects, the dentry and inode caches among them |
| `KernelStack`, `PageTables`, `Percpu`, `VmallocUsed` | 348 | |
| `AnonPages` | 96 | the shell's own memory |
| not attributed | about 2650 | "used" (14736 − 3396 − 884 − 6012 = 4444) minus the lines above |

- `SReclaimable` is 0 because Espressif's defconfig sets `CONFIG_SLUB_TINY`,
  which turns `SLAB_RECLAIM_ACCOUNT` into a no-op (`include/linux/slab.h`).
  The dentry and inode caches can still be shrunk; they are only counted as
  unreclaimable.
- The unattributed part is memory no meminfo line counts, for example pages
  drivers take straight from the page allocator (inferred).

Comparison under Buildroot, same kernel, stick plugged in, about 8 s after
boot, before and after `mount -t ext4 -o ro /dev/sda1 /mnt`:

| KiB | Buildroot, stick in | Buildroot, ext4 mounted ro | Alpine shell, ext4 root rw |
| --- | --- | --- | --- |
| `MemFree` | 11432 | 10708 | 3396 |
| `MemAvailable` | 10964 | 10564 | 8836 |
| `Buffers` + `Cached` | 532 | 1192 | 6896 |
| `Slab` | 1268 | 1320 | 1348 |
| `KernelStack` | 200 | 224 | 216 |
| `AnonPages` | 132 | 128 | 96 |
| `PageTables` + `Percpu` + `VmallocUsed` | 148 | 148 | 132 |
| used (Total − Free − Buffers − Cached) | 2772 | 2836 | 4444 |
| not attributed | 1024 | 1016 | 2652 |

- Mounting ext4 costs 656 KiB of buffer cache, which is reclaimable, and 64
  KiB that is not: 52 KiB of slab and 24 KiB of kernel stacks (the journal and
  ext4 worker threads, inferred).
- About 1 MiB is unattributed under Buildroot as well.
- The Alpine run has 1.6 MiB more unattributed memory. Not identified;
  candidates are the per-CPU lists of free pages, which `MemFree` does not
  count and which fill after a burst of frees such as apk exiting, and the
  journal running read-write. `/proc/zoneinfo` shows the per-CPU lists; to be
  measured in step 14 with OpenRC running.
- The read-only mount printed `EXT4-fs (sda1): orphan cleanup on readonly fs`:
  ext4 found an unfinished delete or truncate in its orphan list, left by the
  Alpine session that ended with a remount instead of a clean shutdown, and
  completed it (inferred).

The remount before the reset:

```
~ # sync
~ # mount -o remount,ro /
[  134.251529] EXT4-fs (sda1): re-mounted 1c890aa0-c025-4720-895d-58cc71c59e6e ro.
```

## Next

Step 14: OpenRC. The same command line without `init=/bin/sh`, so the kernel
starts Alpine's `/sbin/init`, after the console getty, `fstab` and the boot
services are set up in the tree.
