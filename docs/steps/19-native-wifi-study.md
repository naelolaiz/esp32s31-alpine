# Step 19: What the board offers native Wi-Fi

Goal: three facts from our own board that decide how much of the native
Wi-Fi port (phase 5) is needed, and, optionally, Wi-Fi seen working on this
board before porting anything.

The study behind this step, with the three community designs and why the
plan follows "the Wi-Fi blob runs inside Linux", is in the
[journal](../journal/2026-10-07-native-wifi-study.md). In short: Espressif's
Wi-Fi code only exists as ESP-IDF libraries, GrieferPig runs them inside a
Linux 6.18 driver on a boot chain shaped like Espressif's, and that needs
about 282 KiB of internal SRAM (0x2F030000 to 0x2F07CFB0) plus the F
(single-precision floating point) extension.

| Question | Why it matters | Section |
| --- | --- | --- |
| How much internal SRAM does our OpenSBI take? | The radio needs SRAM from 0x2F030000; OpenSBI's own region is closed to Linux. | 2 |
| Does our OpenSBI already start hart 1? | Espressif added that on 2026-10-06; it decides whether SMP is possible later without touching OpenSBI. | 2 |
| What does our kernel reserve, and which options are already on? | The 256 KiB DMA pool sits where the radio needs SRAM; `CONFIG_FPU`, modules and wireless decide the kernel work. | 3, 4 |

Nothing here changes the board. Section 5 is optional and replaces the whole
flash for a while, with a backup first.

## 1. Why OpenSBI is read from the PC

OpenSBI is the M-mode firmware between U-Boot SPL and U-Boot. Upstream
OpenSBI prints a banner at start-up with where it sits in memory, which
platform drivers it found, and the memory regions it protects with PMP
(Physical Memory Protection, the RISC-V mechanism M-mode uses to fence off
memory from S-mode and U-mode).

Espressif's boot does not show that banner. A full boot log has only the
platform's own line between SPL and U-Boot:

```
Trying to boot from NOR
OpenSBI: ESP32-S31 firmware active


U-Boot 2024.07 (...)
```

The likely reason is U-Boot SPL: it starts OpenSBI's `fw_dynamic` firmware
with an options word, and U-Boot's default for it
(`CONFIG_SPL_OPENSBI_SCRATCH_OPTIONS=0x1`) is "no boot prints" (inferred from
U-Boot's defaults, not checked in Espressif's SPL config). Changing that
would mean changing the boot stack, so the same facts are read from the
OpenSBI binary that Buildroot built instead.

## 2. Read OpenSBI's build

HOST, in the Buildroot output directory of your Espressif build (the one
with `images/` and `build/`):

```sh
grep -a -o esp32s31-hsm images/u-boot.itb
readelf -lW $(find build -name fw_dynamic.elf | head -1)
```

- `u-boot.itb` is the image flashed at 0x100000, and OpenSBI's
  `fw_dynamic.bin` is inside it. The string `esp32s31-hsm` is the name of
  Espressif's hart 1 start code (`hsm.c`, merged on 2026-10-06), so `grep`
  printing it means our OpenSBI can start hart 1; printing nothing means the
  branch was fetched before that.
- `readelf -l` lists the program segments: `VirtAddr` is where each one is
  loaded and `MemSiz` how much memory it takes, including the zeroed data
  (BSS) that the file does not store. The highest `VirtAddr + MemSiz` is
  where OpenSBI's SRAM ends; it must stay below 0x2F030000 for the radio
  layout to fit. The host's own `readelf` reads RISC-V files, so the
  toolchain's is not needed.
- OpenSBI's default PMP setup fences off only its own image from S-mode and
  leaves the rest open, and Espressif's platform code adds no regions (read
  from its source, not checked on the board). So the end address from
  `readelf` is also the edge of what Linux may not touch.

## 3. What Linux reserved

Open the board's console first. HOST, in the directory that holds the clone
of this repository (one level above it). This runs miniterm with the venv's
Python by its path, so the venv does not have to be activated; pyserial,
which provides miniterm, was installed in it together with esptool. The
console is usually `/dev/ttyUSB0`; check with `ls /dev/ttyUSB*` if not:

```sh
esp32s31-alpine/.venv/bin/python -m serial.tools.miniterm --raw /dev/ttyUSB0 115200
```

BOARD, Alpine logged in as root. The first command shows the reserved
memory regions the kernel found in the device tree and its bounce buffer
(swiotlb, the PSRAM area used to copy DMA data for devices that cannot reach
an address); the others show how many harts Linux runs on and the ISA it was
told about:

```sh
dmesg | grep -i -E 'reserved mem|dma|swiotlb'
cat /sys/devices/system/cpu/possible /sys/devices/system/cpu/online
grep isa /proc/cpuinfo
```

- Expect a line naming the pool at `0x2f030000`. The kernel prints its size
  in whole MiB, so the 256 KiB pool shows as `0 MiB`. That pool sits where
  the radio needs SRAM and has to shrink in step 20.
- `possible` and `online` both `0` means Linux runs on hart 0 only, as the
  device tree with one `cpu@0` says.

The kernel also exposes the device tree it booted with under
`/proc/device-tree`, one directory per node and one file per property. That
is the tree from the DTB slot (flash 0x300000) as the kernel received it,
after U-Boot's changes, so it is the ground truth rather than the `.dts`
source. BOARD, same shell. The first command lists every reservation, the
second prints the pool's `reg` property, the third the ISA string the kernel
was given:

```sh
ls /proc/device-tree/reserved-memory/
hexdump -C /proc/device-tree/reserved-memory/dma-pool@2f030000/reg
cat /proc/device-tree/cpus/cpu@0/riscv,isa; echo
```

- Device tree numbers are big-endian 32-bit cells. With one address cell and
  one size cell, the `reg` dump should read `2f 03 00 00 00 04 00 00`: start
  0x2F030000, size 0x40000 (256 KiB).
- Any other directory under `reserved-memory` is also SRAM or PSRAM the radio
  layout has to respect; paste the list even if it only holds the pool.
- `echo` only adds the line break that the property file does not end with.

## 4. Which kernel options are already on

HOST, in the Buildroot output directory of your Espressif build (the one
with `images/` and `build/`). The kernel's final configuration is in its
build directory; this lists the options the radio port depends on:

```sh
grep -E '^(# )?CONFIG_(SMP|FPU|MODULES|WLAN|CFG80211|MAC80211|RFKILL|CRYPTO_CCM|CRYPTO_AES)[ =]' build/linux-*/.config
```

- `CONFIG_FPU=y`: the kernel can save and restore floating point registers.
  It is on by default, but stock Linux still drops F on a hart without D
  (`This kernel does not support systems with F but not D`), so step 20 needs
  a small kernel patch either way.
- `CONFIG_MODULES`: whether the radio can be a loadable module or must be
  built in.
- `CONFIG_WLAN`, `CONFIG_CFG80211`, `CONFIG_MAC80211`: the Linux wireless
  stack. Expect them off; step 21 turns them on and measures what they cost
  in flash and RAM.

Paste the output of sections 2 to 4 in the thread.

## 5. Optional: Wi-Fi on this board with GrieferPig's image

This is a trial, not a step towards our image: it shows that the radio,
the antenna and GrieferPig's driver work on the Function-CoreBoard-1
before weeks go into the port. It erases the whole flash, so the first
command saves it.

GrieferPig publishes one full image, `s31_full_flash.bin`, under
[Releases](https://github.com/GrieferPig/esp32-s31-linux/releases)
(flashing guide:
[Flash and first boot](https://grieferpig.github.io/esp32-s31-linux-docs/en/get-started/flash-and-first-boot.html)).
If the latest release has no such file, skip this section.

Unplug the USB stick first. GrieferPig's image probes USB storage at boot,
and the stick now holds the Alpine root with everything installed on it
(htop, vim, mc, python3 and their libraries), which should not depend on code
we have not read closely.

The commands below call `esptool` by name, so activate its venv in every
terminal you use for them first. HOST, in the directory that holds the
clone of this repository (one level above it):

```sh
. esp32s31-alpine/.venv/bin/activate
```

The venv stays active after `cd`, so the commands below work from the
directories they name.

### Back up the flash

The step 14 backup no longer matches the board: steps 14b and 15 changed
the root file system slot, so take a fresh one. Put the board in download
mode (hold **BOOT**, tap **RST**, release **BOOT**), because esptool can
only read the flash through the ROM's download mode.

HOST, in the Buildroot output directory, next to the step 14 backup. This
reads all 16 MiB (0x1000000 bytes) from offset 0:

```sh
esptool --chip esp32s31 --port /dev/ttyUSB0 --baud 1152000 read-flash 0 0x1000000 s31_full_flash-step19.bin
ls -l s31_full_flash-step19.bin
```

`ls` must show 16777216 bytes. A shorter file means the read stopped; run
it again at `--baud 460800`.

### Flash GrieferPig's image

HOST, in the directory you downloaded `s31_full_flash.bin` to. Board still
in download mode (enter it again if it reset). `erase-flash` empties the
whole chip first, as GrieferPig's guide does, because his image keeps
settings in a JFFS2 partition (`persist`) that must not start from our old
bytes:

```sh
esptool --chip esp32s31 --port /dev/ttyUSB0 --baud 1152000 erase-flash
esptool --chip esp32s31 --port /dev/ttyUSB0 --baud 1152000 write-flash --flash-mode dio --flash-freq 80m --flash-size 16MB 0x0 s31_full_flash.bin
```

HOST, in the directory that holds the clone of this repository, so the log
stays outside the clone, then tap **RST**. `script` copies everything the
terminal shows into the file:

```sh
script -c "esp32s31-alpine/.venv/bin/python -m serial.tools.miniterm --raw /dev/ttyUSB0 115200" boot-grieferpig.log
```

BOARD, at `esp32-s31 login:`, log in as `root` with no password (his
default). Connect to your Wi-Fi network; the command asks for the password
on its own line, so it does not end up in the shell history. The network
must use WPA2-Personal with AES (CCMP), the only mode his driver supports:

```sh
esp32-config wifi connect 'YOUR NETWORK NAME'
esp32-config wifi status
ip addr show wlan0
ping -c 3 192.168.0.14
```

Then the facts we want from it:

```sh
grep isa /proc/cpuinfo
free
dmesg | grep -i -E 'radio|wlan|esp32s31-wifi|softmac' | head -30
```

- `grep isa`: his device tree lists `f`. His radio code executes F
  instructions, so Wi-Fi working on his image means our chip has F.
- `free`: RAM left with Wi-Fi up, to compare with our 8892 KiB available
  after boot with networking (step 15).
- `ping` to the PC (192.168.0.14) goes over Wi-Fi only, since the Ethernet
  cable stays unplugged.

### Restore our flash

Download mode again, then HOST, in the Buildroot output directory. Writing
the 16 MiB backup puts every slot back, ours included:

```sh
esptool --chip esp32s31 --port /dev/ttyUSB0 --baud 1152000 write-flash 0x0 s31_full_flash-step19.bin
```

Plug the stick back in and tap **RST**: Alpine should boot from the stick
as before (step 14b).
