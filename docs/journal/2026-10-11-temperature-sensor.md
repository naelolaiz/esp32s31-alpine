# 2026-10-11: The chip's temperature sensor (step 22b)

Start: Alpine on GrieferPig's kernel, on Wi-Fi with SSH. The question was
whether porting lm-sensors made sense, and which of the chip's sensors Linux
could read.

End: the temperature sensor reads 28.6 °C from Alpine through
`/sys/class/hwmon`, switched on with a 267-byte overlay of our own, and
`tsens.start` switches it on at boot. The lm-sensors package is written,
not yet tried on the board.

## What the two kernels have

GrieferPig's kernel (`v6.18-esp32-s31`) has built-in drivers for the
temperature sensor (hwmon), the SAR ADC (IIO), touch, comparator and DAC,
all `CONFIG_...=y` in his `esp32s31_defconfig`, all with their device-tree
nodes disabled until an overlay switches them on. Only the temperature
sensor is a hwmon device, so it is the only one lm-sensors can show.
Espressif's kernel (`integration/v6.18-esp32s31`) has none of these
drivers and no nodes for them.

## On the board

The driver directories were there with nothing bound, and `/dev/s31-overlay`
existed:

```
esp32s31:~# ls -d /sys/bus/platform/drivers/esp32s31-tsens /sys/bus/platform/drivers/esp32s31-adc
/sys/bus/platform/drivers/esp32s31-adc
/sys/bus/platform/drivers/esp32s31-tsens
esp32s31:~# ls -l /dev/s31-overlay
crw-------    1 root     root       10, 256 Jan  1  1970 /dev/s31-overlay
```

His `s31-overlay status`, run in a chroot of his squashfs (partition
`rootfs`, `mtdblock7`), showed one active overlay, the radio, with no pins:

```
active-count: 1
active: radio-combo id=1 gpios=0000000000000000
persisted: (none)
```

The first try wrote a 3-byte file (a pasted placeholder instead of the
base64 text) and the manager refused it: `cat: write error: Invalid
argument`, because it is shorter than a device-tree header. Copied over SSH
instead, the real overlay applied:

```
esp32s31:~# cat esp32s31-overlay-tsens.dtbo > /dev/s31-overlay; echo $?
0
esp32s31:~# chroot /mnt/gp /usr/sbin/s31-overlay status
active-count: 2
active: radio-combo id=1 gpios=0000000000000000
active: tsens id=2 gpios=0000000000000000
persisted: (none)
esp32s31:~# cat /sys/class/hwmon/hwmon*/name
esp32s31_tsens
esp32s31:~# cat /sys/class/hwmon/hwmon0/temp1_input /sys/class/hwmon/hwmon0/temp1_min /sys/class/hwmon/hwmon0/temp1_max
28603
-10000
80000
```

28603 is raw value 112 in the driver's uncalibrated formula
(`0.4386 * raw - 20.52`). `dmesg` added only the kernel's usual warning for
an overlay that changes a property of an existing node
(`memory leak will occur if overlay removed`).

## At boot

With the `.dtbo` in `/usr/local/lib/s31-overlays/` and `tsens.start` in
`/etc/local.d`, the first boot showed `* Starting local ... [ !! ]`. After
that OpenRC's `local` service worked, and the sensor was there without a
manual step:

```
esp32s31:~# cat /sys/class/hwmon/hwmon*/name
esp32s31_tsens
esp32s31:~# cat /sys/class/hwmon/hwmon0/temp1_input
27726
```

What failed the first time was not found. `rc-service -v local restart`
names the failing script if it happens again (OpenRC sends the scripts'
output to `/dev/null` unless it runs verbose).

## Open

- `sensors` from lm-sensors (alpine-riscv32 patch 0019) still needs a board
  run.
- The driver uses no per-chip calibration. ESP-IDF reads one from eFuse on
  other ESP32 chips, and his kernel has an eFuse NVMEM driver; a patch for
  his tree, together with registering a thermal zone.
