# Step 13: Alpine's root file system on a USB stick

Goal: the kernel mounts an ext4 partition on a USB stick as `/` and starts
Alpine's `/bin/sh` from it. Espressif's SPL, U-Boot, kernel and device tree
stay as they are in flash, and so does Buildroot's cramfs: a reset boots
Buildroot again.

Step 12 showed that Alpine's programs run on this CPU. What exFAT could not
give them is a real root file system: Unix owners and permissions, symbolic
links, a writable `/`. That needs ext4, on a stick of its own.

## 1. Where the kernel's command line comes from

The kernel decides what to mount as `/` from its command line. On this board
the line comes from the device tree, node `/chosen`, property `bootargs`, set
in Espressif's kernel tree in `arch/riscv/boot/dts/espressif/esp32s31.dts`:

```dts
chosen {
	bootargs =
	"earlycon=sbi console=ttyS0 root=mtd:rootfs rootfstype=cramfs ro swiotlb=128";
};
```

| Part | What it does |
| --- | --- |
| `earlycon=sbi` | kernel messages go through OpenSBI's console until the UART driver is up |
| `console=ttyS0` | the console after that: the UART behind the USB-UART port |
| `root=mtd:rootfs` | the root device: the MTD flash partition named `rootfs`, the cramfs |
| `rootfstype=cramfs` | try only this file system type instead of every one the kernel knows |
| `ro` | mount the root read-only |
| `swiotlb=128` | the DMA bounce buffer gets 128 slots of 2 KiB, 256 KiB; the kernel's default is 64 MiB (`IO_TLB_DEFAULT_SIZE` in `include/linux/swiotlb.h`), four times the board's RAM |

U-Boot starts the kernel with `booti 0x40400000 - 0x40200000`
(`CONFIG_BOOTCOMMAND` in Espressif's `configs/espressif_esp32s31_defconfig`):
the kernel at 0x40400000 (flash 0x500000), no initramfs (`-`), the device tree
at 0x40200000 (flash 0x300000).

Before jumping to the kernel, `booti` copies the device tree into RAM (the
boot log line `Loading Device Tree to 50ffb000`) and edits the copy. One of
those edits, `fdt_chosen()` in U-Boot's `boot/fdt_support.c`, replaces
`/chosen/bootargs` with U-Boot's environment variable `bootargs` when that
variable exists. Espressif's board code (`board/espressif/esp32s31/board.c`)
does not change that behaviour, and the default environment has no
`bootargs`, so normally the line from the device tree reaches the kernel
unchanged.

U-Boot's environment is not stored anywhere: the boot log says
`Loading Environment from nowhere... OK`. A `setenv` at the U-Boot prompt
lasts until the next reset.

Together that is the method for this step: change the root at the U-Boot
prompt, without rebuilding or flashing anything, and a reset is the way back
to Buildroot. The line in the device tree changes only once Alpine boots to a
login prompt.

## 2. Try it with Buildroot's root first

This checks the method on the system that already works, so a failure later
can only come from the stick or from Alpine. The change is `init=`: it names
the first program the kernel starts, process 1, instead of `/sbin/init`.

**Stop U-Boot.** The boot log shows `Hit any key to stop autoboot:  0`. With
a delay of 0, U-Boot checks once whether a key is waiting in the UART
(`abortboot_single_key()` in `common/autoboot.c`) and boots otherwise. The
UART's receive buffer is cleared when U-Boot starts, so the key has to arrive
during the last moments of U-Boot's start-up.

With the console open (`python3 -m serial.tools.miniterm --raw /dev/ttyUSB0 115200`),
hold the space bar down, because the keyboard's repeat then sends a space
every few dozen milliseconds, and tap **RST**. Release the space bar when
`esp32s31>` appears. If Linux starts instead, tap RST again while still
holding it. Press Enter once for a clean prompt, because the spaces still in
the buffer land on the command line.

BOARD, U-Boot prompt (`esp32s31>`):

```sh
printenv bootcmd
printenv bootargs
```

`bootcmd` is the `booti` line from the defconfig. `bootargs` answers
`## Error: "bootargs" not defined`: the device tree's line is used.

Still at the U-Boot prompt, Espressif's line plus `init=/bin/sh`, then the
same `booti` that `bootcmd` runs:

```sh
setenv bootargs earlycon=sbi console=ttyS0 root=mtd:rootfs rootfstype=cramfs ro swiotlb=128 init=/bin/sh
booti 0x40400000 - 0x40200000
```

Check that the echoed `setenv` line is complete before pressing Enter,
because a character lost on the serial line here becomes a wrong kernel
option.

The boot ends differently from usual: `Run /bin/sh as init process`, then
`/bin/sh: can't access tty; job control turned off` and a `#` prompt.
Buildroot's init would mount `/proc`, run its start scripts and start a
login; none of that happened. The job control message comes from BusyBox
ash: process 1 has no controlling terminal, so Ctrl+C does nothing in this
shell either.

BOARD, the shell started as init:

```sh
mount -t proc proc /proc
cat /proc/cmdline
echo $$
```

- `/proc` is empty until mounted, because mounting it was init's job.
- `/proc/cmdline` shows the line with `init=/bin/sh`: U-Boot's variable
  replaced the device tree's line.
- `echo $$` prints `1`, this shell's process ID.

Do not type `exit`: when process 1 ends, the kernel panics with
`Attempted to kill init!`. Tap **RST** without touching the keyboard instead.
Buildroot boots normally, because the variable was never saved.

If Buildroot's login came up even with `init=/bin/sh`, U-Boot did not pass
the variable on. Stop there and keep the boot log: the fallback is to change
the device tree itself.

## 3. Prepare the stick

This needs a stick whose contents can be erased. The Ventoy stick does not
work as it is: Ventoy's exFAT partition fills the whole stick, and Linux has
no tool that shrinks exFAT.

HOST, any directory, with the stick plugged into the PC:

```sh
lsblk -o NAME,SIZE,TRAN,MODEL,FSTYPE,LABEL,MOUNTPOINTS
```

The stick is the disk with `usb` under `TRAN` and the stick's size and model.
The commands below write `/dev/sdX`: replace `X` with the letter `lsblk`
shows. The placeholder is deliberate, because pasted unchanged the commands
fail instead of erasing a disk.

If your desktop mounted any of the stick's partitions (a path under
`MOUNTPOINTS`), unmount each one, because a disk in use cannot be
repartitioned:

```sh
sudo umount /dev/sdX1
```

Then wipe, partition and format. The `wipefs` line names every partition
`lsblk` listed, then the disk; this example is for a stick with two
partitions, such as a Ventoy stick. HOST:

```sh
sudo wipefs -a /dev/sdX1 /dev/sdX2 /dev/sdX
printf 'label: dos\nsize=2GiB, type=83\n' | sudo sfdisk /dev/sdX
sudo mkfs.ext4 -L alpine-root -E lazy_itable_init=0,lazy_journal_init=0 /dev/sdX1
```

- `wipefs -a` erases the signatures of the old file systems and of the old
  partition table, so no tool, and not the board's kernel, finds the old
  layout any more. The partitions go first because the new partition
  usually starts at the same 1 MiB as the old first one, so mkfs would
  otherwise find the old file system inside it.
- `sfdisk` reads the new table from its input. `label: dos` is an MBR
  table; the kernel reads MBR and GPT alike (`MSDOS_PARTITION` and
  `EFI_PARTITION`, step 6). One partition of 2 GiB, starting at sfdisk's
  default 1 MiB alignment, type `83` (Linux). 2 GiB is plenty for Alpine; the
  rest of the stick stays unallocated for later.
- `mkfs.ext4 -L alpine-root` names the file system, so `lsblk` shows what is
  on the stick. Without the `-E` options, mkfs leaves the inode tables and
  the journal unzeroed and the kernel's `ext4lazyinit` thread zeroes them in
  the background after the first mount. That would be the board's job, over
  USB, on a 16 MB system, so the PC does it now.
- On a stick that had exFAT or FAT in its first partition, wipefs reports
  `ignoring nested "dos" partition table on non-whole disk device`, and
  mkfs then says `/dev/sdX1 contains 'DOS/MBR boot sector' data` and asks
  `Proceed anyway?`. The old boot sector ends with `55 aa`, the same marker
  as an MBR, and wipefs leaves a "partition table" inside a partition alone
  without `--force`. Answer `y`: the sector lies before ext4's superblock (at
  byte 1024), and the kernel reads partition tables only from whole disks.
- Run mkfs on its own, not pasted together with other lines, because its
  question takes the next pasted line as the answer, which counts as no.

Mount the new file system, because the root tree goes into it in section 5.
HOST:

```sh
sudo mkdir -p /mnt/alpine-root
sudo mount /dev/sdX1 /mnt/alpine-root
```

## 4. The root tree

The tree is made in the x86_64 build container, as in the sibling repository
`alpine-riscv32`, step 10, section 5: apk installs riscv32 packages into a
directory and runs their install scripts there through the host's binfmt_misc
rule for `qemu-riscv32`.

HOST, any directory. The rule disappears when the PC reboots, so check it
first:

```sh
cat /proc/sys/fs/binfmt_misc/qemu-riscv32
```

It must say `enabled` and show `flags: POCF`. If the file does not exist,
register the rule again as in `alpine-riscv32` step 11, section 1.

CONTAINER `alpine-rv32`, as root (`podman exec -it -u root alpine-rv32 sh`):

```sh
mkdir -p /var/tmp/alpine-board-root/etc/apk/keys
cp /work/.config/abuild/*.rsa.pub /var/tmp/alpine-board-root/etc/apk/keys/
apk add --root /var/tmp/alpine-board-root --initdb --arch riscv32 --repository /work/.local/share/abuild/main alpine-base
ls -l /var/tmp/alpine-board-root/sbin/init
du -sh /var/tmp/alpine-board-root
```

- A new tree, not the QEMU one from step 10, because that one has the VM's
  `fstab`, network settings and root password.
- The public key goes in first, because apk checks every package's signature
  against `etc/apk/keys` inside the tree; it also lets the board's apk trust
  the same repository in step 15.
- `alpine-base` is the minimal system: BusyBox, OpenRC, apk-tools, musl and
  the base layout. Nothing else yet, because step 15 tests `apk add` on the
  board.
- `/sbin/init` is a link to `/bin/busybox` that BusyBox's install script
  creates; if it is missing, the scripts did not run (binfmt rule).
- Expected warning, as in step 10:
  `busybox-1.38.0-r7: failed to create initial device nodes: Operation not permitted`.
  A rootless container may not create device nodes, and the board does not
  need them: the kernel mounts devtmpfs on `/dev` (`CONFIG_DEVTMPFS_MOUNT=y`
  in Espressif's kernel defconfig).

## 5. Copy the tree onto the stick

HOST, any directory. tar streams the tree out of the container straight into
the mounted partition:

```sh
podman exec -u root alpine-rv32 tar -C /var/tmp/alpine-board-root -cf - . | sudo tar -C /mnt/alpine-root --numeric-owner -xpf -
```

- The left tar runs in the container as its root, so it can read every file
  and records owner 0 for the files root owns in the tree.
- The right tar runs as root on the PC. `-p` keeps every permission bit,
  setuid included. `--numeric-owner` uses the numbers stored in the archive
  instead of looking the names up in the PC's `/etc/passwd`, whose numbers
  are not Alpine's.
- `podman exec` has no `-t`, because a terminal in between would mangle the
  binary stream.

Check, then unmount, because unmounting writes everything out to the stick.
HOST:

```sh
ls -ln /mnt/alpine-root/sbin/init /mnt/alpine-root/bin/busybox
cat /mnt/alpine-root/etc/alpine-release
df -h /mnt/alpine-root
sudo umount /mnt/alpine-root
```

`ls -ln` shows owner and group as numbers: `0 0` for both files.

## 6. Boot Alpine's shell

Plug the stick into the board's USB-A port, then stop U-Boot as in section 2.

BOARD, U-Boot prompt:

```sh
setenv bootargs earlycon=sbi console=ttyS0 root=/dev/sda1 rootfstype=ext4 rootwait rw swiotlb=128 init=/bin/sh
booti 0x40400000 - 0x40200000
```

What changed against Espressif's line:

| Part | Why |
| --- | --- |
| `root=/dev/sda1` | the first partition of the first SCSI disk, which `usb-storage` creates for the stick (step 6). There is no initramfs, so the kernel looks the name up itself (`early_lookup_bdev()`) |
| `rootfstype=ext4` | the stick's file system |
| `rootwait` | the stick appears only after USB enumeration, and `usb-storage` waits another second (`delay_use`) before scanning it. Without `rootwait` the kernel would look once, find nothing and stop with `VFS: Unable to mount root fs`. With it, it prints `Waiting for root device /dev/sda1...` and looks again until the stick is there |
| `rw` | mount `/` writable from the start. A normal Alpine boot mounts it read-only, checks it with `fsck`, then remounts it writable; there is no `fsck.ext4` for riscv32 yet (e2fsprogs is deferred) |
| `init=/bin/sh` | as in section 2: Alpine's BusyBox shell as process 1, OpenRC not involved yet |

Kernel lines to look for:

- `Waiting for root device /dev/sda1...`
- the USB and SCSI lines from step 6, ending with ` sda: sda1`
- `VFS: Mounted root (ext4 filesystem) on device 8:1.`: major 8 is the SCSI
  disk driver, minor 1 the first partition.
- `Run /bin/sh as init process`, then the job control message and the prompt.

BOARD, Alpine's shell started as init:

```sh
cat /etc/alpine-release
mount -t proc proc /proc
cat /proc/mounts
/lib/ld-musl-riscv32-sf.so.1 2>&1 | head -2
apk --version
apk info | wc -l
free
```

- `/etc/alpine-release` is the release of the base system built in step 9.
- `/proc/mounts` shows `/dev/root` on `/` as ext4, read-write, and devtmpfs
  on `/dev`.
- The loader prints `musl libc (riscv32-sf)` and `Version 1.2.6`: this time
  it is the system's C library, not a file on the stick being run by hand.
- `apk info` reads the installed-package database that `--initdb` created
  in section 4 and lists one package per line.
- `free`: compare with Buildroot, about 11000 KiB available after boot
  (step 6).

**Before the reset.** No init runs a shutdown here, so close the file system
by hand, because a reset with unwritten data leaves ext4 to repair its journal
at the next mount:

```sh
sync
mount -o remount,ro /
```

If the remount answers `Device or resource busy`, a file is still open for
writing; `sync` alone is enough then, and ext4 replays its journal at the
next mount. Tap **RST** without a key: Buildroot boots again.

Done when: Alpine's `/bin/sh` runs as process 1 from the ext4 root on the
stick, the outputs above are recorded, and a plain reset boots Buildroot.
Next: step 14, OpenRC. The same line without `init=/bin/sh`, so the kernel
starts Alpine's `/sbin/init`, after the console login, `fstab` and services
are set up in the tree.
