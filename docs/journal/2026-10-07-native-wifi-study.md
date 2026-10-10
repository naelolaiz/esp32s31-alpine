# 2026-10-07: How native Wi-Fi can work on the ESP32-S31 (step 19)

The Ethernet cable is unplugged from now on, so Wi-Fi has to carry apk and
SSH. The shipped kernel has no Wi-Fi driver. This entry compares the three
community ports that do have Wi-Fi with Espressif's boot chain, and records
which design phase 5 follows and why. Board checks for this step:
[`docs/steps/19-native-wifi-study.md`](../steps/19-native-wifi-study.md).

Sources read (shallow clones, commit dates in brackets):

| Port | Commit | Kernel | Boot chain |
| --- | --- | --- | --- |
| [vanbuong/esp32-s31-linux](https://github.com/vanbuong/esp32-s31-linux) | 2988826 (2026-09-21) | upstream 7.1.3 + patches | ROM, ESP-IDF second stage, ESP-IDF loader, OpenSBI 1.9, Linux |
| [platima/esp32-s31-linux](https://github.com/platima/esp32-s31-linux) (fork of vanbuong) | be5b725 (2026-09-25) | upstream 7.2.7 + patches | same as vanbuong |
| [GrieferPig/esp32-s31-linux](https://github.com/GrieferPig/esp32-s31-linux) | 1c132b4 (2026-10-05) | [linux-esp32-s31](https://github.com/GrieferPig/linux-esp32-s31) `v6.18-esp32-s31` (52dc6ea4) | ROM, SPL, U-Boot 2024.07, OpenSBI 1.9 fork, Linux |
| Espressif (ours) | see [bsp/versions.md](../../bsp/versions.md) | `integration/v6.18-esp32s31` (dc0e382) | ROM, SPL, OpenSBI 1.6 (5395e03), U-Boot 2024.07, Linux |

## Why Wi-Fi is hard on this chip

The Wi-Fi hardware (MAC and baseband) has no public register documentation.
Espressif ships the 802.11 logic only as precompiled libraries for ESP-IDF,
its FreeRTOS-based SDK: `libnet80211.a`, `libpp.a`, `libphy.a` and others, in
[esp32-wifi-lib](https://github.com/espressif/esp32-wifi-lib) (Apache-2.0,
`esp32s31/` directory, af55a0c). So every design has to run these libraries
somewhere, and the libraries expect ESP-IDF around them: FreeRTOS tasks,
queues, timers, an interrupt allocator and a heap in internal SRAM.

On other Espressif chips the usual Linux answer is
[ESP-Hosted](https://github.com/espressif/esp-hosted): a second chip runs
ESP-IDF and talks to Linux over SPI or SDIO. vanbuong's README explains why
that does not carry over here: ESP-Hosted's full-MAC firmware needs hooks
that Espressif adds to patched Wi-Fi libraries for other chips, and not for
the ESP32-S31.

The question is therefore where the ESP-IDF Wi-Fi code runs: on its own CPU
core, or inside Linux.

## Design A: ESP-IDF owns hart 0 (vanbuong, platima)

The chip has two cores (harts). These ports never hand hart 0 to Linux:

1. The ROM starts hart 0 and runs Espressif's normal ESP-IDF second-stage
   bootloader, which starts an ESP-IDF application (the "loader") from flash
   0x20000. It is built single-core (`CONFIG_FREERTOS_UNICORE=y`,
   `bootloader/sdkconfig.defaults:2`).
2. The loader copies the kernel and an initramfs into PSRAM, copies OpenSBI
   to 0x50E00000, starts Wi-Fi, and only then releases hart 1:
   `esp_cpu_unstall(1)`, clock and reset on, and `ets_set_appcpu_boot_addr()`
   pointing at a small stub that jumps into OpenSBI (`bootloader/main/main.c:236-241`
   in vanbuong).
3. Hart 0 stays in ESP-IDF forever, in M-mode, as the Wi-Fi firmware. Linux
   runs uniprocessor on hart 1.

Linux and the firmware talk through 64 KiB of internal SRAM at 0x2F050000
(`shared/esp32s31-wifi-ipc.h:33`): a command block, a scan table and two
rings of 16 slots of 1536 bytes, one per direction. The data are whole
Ethernet (802.3) frames, so the Linux driver `esp32s31-wifi` is a
**full-MAC** cfg80211 driver: the firmware does association, encryption and
the WPA handshake itself, and Linux only sees an Ethernet-like `wlan0`. Each
side rings the other with a cross-core interrupt register (`FROM_CPU_n` in
HP_SYSTEM). platima moved the firmware-to-Linux doorbell from `FROM_CPU_0` to
`FROM_CPU_2` because ESP-IDF's own cross-core handler on hart 0 clears
`FROM_CPU_0` (platima `docs/internals.md:188-190`); platima's README now lists
Wi-Fi with `wpa_supplicant` as working.

Internal SRAM is not cached, so both harts see the rings directly, without
cache maintenance. That is why the rings are there and not in PSRAM.

What this means for us:

- It replaces SPL, U-Boot and OpenSBI 1.6 with an ESP-IDF loader and
  OpenSBI 1.9, and runs the kernel from PSRAM instead of executing it in
  place from flash. That breaks this project's ground rule: Espressif's boot
  stack stays as shipped.
- The kernels are upstream 7.1 and 7.2 with their own CLIC interrupt driver
  and device tree, not Espressif's 6.18. The Wi-Fi driver also uses APIs from
  after 6.18 (for example `.get_station` takes a `wireless_dev` since 6.19,
  inferred by the subagent that read the code, not checked against a tree).
- Linux can never use hart 0, and M-mode firmware on hart 0 can overwrite
  anything Linux owns.

## Design B: the Wi-Fi blob runs inside Linux (GrieferPig)

GrieferPig keeps a chain shaped like Espressif's: SPL, U-Boot 2024.07 and
OpenSBI, then an XIP Linux 6.18. The first eight kernel commits on top of
v6.18 have the same hashes as in Espressif's `integration/v6.18-esp32s31`
(`690c4c549` to `b0c63aaed`), so the fork starts from Espressif's own work.

The Wi-Fi libraries do not get a core of their own. They run inside Linux,
in S-mode:

- `firmware/radio/` links the ESP-IDF Wi-Fi and Bluetooth libraries into one
  relocatable object, renames their FreeRTOS, heap, timer and interrupt calls
  to Linux-side replacements, and prelinks the result to execute in place
  from flash (a 1.5 MiB slot at flash 0x6E000), with its hottest functions
  copied into SRAM. The Makefile says it "contains no OpenSBI objects and
  performs no M-mode setup" (`firmware/radio/Makefile:90-91`).
- In the kernel, `drivers/platform/esp32s31-radio-*` (loader, a FreeRTOS
  emulation where each ESP-IDF task becomes a kernel thread, interrupt glue)
  and `drivers/net/wireless/espressif/esp32s31_softmac.c` make a
  **soft-MAC** `mac80211` driver: Linux builds the 802.11 frames, and the
  normal `wpa_supplicant` does the WPA2 handshake over nl80211. About 8,500
  lines in all.
- Linux is SMP on both harts in that port, but the radio does not need SMP:
  its worker thread is bound to CPU 0 and it falls back to CPU 0 when CPU 1 is
  offline (`docs/en/api-reference/radio/architecture.md`).

It costs about 282 KiB of the 512 KiB internal SRAM (radio heap and buffers
0x2F030000-0x2F071800, a high heap up to 0x2F07CFB0,
`shared/s31_memory_layout.h`), up to 1.5 MiB of flash, and a 40 KiB arena in
the kernel's PSRAM. Only WPA2-Personal with CCMP and one station interface
are supported; no access point mode.

## Espressif's chain today

Read from `espressif/opensbi` (5395e03) and `espressif/linux` (dc0e382):

- **Hart 1 is unused.** The kernel device tree has only `cpu@0`, and
  `esp32s31_minimal_defconfig` has no `CONFIG_SMP`.
- **But Espressif is moving towards SMP.** On 2026-10-06 OpenSBI merged
  `feat/esp32s31_smp`, which adds an SBI HSM device that starts and stops
  hart 1 (`platform/generic/espressif/esp32s31/hsm.c`): stall release in
  `PMU_CPU_STALL_SW_REG`, clock and reset bits in `HP_SYS_CLKRST_HPCORE1_CTRL0`,
  and the ROM's `ets_set_cpu_boot_addr`. Whether the OpenSBI on our board
  already has it depends on when Buildroot fetched the branch; the board
  checks in the guide answer that.
- **SRAM.** OpenSBI is linked at 0x2F000000 (`FW_TEXT_START`). The kernel
  device tree reserves 0x2F030000-0x2F070000 (256 KiB) as the default DMA pool
  (`dma-pool@2f030000`, `shared-dma-pool`), which the Ethernet and USB
  drivers use for memory both the CPU and the DMA engine see without cache
  maintenance. That is exactly the area GrieferPig's radio uses, and vanbuong's
  IPC block (0x2F050000) is inside it too. Every design has to shrink or move
  this pool.
- **Access control is open.** OpenSBI grants every master read, write and
  execute on every APM region and maps both harts' S-mode to M-level access
  (`apm.c`), so Linux in S-mode can reach the radio, PMU and clock registers
  that the Wi-Fi code needs.

## The F extension: the step 2 conclusion was wrong

Step 2 concluded "no FPU" from `/proc/cpuinfo`, which prints
`rv32imac_...`. That line is not read from the hardware: the kernel builds
it from the device tree's `riscv,isa-extensions`, and Espressif's tree lists
only `i m a c zicsr zifencei`. Evidence that the cores do have single
precision floating point (F, without D):

- Espressif's own Wi-Fi libraries for this chip are built for it:

  ```
  $ readelf -h esf_buf.o | grep Flags      # from esp32s31/libpp.a
    Flags:                             0x3, RVC, single-float ABI
  $ readelf -A esf_buf.o | grep arch
    Tag_RISCV_arch: "rv32i2p0_m2p0_a2p0_f2p0_c2p0"
  ```

- GrieferPig's device tree lists `rv32imafbcn_zba_zbb_zbc_zbs`, and his
  kernel carries an F-only context switch.
- The board's Buildroot `/lib` has an `ld-musl-riscv32-sp.so.1` loader next to
  the `-sf` one (step 12): Espressif's toolchain ships a single-precision
  (`ilp32f`) multilib.

Not measured on our board yet. It matters for design B because the Wi-Fi
libraries execute F instructions, and stock Linux refuses an F-only hart:
`arch/riscv/kernel/cpufeature.c:1083` drops F with "This kernel does not
support systems with F but not D". GrieferPig patches that check and the
context switch (`fsw`/`flw` instead of `fsd`/`fld`). Alpine's ABI is not
affected: userspace stays `ilp32` soft-float, as Espressif's Buildroot
userspace is.

## Options for our board

| | A: firmware owns a hart | B: radio inside Linux | C: firmware on hart 1 |
| --- | --- | --- | --- |
| Who did it | vanbuong, platima (works) | GrieferPig (works, "experimental") | nobody |
| Boot chain | replaced (ESP-IDF loader, OpenSBI 1.9) | **unchanged** | unchanged |
| Kernel | upstream 7.1/7.2 | Espressif's 6.18 plus a driver port | Espressif's 6.18 plus a driver |
| Linux harts | hart 1 only | hart 0 (SMP possible later) | hart 0 only |
| Wi-Fi model | full-MAC, sysfs or nl80211 keys | soft-MAC mac80211, plain `wpa_supplicant` | full-MAC (vanbuong's driver) |
| SRAM | 64 KiB IPC + ESP-IDF heap | about 282 KiB | IPC + ESP-IDF heap |
| Main risk | breaks the ground rule | large port; SRAM layout is fragile (`s31_memory_layout.h:17-27`) | ESP-IDF has no start-up path for the second core alone |

C needs a word: Linux would keep hart 0 as today, and a driver would start
an ESP-IDF image on hart 1 the way Espressif's new HSM code does. But ESP-IDF
initialises the system from the first core, and a single-core ESP-IDF build
on the second core, sharing flash and cache with an XIP kernel, is new
ground. It also conflicts with where Espressif is heading: hart 1 for Linux.

## Decision

Chosen on 2026-10-10: **B, the radio inside Linux**. It is the only design
that keeps Espressif's boot stack as shipped, it starts from a kernel already
based on Espressif's commits, and it gives Alpine a normal `wlan0` driven by
`wpa_supplicant`, `iw` and `udhcpc`.

What B needs, in the order phase 5 does it:

1. Measure the SRAM and the hart-1 state on the board (step 19 guide).
2. Shrink the DMA pool so 0x2F030000-0x2F07CFB0 is free for the radio, and
   check that Ethernet and the USB stick still work (device tree only).
3. F-only FPU support in the kernel: the `cpufeature.c` check, the F-only
   context switch, and `f` in the device tree.
4. Port the radio loader, FreeRTOS emulation and soft-MAC driver onto
   Espressif's 6.18, adapting the interrupt glue to Espressif's CLIC and
   interrupt matrix drivers (`esp,esp32s31-intmtx`). This starts the
   `esp32s31-wifi` repository.
5. Build the radio payload with ESP-IDF `a602e67b` (pinned in GrieferPig's
   `configs/build-versions.mk`) and flash it into a free flash slot.
6. Alpine: `wpa_supplicant` and `iw` from aports, an OpenRC setup for
   `wlan0`, and apk and SSH over Wi-Fi.
