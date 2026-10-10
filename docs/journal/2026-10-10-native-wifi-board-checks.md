# 2026-10-10: What the board offers native Wi-Fi (step 19 results)

The checks from the [step 19 guide](../steps/19-native-wifi-study.md), run on
the board and on the Espressif build that produced its flash. The design
they were measured for is in the
[study](2026-10-07-native-wifi-study.md): the ESP-IDF Wi-Fi libraries run
inside Linux, as in GrieferPig's port, and need internal SRAM from
0x2F030000.

In short:

- OpenSBI ends below 0x2F030000, so the radio area is not taken by firmware.
- The only thing in the radio's way is the kernel's 256 KiB DMA pool, which
  step 20 shrinks and moves.
- Our OpenSBI cannot start hart 1. The radio design does not need it.
- The kernel has `CONFIG_FPU=y`, no modules and no wireless stack.

The optional section 5 (Wi-Fi with GrieferPig's image) was run later the same
day: [results](2026-10-10-grieferpig-trial.md).

## OpenSBI prints no banner

Upstream OpenSBI prints a banner with its memory layout and PMP regions.
Espressif's boot shows only the platform's own line between SPL and U-Boot:

```
U-Boot SPL 2024.07 (Oct 06 2026 - 21:19:18 +0200)
ESP32-S31 SPL active
flash-qio: QIO @ 80 MHz enabled
Trying to boot from NOR
OpenSBI: ESP32-S31 firmware active


U-Boot 2024.07 (Oct 06 2026 - 21:19:18 +0200)
```

U-Boot SPL most likely starts OpenSBI with the "no boot prints" option
(`CONFIG_SPL_OPENSBI_SCRATCH_OPTIONS=0x1`, U-Boot's default; inferred, not
read in Espressif's SPL config). Linux confirms which OpenSBI it talks to:

```
[    0.000000] SBI specification v2.0 detected
[    0.000000] SBI implementation ID=0x1 Version=0x10006
```

Implementation ID 1 is OpenSBI and version 0x10006 is 1.6, as Buildroot's
`integration/v1.6-esp32s31` branch says.

## OpenSBI's size and features, read from the build

The firmware that U-Boot SPL loads is `fw_dynamic.bin`, packed into
`images/u-boot.itb`. Its ELF twin in the Buildroot build tree gives the
layout:

```
$ readelf -lW $(find build -name fw_dynamic.elf | head -1)
Entry point 0x2f000000
  Type           Offset   VirtAddr   PhysAddr   FileSiz MemSiz  Flg Align
  RISCV_ATTRIBUT 0x01caec 0x00000000 0x00000000 0x0006c 0x00000 R   0x1
  LOAD           0x001000 0x2f000000 0x2f000000 0x1a900 0x1a900 R E 0x1000
  LOAD           0x01c000 0x2f020000 0x2f020000 0x00aec 0x03b88 RW  0x1000
  DYNAMIC        0x01ca18 0x2f020a18 0x2f020a18 0x00090 0x00090 RW  0x4
  GNU_STACK      0x000000 0x00000000 0x00000000 0x00000 0x00000 RW  0x10
```

| Part | From | To | Size |
| --- | --- | --- | --- |
| Code and read-only data | 0x2F000000 | 0x2F01A900 | 106 KiB |
| Data and BSS | 0x2F020000 | 0x2F023B88 | 15 KiB |
| Stacks and heap (after BSS, set at run time) | 0x2F023B88 | about 0x2F02E000 | about 40 KiB (estimated) |

- OpenSBI fences itself off from S-mode with two PMP regions: one for the
  code (from 0x2F000000 up to where the RW part starts, 128 KiB) and one for
  the RW part with its stacks and heap. A PMP region of this kind is a power
  of two in size and aligned to it, so the RW part rounds up to 64 KiB, and
  the fence most likely ends at exactly 0x2F030000 (from how OpenSBI 1.6 sets
  up its regions, not measured). What is measured: Ethernet uses the DMA pool
  from 0x2F030000 without faulting, so Linux can reach SRAM from there.
- GrieferPig's OpenSBI is smaller and lends the radio a further 96 KiB of
  heap below 0x2F030000 (`0x2F018000` to `0x2F030000`). Ours keeps that space,
  so the radio gets the main area and the high heap only. His driver turns
  the low heap off by itself when OpenSBI does not offer it; whether the
  radio copes with less heap under load is a step 21 question.

Hart 1 support, from the strings in the same image:

```
$ grep -a -o -e esp32s31-reset -e esp32s31-hsm images/u-boot.itb
esp32s31-reset
```

`esp32s31-reset` is OpenSBI's reset driver, present in every version, so the
search works. `esp32s31-hsm` is Espressif's hart 1 start code (`hsm.c`,
merged on 2026-10-06), and it is missing: Buildroot fetched the branch before
that merge (U-Boot and OpenSBI were built on 2026-10-06 at 21:19 CEST). Only
SMP later would need a newer OpenSBI; the radio design runs on one hart.

## U-Boot and the device tree

```
## Flattened Device Tree blob at 40200000
   Booting using the fdt blob at 0x40200000
ERROR: reserving fdt memory region failed (addr=2f030000 size=40000 flags=4)
   Loading Device Tree to 50ffb000, end 50fff6a2 ... OK
```

- 0x40200000 is the DTB slot (flash 0x300000): SPL maps flash from 0x100000
  at 0x40000000 (`board/espressif/esp32s31/spl.c`).
- U-Boot copies the tree to the top of PSRAM (0x50FFB000 to 0x50FFF6A2)
  before starting Linux, so it does not land in SRAM. The copy is 18 KiB
  while the DTB file is 5795 bytes: U-Boot leaves free room after the tree
  for its own changes (inferred, from the sizes only).
- The `ERROR` line is U-Boot failing to add the DMA pool to its own memory
  map. U-Boot's memory bank is SRAM at 0x2F030000 (320 KiB, the `DRAM:` line)
  and it runs from there itself, which is probably why the reservation is
  refused (inferred). Linux reserves the pool correctly a moment later, so it
  is harmless.

## What Linux reserved

```
[    0.000000] Reserved memory: created DMA memory pool at 0x2f030000, size 0 MiB
[    0.000000] OF: reserved mem: initialized node dma-pool@2f030000, compatible id shared-dma-pool
[    0.000000] OF: reserved mem: 0x2f030000..0x2f06ffff (256 KiB) nomap non-reusable dma-pool@2f030000
```

The kernel prints the pool size in whole MiB, hence `0 MiB`; the third line
gives the real 256 KiB. In the tree the kernel booted with
(`/proc/device-tree`):

- `reserved-memory/` holds only `dma-pool@2f030000`, so the pool is the only
  reservation in SRAM or PSRAM.
- `hexdump -C` of its `reg` reads `2f 03 00 00 00 04 00 00`: start
  0x2F030000, size 0x40000.
- `cpus/cpu@0/riscv,isa` is `rv32imac_zicsr_zifencei`, and the kernel says
  `riscv: base ISA extensions acim`. The tree lists no `f`, so this says
  nothing about the F extension either way.
- `/sys/devices/system/cpu/possible` and `online` are both `0`: Linux runs on
  hart 0 only.

The swiotlb bounce buffer (`swiotlb=128` on the command line) is in PSRAM,
not SRAM (0x50FAA000 to 0x50FEA000 on step 15's boot).

## Kernel options

```
$ grep -E '^(# )?CONFIG_(SMP|FPU|MODULES|WLAN|CFG80211|MAC80211|RFKILL|CRYPTO_CCM|CRYPTO_AES)[ =]' build/linux-*/.config
# CONFIG_SMP is not set
CONFIG_FPU=y
# CONFIG_MODULES is not set
# CONFIG_CFG80211 is not set
# CONFIG_RFKILL is not set
CONFIG_WLAN=y
```

- `CONFIG_FPU=y`, but stock Linux still drops F on a hart without D, so the
  F-only patch is needed (step 20).
- No modules: the radio driver is built into the kernel.
- `MAC80211` has no line because it depends on `CFG80211`. `CRYPTO_CCM` and
  `CRYPTO_AES` have no line either, so they are off or depend on something
  that is off (not checked further). Step 21 turns the wireless stack on.

## Internal SRAM today, and what the radio needs

| Range | Today | Radio layout (GrieferPig) |
| --- | --- | --- |
| 0x2F000000-0x2F030000 | OpenSBI | OpenSBI, plus the radio's low heap from 0x2F018000 (not possible with ours) |
| 0x2F030000-0x2F071800 | kernel DMA pool | radio main area: heap, buffers, Wi-Fi code in SRAM |
| 0x2F071800-0x2F072380 | kernel DMA pool | radio exception stack and guards |
| 0x2F072380-0x2F078C00 | 0x2F070000 up: free at run time | DMA descriptors of his own drivers |
| 0x2F078C00-0x2F07CFB0 | free at run time | radio high heap |
| 0x2F07CFB0-0x2F080000 | ROM data | ROM data |

So step 20 moves the DMA pool into the descriptor gap at 0x2F072380, where
GrieferPig's layout keeps DMA descriptors too. "Free at run time" means
that only U-Boot and SPL used it, and they are gone once Linux runs.
