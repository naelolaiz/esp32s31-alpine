# Step 22a: GrieferPig's image with Wi-Fi channels 12 and 13

Goal: the board joins a router on 2.4 GHz channel 12 or 13. That needs a
rebuild of GrieferPig's image with one patch, then flashing every slot
except `persist`, so our `/sbin/init` and the Wi-Fi profile stay.

## 1. Why a rebuild

On 2026-10-11 the router's automatic channel choice put its 2.4 GHz network
on channel 13 (2472 MHz). The board saw 38 other networks, never that one,
and wpa_supplicant stayed in `SCANNING`
([step 22](22-alpine-wifi.md), section 2). A router set to a fixed channel
from 1 to 11 needs no rebuild; with the rebuild, the router can keep its
automatic choice.

- His radio firmware already allows channels 1 to 13: `s31_wifi_prepare()`
  in `firmware/radio/radio_stack.c` sets the ESP-IDF country `CN` with
  channels 1 to 13, and its `S31_WIFI_SET_CHANNEL` accepts up to 13.
- His Linux driver stops at 11. `drivers/net/wireless/espressif/esp32s31_softmac.c`
  in `linux-esp32-s31` registers an 11-entry channel table with mac80211
  and drops received frames from any channel above 11.

[`grieferpig/patches/linux-esp32-s31/0001-wifi-esp32s31-softmac-channels-12-and-13.patch`](../../grieferpig/patches/linux-esp32-s31/0001-wifi-esp32s31-softmac-channels-12-and-13.patch)
changes those two lines. With the world regulatory domain, cfg80211 first
scans channels 12 and 13 passively (it listens for beacons) and allows
transmitting there once it has heard one.

That driver is part of his radio module (`esp32s31-radio.ko`, in his
squashfs root). His build makes the module together with the kernel (the
kernel exports only the symbols the radio needs), the radio firmware
(built with ESP-IDF, prelinked against the kernel's addresses) and the
root filesystem. So the whole image is rebuilt and flashed as one set.

Both builds below use his repository at commit `1c132b4` (2026-10-05) with
its pinned submodules, ESP-IDF `a602e67b` and his toolchain
`esp32s31-linux-gcc-15.2.0-5`, and the steps of his own release workflow.

## 2. Build on GitHub Actions

[`.github/workflows/grieferpig-image.yml`](../../.github/workflows/grieferpig-image.yml)
runs on every push that changes `grieferpig/patches/` or the workflow. The
run's artifact `grieferpig-images` holds:

| File | Flash offset |
| --- | --- |
| `spl_app.bin` | `0x2000` |
| `u-boot.itb` | `0xE000` |
| `esp32s31_generic.dtb` | `0x5E000` |
| `radio.bin` | `0x6E000` |
| `xipImage` | `0x400000` |
| `rootfs.sqfs` | `0xA00000` |

plus `SHA256SUMS`, `esp32s31-layout.cfg` (where the offsets come from),
`linux.config` and `BUILD-INFO.txt`. Download it from the run page (the
Artifacts box at the bottom) and unpack it into a directory
`grieferpig-images` in the porting directory, next to the `esp32s31-alpine`
clone, then go to section 4.

## 3. Build on the PC

HOST, in the porting directory. Everything goes into a new directory,
`grieferpig-build`, ESP-IDF's tools included.

### 3.1 Host tools

His reference host is Ubuntu 24.04; on another distribution the same tools
have to be there. This prints only what is missing:

```sh
for c in git curl wget file cmake bison bc flex gperf ninja swig rsync unzip xz mksquashfs dtc cpio; do command -v $c >/dev/null || echo "missing: $c"; done
python3 -c 'import pkg_resources, elftools' && echo python-ok
```

On Gentoo the packages are `dev-build/cmake`, `sys-devel/bison`,
`sys-devel/bc`, `sys-devel/flex`, `dev-util/gperf`, `dev-build/ninja`,
`dev-lang/swig`, `net-misc/rsync`, `app-arch/unzip`, `app-arch/xz-utils`,
`sys-fs/squashfs-tools`, `sys-apps/dtc`, `app-alternatives/cpio`, and for the
Python check `dev-python/setuptools` (`pkg_resources`) and
`dev-python/pyelftools`. U-Boot's build scripts import those two Python
modules.

### 3.2 His repository

```sh
mkdir grieferpig-build
cd grieferpig-build
git clone https://github.com/GrieferPig/esp32-s31-linux.git
git -C esp32-s31-linux checkout 1c132b4ad991960d10810d6138af317b01cf74c6
git -C esp32-s31-linux submodule update --init --recursive --depth 1
```

- The same commit as the GitHub build, so both make the same set.
- `--depth 1` fetches only the pinned commit of each submodule, not the
  whole history of his Linux fork.

### 3.3 ESP-IDF

```sh
export IDF_PATH=$PWD/esp-idf IDF_TOOLS_PATH=$PWD/espressif
git clone --filter=blob:none https://github.com/espressif/esp-idf.git esp-idf
git -C esp-idf checkout a602e67b0bf9ee0806dc4e1df7afc9affedf5c33
git -C esp-idf submodule update --init --recursive
esp-idf/install.sh esp32s31
```

- The exact revision from his `configs/build-versions.mk`; `make` refuses
  another one.
- `IDF_TOOLS_PATH` keeps ESP-IDF's compilers and Python environment in this
  directory instead of `~/.espressif`.
- Shell variables last for one terminal: in a new terminal, `cd` into
  `grieferpig-build` and run the `export` line again.

### 3.4 Downloads and the patch

```sh
cd esp32-s31-linux
export HOST_PYTHON=/usr/bin/python3
make doctor
make fetch
curl -fLO https://raw.githubusercontent.com/naelolaiz/esp32s31-alpine/claude/native-wifi-ensw69/grieferpig/patches/linux-esp32-s31/0001-wifi-esp32s31-softmac-channels-12-and-13.patch
git -C linux-esp32-s31 apply --verbose ../0001-wifi-esp32s31-softmac-channels-12-and-13.patch
grep -n 'channels\[13\]' linux-esp32-s31/drivers/net/wireless/espressif/esp32s31_softmac.c
```

- `HOST_PYTHON`: the Python that U-Boot's tools run with, the one that
  passed the import check.
- `make doctor` checks the host and the ESP-IDF checkout. `make fetch`
  downloads his toolchain, BTstack and the root filesystem sources.
- The patch goes in after `make fetch`, because `fetch` runs
  `git submodule update` on the submodules. `git -C` runs inside the Linux
  submodule, so the patch path is relative to it.

### 3.5 Build

```sh
. "$IDF_PATH/export.sh"
make image
ls -l dist/current/
```

- `export.sh` puts ESP-IDF's compilers and Python on `PATH` for this
  terminal.
- `make image` builds the boot chain, kernel, radio firmware, radio image
  and root filesystem, checks them, and publishes them to `dist/current/`.
  `JOBS=4` before `image` limits parallel jobs if the PC runs out of RAM.

## 4. Back up the flash

BOARD in download mode (hold BOOT, tap RST). HOST, in the porting
directory, with the esptool venv of the `esp32s31-alpine` clone:

```sh
esp32s31-alpine/.venv/bin/esptool --chip esp32s31 --port /dev/ttyUSB0 --baud 1152000 read-flash 0 0x1000000 s31_full_flash-gp-alpine.bin
```

All 16 MiB as they are now: his release, and `persist` with our
`/sbin/init` and the Wi-Fi profile. `write-flash 0x0` of this file undoes
section 5. It stays outside the clone, so it never ends up in git: `persist`
holds the Wi-Fi key.

## 5. Flash every slot except persist

`persist` (`0x1EE000` to `0x400000`) is not written, so the hand-over to
Alpine and the Wi-Fi profile stay.

From a PC build, HOST, in `grieferpig-build/esp32-s31-linux`, with the
ESP-IDF environment of section 3.5, board in download mode:

```sh
make PORT=/dev/ttyUSB0 BAUD=1152000 flash-existing-all
```

His target checks the `dist/current` set, checks that the board has the
same layout, and writes the six slots.

From the GitHub artifact, HOST, in `grieferpig-images` (section 2), board in
download mode:

```sh
sha256sum -c SHA256SUMS
../esp32s31-alpine/.venv/bin/esptool --chip esp32s31 --port /dev/ttyUSB0 --baud 1152000 write-flash --flash-mode dio --flash-freq 80m --flash-size 16MB 0x2000 spl_app.bin 0xE000 u-boot.itb 0x5E000 esp32s31_generic.dtb 0x6E000 radio.bin 0x400000 xipImage 0xA00000 rootfs.sqfs
```

The offsets are the ones in `esp32s31-layout.cfg`. Tap RST afterwards, with
the stick plugged in.

## 6. Check on the board

BOARD, Alpine, as root, then section 2 of [step 22](22-alpine-wifi.md):

```sh
dmesg | grep -i -e 'radio XIP' -e esp32s31-radio | head -5
```

The radio module loads from the new squashfs and finds the new radio
image. Section 2's wpa_supplicant should then reach `wpa_state=COMPLETED`
with the router on channel 13.
