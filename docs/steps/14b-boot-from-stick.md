# Step 14b: boot Alpine from the stick automatically

Goal: with the Alpine stick plugged in, a reset boots Alpine to the login
prompt with no typing at the U-Boot prompt. Without the stick, the board boots
Buildroot as before. Stopping U-Boot depends on a key arriving during one
check of the UART (`CONFIG_BOOTDELAY=0`, step 13), which does not always work.

## 1. Where the choice is made

| Place | Why or why not |
| --- | --- |
| U-Boot (`bootdelay`, a saved environment, a boot script) | The ground rule keeps the boot stack as Espressif ships it ([plan](../plan.md)). |
| `root=/dev/sda1 rootwait` in the device tree's `bootargs` | No fallback: without the stick, `rootwait` waits forever. |
| An initramfs built into the kernel | Needs a kernel rebuild and a static BusyBox inside the kernel image, and the archive is unpacked into RAM. |
| Buildroot's `/sbin/init` in the flash root | **Chosen.** The kernel already mounts the cramfs and runs `/sbin/init` from it. Replacing that file with a script changes only the root file system image; the kernel, the device tree and the boot stack stay the same. |

The kernel line stays Espressif's
(`earlycon=sbi console=ttyS0 root=mtd:rootfs rootfstype=cramfs ro swiotlb=128`).
The kernel boots the cramfs as always, and the switch to the stick happens in
user space, in the first program the kernel runs.

## 2. The script

[`buildroot/rootfs-overlay/sbin/init`](../../buildroot/rootfs-overlay/sbin/init):

```sh
#!/bin/sh
tries=5
while [ ! -b /dev/sda1 ] && [ "$tries" -gt 0 ]; do
	sleep 1
	tries=$((tries - 1))
done

if [ -b /dev/sda1 ] && mount -t ext4 -o rw /dev/sda1 /mnt; then
	if [ -f /mnt/etc/alpine-release ] && mount -t devtmpfs devtmpfs /mnt/dev; then
		echo "init: Alpine found on /dev/sda1, switching to it"
		cd /mnt
		if pivot_root . mnt; then
			umount -l /mnt
			exec chroot . /sbin/init <dev/console >dev/console 2>&1
		fi
		cd /
		umount /mnt/dev
	fi
	umount /mnt
fi

echo "init: no Alpine stick, starting Buildroot"
exec /bin/busybox init
```

- **It is a shell script.** The kernel runs `/sbin/init` as process 1
  (`Run /sbin/init as init process`). A `#!` script works there because the
  kernel's `CONFIG_BINFMT_SCRIPT` (on by default) hands it to `/bin/sh`,
  Buildroot's BusyBox shell.
- **The wait.** Init starts at about 1.45 s, and `sda1` appears at about
  3.04 s (step 13's table). The loop checks once a second, for at most five
  seconds, whether `/dev/sda1` exists as a block device (`-b`). The cost: a
  boot without a stick waits five seconds longer before Buildroot starts.
- **`mount -t ext4 -o rw /dev/sda1 /mnt`.** This uses the cramfs's empty
  `/mnt`, as in step 6. It mounts read-write, as the kernel did in step 14
  with `rw`. A stick with another file system, an exFAT data stick for
  example, fails to mount here, and the script goes on to Buildroot.
- **`/mnt/etc/alpine-release`.** An ext4 stick that is not an Alpine root is
  unmounted, and Buildroot starts.
- **`mount -t devtmpfs devtmpfs /mnt/dev`.** The kernel mounts devtmpfs only
  on the root it mounted itself (`CONFIG_DEVTMPFS_MOUNT`, `init/do_mounts.c`),
  so Alpine's `/dev` would be empty. All devtmpfs mounts show the same single
  set of device nodes, so Alpine gets the same `/dev` the kernel gave it in
  steps 13 and 14.
- **`pivot_root . mnt`.** This makes the stick `/` and moves the cramfs to
  `/mnt` inside it. `switch_root` would not work: it is made for an initramfs.
  It deletes the old root's files to free their RAM, and it refuses any other
  root (`root filesystem is not ramfs/tmpfs`, `util-linux/switch_root.c` in
  BusyBox). The cramfs is an ordinary mounted file system, the case
  `pivot_root` was made for.
- **After `pivot_root`, commands come from Alpine.** `pivot_root` changes the
  root directory of every process whose root was the old root, this shell
  included. So `umount` and `chroot` below are Alpine's BusyBox, found through
  the shell's default `PATH`.
- **`umount -l /mnt`.** This detaches the cramfs, together with the kernel's
  devtmpfs mount on its `/dev`, so Alpine's `/mnt` is empty. The `-l` (lazy)
  option is needed because the running shell and the script it is reading
  still come from the cramfs; the kernel releases the file system once they
  are gone, after the `exec`.
- **`exec chroot . /sbin/init <dev/console >dev/console 2>&1`.** This is the
  sequence from the `pivot_root(8)` manual page.
  - `exec` replaces the shell and keeps process ID 1: if process 1 exits, the
    kernel panics.
  - `chroot` starts Alpine's `/sbin/init`, BusyBox init, which runs OpenRC as
    in step 14.
  - The redirections reopen the console from the new `/dev`, so no file of
    the old root stays open.
- **Failures.** Each failed step before `pivot_root` unmounts what it mounted
  and falls through to Buildroot.
- **`exec /bin/busybox init`.** This is Buildroot's init. `/sbin/init` was a
  link to `/bin/busybox`, so Buildroot boots exactly as before, with
  Espressif's `/etc/inittab`.

The three paths (Alpine stick, ext4 stick without Alpine, no stick) were run
on a PC in a mount namespace before this guide was written. There, a tmpfs
stood in for the cramfs and a loop device for the stick, and Ubuntu's static
BusyBox 1.36.1 ran the commands. In the Alpine case, the test init ran as
process 1 and saw only three mounts: the stick on `/`, devtmpfs on `/dev`,
and its own `/proc`. `/mnt` was empty.

## 3. Check BusyBox on Buildroot

BOARD, Buildroot shell (reset without the stick). This confirms that
Buildroot's BusyBox has the commands the script needs before the script goes
into flash:

```sh
busybox --list | grep -E '^(pivot_root|chroot|mount|umount|sleep)$'
ls -l /sbin/init
```

All five names should print. Buildroot's default BusyBox configuration
(`package/busybox/busybox.config`) has `CONFIG_PIVOT_ROOT=y`, and
Espressif's defconfig does not replace it. `/sbin/init` should be a link to
`../bin/busybox`.

## 4. Add the overlay to Buildroot

A Buildroot overlay is a directory that Buildroot copies over the target tree
just before it packs the root file system image. Espressif's defconfig
already uses one, for its `inittab` and `fstab`.

HOST, in the clone of this repository. This gets the script, shows that it is
executable, and prints the directory's absolute path, which Buildroot needs:

```sh
git pull
ls -l buildroot/rootfs-overlay/sbin/init
realpath buildroot/rootfs-overlay
```

The mode must be `-rwxr-xr-x`. Buildroot copies overlays with
`rsync --chmod=u=rwX,go=rX`, which keeps the execute bit only on a file that
already has it.

HOST, in the Buildroot output directory of the Espressif build (the one with
`images/` and `build/` in it). Keep a copy of the current flash image first,
because flashing it back undoes this step:

```sh
cp images/s31_full_flash.bin s31_full_flash-step14.bin
make menuconfig
```

In the menu, open **System configuration → Root filesystem overlay
directories** and press Enter. Move to the end of the line, type a space,
then the path that `realpath` printed. Confirm with Enter, exit, and save.
The entries are copied in order, so this repository's `/sbin/init` comes last
and replaces BusyBox's link.

```diff
.config
-BR2_ROOTFS_OVERLAY="$(BR2_EXTERNAL_ESP_LINUX_BSP_PATH)/board/espressif/esp32s31/rootfs_overlay"
+BR2_ROOTFS_OVERLAY="$(BR2_EXTERNAL_ESP_LINUX_BSP_PATH)/board/espressif/esp32s31/rootfs_overlay <the path realpath printed>"
```

The change lives in `.config` only. Loading Espressif's defconfig again
(`make espressif_esp32s31_function_core_board_nor_defconfig`) would drop it,
so this guide is the record of it.

Check the line, rebuild, and look at what went into the image:

```sh
grep BR2_ROOTFS_OVERLAY .config
make
ls -l target/sbin/init
head -3 target/sbin/init
ls -l images/rootfs.cramfs images/s31_full_flash.bin
```

- A plain `make` is enough. Buildroot copies the overlays at every `make` and
  repacks the cramfs and the flash image from `target/`; no package is
  rebuilt.
- `target/sbin/init` is now a regular file of about 1 KiB instead of a link.
- The build prints `rootfs-cramfs: rootfs.cramfs = ... bytes`. It should be
  within a few KiB of before; the slot is 4 MiB.

## 5. Flash and test

Flash `images/s31_full_flash.bin` as in step 4. Only the root file system
slot at 0xC00000 differs from what is in flash now.

**With the stick.** Plug the stick into the board and tap **RST** without
touching the keyboard. The log should show:

```
[    1.4xxxxx] Run /sbin/init as init process
init: Alpine found on /dev/sda1, switching to it
...
   OpenRC 0.63.2 is starting up Linux 6.18.0 (riscv32)
...
esp32s31 login:
```

The kernel lines between them are the stick being detected, as in step 13.

BOARD, logged in to Alpine:

```sh
cat /proc/mounts
cat /proc/cmdline
free
```

- `/proc/mounts` should list `/dev/sda1` on `/` and devtmpfs on `/dev`, and
  no cramfs.
- `/proc/cmdline` still shows Espressif's line with `root=mtd:rootfs`: the
  kernel booted the cramfs, and the script switched roots after it.
- `free` should be close to step 14's 9912 KiB available.

Shut down with `poweroff`, as in step 14.

**Without the stick.** Unplug the stick and tap **RST**. About five seconds
after `Run /sbin/init as init process`, the log shows
`init: no Alpine stick, starting Buildroot`, followed by Buildroot's shell as
before.

The U-Boot prompt still works as in steps 13 and 14, for example to add
`init=/bin/sh`.

## 6. If something goes wrong

- **Alpine fails after the switch.** Unplug the stick and tap RST: Buildroot
  boots.
- **The script itself fails.** The kernel panics with
  `Attempted to kill init!`. Stop U-Boot and give Buildroot's line with
  `init=/bin/sh`, which bypasses the script:
  `setenv bootargs earlycon=sbi console=ttyS0 root=mtd:rootfs rootfstype=cramfs ro swiotlb=128 init=/bin/sh`,
  then `booti 0x40400000 - 0x40200000`. Or flash `s31_full_flash-step14.bin`
  back.
- **Removing the overlay for good.** Delete the path from
  `BR2_ROOTFS_OVERLAY`, then run `rm target/sbin/init` and
  `make busybox-reinstall all`. Buildroot installs BusyBox with
  `install-noclobber`, which does not replace existing files, so the link
  comes back only after the file is removed.

Done when: a reset with the stick boots Alpine to `esp32s31 login:`, and a
reset without it boots Buildroot.
Next: step 15, Ethernet and apk on the board.
