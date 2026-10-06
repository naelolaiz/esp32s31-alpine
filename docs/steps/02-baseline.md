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
