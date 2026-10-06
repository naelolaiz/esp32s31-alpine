# Step 4: first own kernel (block layer)

Goal: learn the rebuild-and-flash loop on the smallest useful change, and
measure what the block layer costs in flash and RAM before any driver work.

The change is [kernel/fragments/10-block.config](../../kernel/fragments/10-block.config):
block layer, loop devices, ext4 and vfat. No storage drivers yet.

## 1. Rebuild

From the top of this repository, pass your Buildroot checkout and the output
directory of the Espressif build from step 2:

```sh
scripts/build-kernel.sh <buildroot-dir> <buildroot-output-dir>
```

The script adds every `kernel/fragments/*.config` to the Buildroot config,
rebuilds the kernel, repacks `s31_full_flash.bin`, and prints the new kernel
options and image sizes. The step 2 `xipImage` was 3,086,101 bytes.

## 2. Flash, boot, check

Flash as in step 2, boot, then on the board:

```sh
free
grep -E 'ext4|vfat' /proc/filesystems
ls /dev/loop* 2>/dev/null | head -3
dmesg | grep -i -E 'memory:|error|fail'
```

Compare with step 2: 12560 KiB free, "Memory: 14512K/16384K available".

Done when: the new kernel boots, lists ext4 and vfat, and the cost is recorded.
Next: step 5 (microSD) needs the breakout wired to GPIO20 to 25.
