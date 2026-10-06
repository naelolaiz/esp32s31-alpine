#!/bin/sh
# Plan step 2: build Espressif's Buildroot image for the Function-CoreBoard-1.
# Usage: scripts/build-baseline.sh [WORKDIR]   (default: ../work next to this repo)
set -eu

repo=$(cd "$(dirname "$0")/.." && pwd)
work=${1:-"$repo/../work"}
mkdir -p "$work"
cd "$work"

[ -d buildroot ] || git clone --branch 2025.02 --depth 1 \
	https://gitlab.com/buildroot.org/buildroot.git buildroot
[ -d esp-buildroot-external ] || git clone --branch buildroot/v2025.02-esp32s31 \
	https://github.com/espressif/esp-buildroot-external.git

if [ ! -x venv/bin/esptool ]; then
	python3 -m venv venv
	venv/bin/pip install -q 'esptool>=5.3' pyserial
fi

make -C buildroot BR2_EXTERNAL="$work/esp-buildroot-external" O="$work/out" \
	espressif_esp32s31_function_core_board_nor_defconfig
ESP_ESPTOOL="$work/venv/bin/esptool" \
	make -C buildroot O="$work/out" -j"$(nproc)" 2>&1 | tee "$work/build.log"

echo "esp-buildroot-external: $(git -C esp-buildroot-external rev-parse HEAD)"
ls -l out/images/s31_full_flash.bin out/images/rootfs.cramfs out/images/xipImage 2>/dev/null || ls -l out/images
