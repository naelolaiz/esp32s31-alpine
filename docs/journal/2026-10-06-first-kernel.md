# 2026-10-06: First own kernel with the block layer (step 4)

Rebuilt Espressif's kernel with
[kernel/fragments/10-block.config](../../kernel/fragments/10-block.config)
(block layer, loop, ext4, vfat) using `scripts/build-kernel.sh`, flashed the
full image and booted it. Banner: `#2 Tue Oct 6 23:09:46 CEST 2026`.

## Results

- `/proc/filesystems` lists `ext4` and `vfat`; `/dev/loop0` to `loop7` and
  `/dev/loop-control` exist.
- No new errors. The only lines matching `error|fail` are the two seen on the
  baseline (`Failed to add a System RAM resource`) plus
  `check access for rdinit=/init failed: -2, ignoring`, which is normal: there
  is no initramfs, so the kernel falls back to the cramfs root.
- The first attempt booted the old kernel because the old image was flashed
  before the rebuild finished. `uname -v` (build number and time) is the quick
  check that the new kernel is the one running.

## Cost

| Item | Step 2 | Step 4 | Change |
| --- | --- | --- | --- |
| `xipImage` (flash) | 3,086,101 B | 3,631,269 B | +545,168 B (+532 KiB) |
| Kernel code (XIP, flash) | 2320 KiB | 2774 KiB | +454 KiB |
| rodata | 395 KiB | 464 KiB | +69 KiB |
| rwdata + bss (RAM) | 145 + 71 KiB | 150 + 105 KiB | +39 KiB |
| "Memory: ... available" | 14512 KiB | 14464 KiB | -48 KiB |
| `free`: free | 12560 KiB | 11488 KiB | -1072 KiB |
| `free`: available | 12112 KiB | 11056 KiB | -1056 KiB |

`s31_full_flash.bin` stays 16,080,896 B because it is padded to the slot layout,
so the larger kernel still fits its flash slot.

The static RAM cost is small (about 48 KiB), but about 1 MiB less is free after
boot. Inferred, not yet measured: the difference is runtime allocations of the
block layer, such as the eight loop devices with their request queues, bio
mempools and ext4/jbd2 slab caches. To check later: compare `/proc/meminfo` and
`/proc/slabinfo` between the two kernels, and try
`CONFIG_BLK_DEV_LOOP_MIN_COUNT=0` (loop devices are then created on demand).

## Next

Step 6 (USB pendrives) goes before step 5 (microSD): it needs only kernel
options, while microSD needs a breakout wired to the header and a driver port.
Neither depends on the other.
