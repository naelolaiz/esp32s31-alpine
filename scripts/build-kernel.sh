#!/bin/sh
# Rebuild Espressif's kernel with this repo's kernel/fragments/*.config and
# repack the flash image.
#
# Usage: scripts/build-kernel.sh BUILDROOT_DIR OUTPUT_DIR
#   BUILDROOT_DIR  your Buildroot 2025.02 checkout
#   OUTPUT_DIR     the Buildroot output directory of the Espressif build (has .config)
set -eu

[ $# -eq 2 ] || { echo "usage: $0 BUILDROOT_DIR OUTPUT_DIR" >&2; exit 1; }
br=$(cd "$1" && pwd)
out=$(cd "$2" && pwd)
repo=$(cd "$(dirname "$0")/.." && pwd)

frags=$(ls "$repo"/kernel/fragments/*.config | tr '\n' ' ' | sed 's/ $//')
echo "fragments: $frags"

sed -i '/^BR2_LINUX_KERNEL_CONFIG_FRAGMENT_FILES=/d' "$out/.config"
echo "BR2_LINUX_KERNEL_CONFIG_FRAGMENT_FILES=\"$frags\"" >> "$out/.config"
make -C "$br" O="$out" olddefconfig
make -C "$br" O="$out" linux-reconfigure
make -C "$br" O="$out"

echo
# Show whether each option from the fragments made it into the kernel config
kconf=$(ls "$out"/build/linux-*/.config | head -1)
grep -h '^CONFIG_' "$repo"/kernel/fragments/*.config | while read -r opt; do
	if grep -qx "$opt" "$kconf"; then echo "ok       $opt"; else echo "MISSING  $opt"; fi
done
ls -l "$out/images/xipImage" "$out/images/s31_full_flash.bin"
