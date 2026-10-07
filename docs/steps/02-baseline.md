# Step 2: build and boot Espressif's image

Goal: prove the board, cable and boot chain work, and record the numbers later
steps are measured against. Nothing is modified in this step.

## Before you start

- Linux on x86_64 or aarch64 (Espressif's toolchain is prebuilt only for those).
- Buildroot host tools: gcc, g++, make, git, rsync, bc, cpio, unzip, file, wget,
  perl, python3 (with venv). About 15 GB free disk.
- Your user in the serial group (`dialout` on Debian/Ubuntu, `uucp` on Arch/Gentoo).
- Board connected through the **USB-UART** Type-C port.

## 1. Build

From the top of this repository:

```sh
scripts/build-baseline.sh          # work tree goes to ../work
```

It clones Buildroot 2025.02 and esp-buildroot-external, installs esptool in a
venv, builds, and prints the image sizes. The first build downloads the
toolchain and the kernel and takes a while.

## 2. Flash

Put the board in download mode: hold **BOOT**, tap **RST**, release BOOT. Then:

```sh
cd ../work
ls /dev/ttyUSB* /dev/ttyACM*       # find the port
venv/bin/esptool --chip esp32s31 --port /dev/ttyUSB0 --baud 1152000 \
    write-flash 0x0 out/images/s31_full_flash.bin
```

Use `--baud 460800` if it fails mid-write.

### Flashing less

`s31_full_flash.bin` is the five slots of the flash layout (SPL, U-Boot,
device tree, kernel, root file system) merged with
`esptool merge-bin --format raw`, which fills the gaps between them with
0xFF. Roughly half of its 16 MB is that filling (estimated from the file
sizes), and esptool erases and writes
it like real data. esptool prints a hint about this during the build. Two
ways to write less, both from the `images/` directory of the Buildroot output
directory, after a build.

A HEX file holds only the five regions, each with its address, so
`write-flash` writes only those (the `0x0` is ignored for a HEX file). The
first command checks that the five files exist under these names; the
second merges them with the flash settings the BSP's layout file uses (DIO at
80 MHz is required by the ROM):

```sh
ls -l spl_app.bin u-boot.itb esp32s31.dtb xipImage rootfs.cramfs
esptool --chip esp32s31 merge-bin -o s31_full_flash.hex --format hex --flash-mode dio --flash-freq 80m --flash-size 16MB 0x2000 spl_app.bin 0x100000 u-boot.itb 0x300000 esp32s31.dtb 0x500000 xipImage 0xc00000 rootfs.cramfs
esptool --chip esp32s31 --port /dev/ttyUSB0 --baud 1152000 write-flash 0x0 s31_full_flash.hex
```

When only one part changed, write only its slot, because the other slots in
flash already hold the same data. After a root file system change:

```sh
esptool --chip esp32s31 --port /dev/ttyUSB0 --baud 1152000 write-flash 0xc00000 rootfs.cramfs
```

After a kernel change:

```sh
esptool --chip esp32s31 --port /dev/ttyUSB0 --baud 1152000 write-flash 0x500000 xipImage
```

The offsets come from `configs/esp32s31-layout.cfg` in esp-linux-bsp.

## 3. Boot and look around

Open the console at 115200 with pyserial's terminal (installed with esptool;
exit with Ctrl+]), logging to a file:

```sh
script -c "venv/bin/python -m serial.tools.miniterm --raw /dev/ttyUSB0 115200" boot-baseline.log
```

`picocom -b 115200`, `tio` or `screen ... 115200` work too. Tap **RST** and
watch SPL, OpenSBI, U-Boot and Linux go by to a root shell. Then run:

```sh
uname -a; cat /proc/cpuinfo; free; head -5 /proc/meminfo
cat /proc/mtd; cat /proc/cmdline; ls /dev/tty*; ip link
dmesg | tail -40
```

## 4. Record

Paste the build sizes, the boot log banners and the command output into the
thread (or a new `docs/journal/<date>-baseline.md`), and fill in the commit
column of [bsp/versions.md](../../bsp/versions.md).

Done when: the board boots to the Buildroot shell repeatably and the numbers are
recorded. Next: step 3, probe Ethernet and USB.
