# Step 22b: The chip's temperature sensor and lm-sensors

Goal: read the ESP32-S31's own temperature sensor from Alpine, at every
boot, and show it with `sensors` from lm-sensors.

This step needs GrieferPig's kernel (steps 19 to 22). Espressif's kernel has
no driver for any of the chip's analog blocks, so on that kernel this would
be new driver work.

## 1. What the chip has, and what Linux sees

lm-sensors reads only `/sys/class/hwmon`: every hardware-monitoring driver
registers a directory there, and lm-sensors prints what it finds. So the
question is which of the chip's blocks have a hwmon driver.

| Block | Driver in GrieferPig's kernel | In `/sys/class/hwmon`? |
| --- | --- | --- |
| Temperature sensor (TSENS) | `drivers/hwmon/esp32s31-tsens.c`, `CONFIG_SENSORS_ESP32S31_TSENS=y` | yes, as `esp32s31_tsens` |
| SAR ADC, 2 x 8 channels | `drivers/iio/adc/esp32s31-adc.c`, `CONFIG_ESP32S31_ADC=y` | no: it is an IIO device (`/sys/bus/iio`). Mainline's `CONFIG_SENSORS_IIO_HWMON` could bridge it, which needs a kernel rebuild |
| Touch, comparator, DAC | built in | no: input, event and output devices, not monitors |

The board has no fan and no voltage or current monitor chip.

All of these are built in, but their device-tree nodes are
`status = "disabled"` in his `esp32s31.dtsi`. His kernel switches nodes on at
run time with device-tree overlays: his overlay manager
(`CONFIG_ESP32S31_DT_OVERLAY`, `drivers/platform/esp32s31-overlay.c`) creates
`/dev/s31-overlay`, and writing a complete `.dtbo` to it applies that
overlay. Several overlays can be active at once, one per name.

His image ships an overlay called `analog` that enables all five blocks. It
also claims 34 GPIOs for the ADC, touch, comparator and DAC pads, among them
GPIO 5 to 19, which are the Ethernet RGMII pins. The sensor itself has no
pins, so this step uses an overlay of our own that enables only the sensor:
[`grieferpig/overlays/esp32s31-overlay-tsens.dtso`](../../grieferpig/overlays/esp32s31-overlay-tsens.dtso).

## 2. Check the kernel

BOARD, Alpine, as root:

```sh
ls /sys/class/hwmon/
ls -d /sys/bus/platform/drivers/esp32s31-tsens
ls -l /dev/s31-overlay
```

- `/sys/class/hwmon/` is empty: the sensor's node is still disabled.
- A directory under `/sys/bus/platform/drivers/` exists when the driver is in
  the kernel, even with no device bound to it. This proves the driver is
  built in without reading the kernel's configuration.
- `/dev/s31-overlay` is the overlay manager (character device 10, 256).

## 3. Build the overlay

HOST, in the esp32s31-alpine clone:

```sh
dtc -I dts -O dtb -o esp32s31-overlay-tsens.dtbo grieferpig/overlays/esp32s31-overlay-tsens.dtso
wc -c esp32s31-overlay-tsens.dtbo
```

- `dtc` is the device-tree compiler: Gentoo's `sys-apps/dtc`, or the one
  Espressif's Buildroot built in `output/host/bin`.
- `/plugin/` in the source makes it an overlay. `&tsens` is not resolved
  here but on the board, against the device tree the kernel booted with.
  His DTB is built with `-@`, which keeps the label names for this.
- His overlay manager refuses an overlay without `espressif,overlay-name`.
- The file is 267 bytes with dtc 1.7.

## 4. Copy it and apply it

HOST, in the esp32s31-alpine clone:

```sh
ssh root@192.168.0.20 'mkdir -p /usr/local/lib/s31-overlays && cat > /usr/local/lib/s31-overlays/esp32s31-overlay-tsens.dtbo' < esp32s31-overlay-tsens.dtbo
```

The shell feeds the file into `ssh`, and `cat` on the board writes it. This
needs only the SSH server of step 17, no `scp`. `/usr/local/lib` is the
place for files that no Alpine package owns.

BOARD:

```sh
cat /usr/local/lib/s31-overlays/esp32s31-overlay-tsens.dtbo > /dev/s31-overlay; echo $?
cat /sys/class/hwmon/hwmon*/name
cat /sys/class/hwmon/hwmon0/temp1_input /sys/class/hwmon/hwmon0/temp1_min /sys/class/hwmon/hwmon0/temp1_max
```

- `echo $?` prints 0 when the kernel took the overlay. A file that is not a
  complete device tree fails with `write error: Invalid argument`.
- The name is `esp32s31_tsens`. The driver probed as soon as the node
  became `okay`.
- hwmon reports millidegrees Celsius: `28603` is 28.6 °C. `temp1_min` and
  `temp1_max` (-10000 and 80000) are the limits the driver programs into the
  sensor's comparator; crossing one sets `temp1_min_alarm` or
  `temp1_max_alarm`.
- `dmesg` shows `OF: overlay: WARNING: memory leak will occur if overlay
  removed, property: /soc/temperature-sensor@20818000/status`. The kernel
  prints this for every overlay that changes a property of an existing node.
  It costs a few bytes, and only if the overlay is removed.

How good the number is: the driver converts with ESP-IDF's formula for the
middle range, `0.4386 * raw - 20.52` °C, so one step is about 0.44 °C, and
it uses no per-chip calibration. Treat the absolute value as a few degrees
off; changes under load are meaningful.

## 5. At every boot

An applied overlay lasts until the next reboot.
[`tsens.start`](../../grieferpig/alpine/etc/local.d/tsens.start) applies it
again from OpenRC's `local` service, the same way as `gp-wifi.start`.

BOARD:

```sh
wget -O /etc/local.d/tsens.start https://raw.githubusercontent.com/naelolaiz/esp32s31-alpine/main/grieferpig/alpine/etc/local.d/tsens.start
chmod +x /etc/local.d/tsens.start
rc-update show | grep local
```

- `local` runs every executable `/etc/local.d/*.start` at the end of boot.
  Without `chmod +x` it skips the file.
- `rc-update show` must list `local` in `default`. Step 14 added it; if it
  is missing, `rc-update add local default`.
- The script does nothing when the sensor is already there, and nothing on a
  kernel without `/dev/s31-overlay`.
- It writes every command and its output to `/var/log/tsens.log`, because
  OpenRC hides the output of `local.d` scripts. If the boot shows
  `* Starting local ... [ !! ]`, `cat /var/log/tsens.log` shows whether this
  script failed and on which line; `rc-service -v local restart` names the
  failing script among all of them.

Reboot, then BOARD: `cat /sys/class/hwmon/hwmon*/name` prints
`esp32s31_tsens` again.

GrieferPig's own way to keep an overlay is `s31-overlay apply NAME` from his
image, which saves it in his persist partition; his `/init` restores the
saved set before it starts our `/sbin/init`. That writes to his flash
settings, so this step keeps the overlay on the Alpine side instead.

## 6. lm-sensors

lm-sensors comes from the Pages repositories of step 22 once the package CI
has built it (alpine-riscv32, patch 0019: built without the `sensord`
daemon, whose RRDtool dependency pulls in cairo and pango).

BOARD:

```sh
apk add lm-sensors
sensors
```

- `lm-sensors` holds `sensors`; `lm-sensors-libs` holds `libsensors`, the
  library other programs use to read the same data.
- `sensors` needs no configuration file for a chip it does not know. It
  should print `esp32s31_tsens-isa-0000` and `Adapter: ISA adapter`
  (libsensors files every platform device under "ISA", a name from PC
  motherboards), then `temp1:` with the `low` and `high` limits from
  `temp1_min` and `temp1_max`. Expected from libsensors' `lib/sysfs.c`, not
  yet seen on the board.
- `sensors-detect` (package `lm-sensors-detect`, Perl) is not needed: it
  probes I2C, SMBus and Super I/O buses for monitor chips, and the board
  has none.

## 7. Switching it off

His overlay manager removes an overlay through an `ioctl`, which only his
`s31-overlay` tool does. It is in his root file system, so mount it first.

BOARD:

```sh
n=$(sed -n 's/^mtd\([0-9]*\):.*"rootfs"$/\1/p' /proc/mtd)
mkdir -p /mnt/gp
mount -t squashfs -o ro /dev/mtdblock$n /mnt/gp
mount -o bind /dev /mnt/gp/dev
mount -t tmpfs tmpfs /mnt/gp/run
chroot /mnt/gp /usr/sbin/s31-overlay status
chroot /mnt/gp /usr/sbin/s31-overlay remove tsens --volatile
```

- `rootfs` is the flash partition with his squashfs; read-only, so nothing
  in his image changes.
- The tool needs `/dev/s31-overlay` inside the chroot, and `/run` for its
  lock and state file.
- `--volatile`: do not touch his saved overlay list, which is in his persist
  partition, not mounted here.

To stop it at boot as well: `chmod -x /etc/local.d/tsens.start`.
