# Step 14: OpenRC on the board

Goal: Alpine boots on the board the way it does on a PC. The kernel starts
Alpine's `/sbin/init` (BusyBox init), which runs OpenRC's runlevels and ends
at a login prompt on the serial console. The root is chosen at the U-Boot
prompt as in step 13, and a reset still boots Buildroot.

## 1. What the tree needs for this board

`apk add alpine-base` installs OpenRC with no service enabled, and its
`/etc/inittab` expects a PC with a screen. Five things have to change:

| What | Why |
| --- | --- |
| `/etc/inittab` | It starts a `getty` (the program that prints the login prompt) on `tty1` to `tty6`, the virtual terminals of a screen. This kernel has none (`CONFIG_VT` is off in Espressif's defconfig), so BusyBox init would retry all six every second. The serial port's line, `ttyS0`, ships commented out. |
| `/etc/fstab` | The root's line, with `0` in the last field (the fsck pass). There is no `fsck.ext4` for riscv32 yet (e2fsprogs is deferred, step 11). A pass other than 0 would make OpenRC's `fsck` service fail, and `root`, `localmount` and `bootmisc` all depend on it. |
| `/etc/hostname` | The name the `hostname` service sets, shown in the prompt. |
| The clock | The kernel has no RTC driver (no `/dev/rtc`), so the clock starts at 1 January 1970 at every boot. OpenRC's `swclock` sets the clock at boot to the time of the last shutdown, which it saves in `/var/lib/misc/openrc-shutdowntime`. The correct time comes with the network in step 15. |
| Runlevels | Services start only once linked into a runlevel with `rc-update`. |

The services, by runlevel. `sysinit` and `boot` run once at start-up,
`default` is the normal running state, `shutdown` runs at `poweroff` and
`reboot`:

| Service | Runlevel | What it does |
| --- | --- | --- |
| `sysfs` | sysinit | mounts `/sys`, which many tools read |
| `devfs` | sysinit | mounts `/dev/pts` (terminals, needed for SSH in step 17) and `/dev/shm` on the kernel's devtmpfs |
| `dmesg` | sysinit | lowers the console log level, so kernel messages stop landing in the middle of the login prompt |
| `swclock` | boot | sets the clock from the last shutdown time |
| `hostname` | boot | sets the host name from `/etc/hostname` |
| `bootmisc` | boot | prepares `/var/run` and the login records (`utmp`, `wtmp`). It needs `localmount`, which needs `root` and `fsck`, so OpenRC starts those too. |
| `local` | default | runs `/etc/local.d/*.start`, the place for your own commands |
| `killprocs` | shutdown | stops the processes still running |
| `savecache` | shutdown | copies OpenRC's dependency cache from `/run` (RAM) to `/var/cache/rc`, so the next boot can reuse it |
| `mount-ro` | shutdown | remounts `/` read-only, what you did by hand in step 13 |

`mdev`, which the plan listed here, moves to step 16. Alpine's `mdev`
service registers `/sbin/mdev` in `/proc/sys/kernel/hotplug`, which exists
only with `CONFIG_UEVENT_HELPER`. That option is off in this kernel, and its
help text warns that it can run small systems out of memory at boot. The
kernel's devtmpfs already creates the device nodes; step 16 runs mdev as a
daemon that listens to the kernel over netlink (`mdev -d`) instead.

## 2. Configure the tree from Alpine's shell

The changes are made on the board, in the `init=/bin/sh` shell from step 13,
because the root is writable there and `rc-update` runs natively.

HOST, any directory. This prints the current time in UTC, which the board
needs in a moment:

```sh
date -u '+%Y-%m-%d %H:%M:%S'
```

Plug the stick into the board, stop U-Boot (hold the space bar, tap RST).
BOARD, U-Boot prompt, the same lines as step 13:

```sh
setenv bootargs earlycon=sbi console=ttyS0 root=/dev/sda1 rootfstype=ext4 rootwait rw swiotlb=128 init=/bin/sh
booti 0x40400000 - 0x40200000
```

BOARD, Alpine's shell. First the clock, with the time the PC printed, because
every file written in this session gets that time as its date. The tree has
no time zone set, so the system clock is UTC:

```sh
date -s '2026-10-07 21:00:00'
touch /var/lib/misc/openrc-shutdowntime
ls -l /var/lib/misc/openrc-shutdowntime
```

`touch` creates the file swclock reads at the first boot; without it the
clock would stay at 1970.

The gettys:

```diff
/etc/inittab:8-13
-tty1::respawn:/sbin/getty 38400 tty1
-tty2::respawn:/sbin/getty 38400 tty2
-tty3::respawn:/sbin/getty 38400 tty3
-tty4::respawn:/sbin/getty 38400 tty4
-tty5::respawn:/sbin/getty 38400 tty5
-tty6::respawn:/sbin/getty 38400 tty6
+#tty1::respawn:/sbin/getty 38400 tty1
+#tty2::respawn:/sbin/getty 38400 tty2
+#tty3::respawn:/sbin/getty 38400 tty3
+#tty4::respawn:/sbin/getty 38400 tty4
+#tty5::respawn:/sbin/getty 38400 tty5
+#tty6::respawn:/sbin/getty 38400 tty6
/etc/inittab:16
-#ttyS0::respawn:/sbin/getty -L 115200 ttyS0 vt100
+ttyS0::respawn:/sbin/getty -L 115200 ttyS0 vt100
```

```sh
sed -i -e 's|^tty\([1-6]\)::|#tty\1::|' -e 's|^#ttyS0::|ttyS0::|' /etc/inittab
grep -n getty /etc/inittab
```

- The first `sed` expression comments out the six screen gettys, the second
  uncomments the serial one.
- In the `ttyS0` line, `respawn` makes init start the getty again after each
  logout. `-L` marks a local line, so getty does not wait for a modem's
  carrier signal. `115200` is the baud rate, and `vt100` becomes `TERM`, the
  terminal type that programs like nano use to draw.

The root's line in `fstab`, the host name, and a check:

```sh
echo '/dev/sda1 / ext4 rw,relatime 0 0' >> /etc/fstab
echo esp32s31 > /etc/hostname
cat /etc/fstab /etc/hostname
```

The `fstab` fields are: device, mount point, type, options, dump (unused),
fsck pass. The other lines in `fstab` (cdrom, usbdisk, floppy) are marked
`noauto`, so nothing tries to mount them.

The services:

```sh
rc-update add sysfs sysinit
rc-update add devfs sysinit
rc-update add dmesg sysinit
rc-update add swclock boot
rc-update add hostname boot
rc-update add bootmisc boot
rc-update add local default
rc-update add killprocs shutdown
rc-update add savecache shutdown
rc-update add mount-ro shutdown
rc-update show
```

Each `rc-update add` creates a link in `/etc/runlevels/<runlevel>/` to the
script in `/etc/init.d/`. `rc-update show` lists every enabled service with its
runlevel.

Close the file system and reset, as in step 13:

```sh
sync
mount -o remount,ro /
```

Tap **RST** without a key.

## 3. Boot with OpenRC

Stop U-Boot again. BOARD, U-Boot prompt, the step 13 line without
`init=/bin/sh`:

```sh
setenv bootargs earlycon=sbi console=ttyS0 root=/dev/sda1 rootfstype=ext4 rootwait rw swiotlb=128
booti 0x40400000 - 0x40200000
```

Without `init=`, the kernel tries `/sbin/init` first: the log shows
`Run /sbin/init as init process`. BusyBox init reads `/etc/inittab` and runs
its lines in order:

1. `::sysinit:/sbin/openrc sysinit`: OpenRC mounts `/proc` and `/run` itself,
   then starts the sysinit services.
2. `::sysinit:/sbin/openrc boot`: the boot runlevel.
3. `::wait:/sbin/openrc default`: the default runlevel; `wait` makes init wait
   for it to finish.
4. `ttyS0::respawn:...`: the login prompt.

Each service prints a ` * ... [ ok ]` line. Expected on this first boot only:
`Clock skew detected` while OpenRC builds its dependency cache in the
sysinit runlevel. The clock still reads 1970 at that point, because swclock
runs later, in the boot runlevel, and the cache comes out older than the
init scripts it was built from. `savecache` saves the cache at shutdown, and
from the next boot on OpenRC reuses it.

The boot ends with:

```
Welcome to Alpine Linux 3.25.0_alpha20260805 (edge)
Kernel 6.18.0 on riscv32 (/dev/ttyS0)

esp32s31 login:
```

Log in as `root`. Alpine's root account has no password (`root::` in
`/etc/shadow`), so none is asked. That is acceptable on a serial cable only;
step 17 (SSH) sets one.

## 4. Look around

BOARD, logged in:

```sh
rc-status
date
ps
free
cat /proc/meminfo
grep -A3 'cpu: 0' /proc/zoneinfo
```

- `rc-status` lists the default runlevel's services and their state.
- `date` shows the time swclock set: the moment of the `touch` above, not
  the real time.
- `ps` shows what runs now: init, your login shell, and the kernel's own
  threads in square brackets. No service in this list stays running.
- `free` and `/proc/meminfo` are the RAM cost of a full Alpine boot,
  compared with step 13's shell (8836 KiB available).
- The `cpu: 0` block of `/proc/zoneinfo` is the per-CPU list of free pages:
  `count` is how many 4 KiB pages it holds. `MemFree` does not include them,
  so this tells whether they explain part of step 13's unattributed memory.

## 5. Shut down

BOARD:

```sh
poweroff
```

OpenRC runs the shutdown runlevel: `killprocs`, `savecache`, swclock saving
the time, `mount-ro`. Tap **RST** once `mount-ro` has reported
`Remounting remaining filesystems read-only` and the kernel has printed its
last line. A plain reset boots Buildroot; booting Alpine again means stopping
U-Boot and typing the two lines from section 3.

Done when: OpenRC boots to `esp32s31 login:`, root logs in, and the RAM
numbers are recorded.
Next: step 15, Ethernet and apk on the board.
