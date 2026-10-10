# SMP: start Linux on the second hart

Goal: see on the board whether Linux can start the ESP32-S31's second core
(hart 1), and learn which layer stops it. This is an experiment, not working
SMP. The study behind it is in the
[journal](../journal/2026-10-10-smp-study.md); in short, Espressif's newest
OpenSBI and U-Boot can start hart 1, but Espressif's kernel has none of the
timer, interrupt and IPI code that a second hart needs, so hart 1 will come
up and then stall.

| Section | What it changes | Undo |
| --- | --- | --- |
| 1. What the build has now | nothing | |
| 2. An SMP kernel on one hart | kernel config, one kernel patch, the DTB | flash two saved files |
| 3. Hart 1 with today's boot stack | nothing (a runtime test) | RST |
| 4. Espressif's SMP boot stack | SPL and U-Boot with OpenSBI (**the boot stack**) | flash two saved slots |
| 5. Hart 1 with the new boot stack | nothing (a runtime test) | RST |
| 6. Going back | | |

Section 4 changes the boot stack. The ground rule keeps it as Espressif
ships it; this is Espressif's own code from 2026-10-06, newer than what is on
the board. Decide before running section 4. Sections 1 to 3 are useful on
their own.

Do this between Wi-Fi steps, not in the middle of one: section 4 also
changes how OpenSBI sets up the interrupt controller on hart 0, and that
should not mix with a radio problem. The device tree here builds on step 20
part 1 (the DMA pool at 0x2F073000) if you have done it, and works without
it too.

## 1. What the build has now

HOST, in the Buildroot output directory of your Espressif build (the one
with `images/` and `build/`). The first command repeats the step 19 check
for Espressif's hart 1 start code in OpenSBI; the second lists the CPU nodes
and the OpenSBI heap setting in U-Boot's device tree, which is the one
OpenSBI reads:

```sh
grep -a -o -e esp32s31-reset -e esp32s31-hsm images/u-boot.itb
build/linux-integration_v6.18-esp32s31/scripts/dtc/dtc -I dtb -O dts images/u-boot.dtb | grep -n -e 'cpu@' -e 'heap-size'
```

- Step 19 found only `esp32s31-reset`: no hart 1 start code.
- Expect only `cpu@0` and no `heap-size`: U-Boot from before Espressif's
  2026-10-06 merge (most likely, not checked yet). A `cpu@1` here would
  mean U-Boot is already newer than OpenSBI.

## 2. An SMP kernel that runs on one hart

This builds the kernel with SMP support and tells it about hart 1, but keeps
it on hart 0 at boot (`maxcpus=1`). It shows what SMP costs in flash and RAM,
and it gives section 3 a kernel that can try to start hart 1 on request.

### Numbers before

BOARD, Alpine logged in as root, right after boot. The `Memory:` line is the
kernel's own account at boot; `MemAvailable` is what programs can still get:

```sh
dmesg | grep -i 'Memory:'
grep -E 'MemTotal|MemAvailable' /proc/meminfo
```

HOST, in the Buildroot output directory. The kernel image size now, and a
copy of it, because `make` overwrites `images/xipImage` and the copy is how
you go back:

```sh
ls -l images/xipImage
cp images/xipImage xipImage-before-smp
```

### Kernel options

HOST, same directory:

```sh
make linux-menuconfig
```

All three options are in **Platform type**:

| Option | Menu entry | Set to | Why |
| --- | --- | --- | --- |
| `SMP` | Symmetric Multi-Processing | `[*]` | Builds the kernel for more than one hart: real spinlocks, per-CPU data, the code that starts other harts through SBI. |
| `NR_CPUS` | Maximum number of CPUs | `2` | The default is 32, and Linux reserves per-CPU memory for every possible CPU up to this number. The chip has two. |
| `HOTPLUG_CPU` | Support for hot-pluggable CPUs | `[*]` | Lets you start hart 1 by hand at run time (`/sys/devices/system/cpu/cpu1/online`) instead of at boot, so a failure does not stop the boot. |

Leave **RISC-V spinlock type** and **NUMA** as they are. Save and exit.
HOST, same directory, to check that the three options landed:

```sh
grep -E '^CONFIG_(SMP|NR_CPUS|HOTPLUG_CPU)=' build/linux-integration_v6.18-esp32s31/.config
```

### The systimer patch

The chip's systimer is the timer Linux uses for its tick on hart 0. Its
driver offers it to "every possible CPU". With two possible CPUs, Linux only
takes such a timer as hart 0's own tick if it can move the timer's interrupt
between harts, and this chip's interrupt matrix cannot. Hart 0 would then use
the RISC-V timer, which stops counting while the hart sleeps in WFI, and the
board would hang the first time it goes idle. The patch,
[kernel/smp/0001-clocksource-esp32s31-systimer-tick-the-boot-CPU-only.patch](../../kernel/smp/0001-clocksource-esp32s31-systimer-tick-the-boot-CPU-only.patch),
offers the systimer to hart 0 only:

```diff
--- a/drivers/clocksource/timer-esp32s31-systimer.c
+++ b/drivers/clocksource/timer-esp32s31-systimer.c
@@ -173,7 +173,14 @@ static int __init systimer_init(struct device_node *np)
 	systimer.ce.set_next_event	= systimer_set_next_event;
 	systimer.ce.set_state_shutdown	= systimer_shutdown;
 	systimer.ce.set_state_oneshot	= systimer_set_oneshot;
-	systimer.ce.cpumask		= cpu_possible_mask;
+	/*
+	 * Only the boot hart takes this interrupt: the interrupt matrix routes
+	 * it to core 0 and cannot move it. With two possible CPUs a device
+	 * whose mask covers both is refused as CPU 0's tick, because its IRQ
+	 * affinity cannot be set, and CPU 0 would fall back to the CLINT timer
+	 * that stops in WFI.
+	 */
+	systimer.ce.cpumask		= cpumask_of(0);
 	systimer.ce.irq			= irq;
 
 	ret = request_irq(irq, systimer_isr, IRQF_TIMER | IRQF_IRQPOLL,
```

HOST, in the directory that holds the clone of this repository (one level
above it). This copies the patch to `/tmp`, so the next command can name it
from the Buildroot output directory:

```sh
cp esp32s31-alpine/kernel/smp/0001-clocksource-esp32s31-systimer-tick-the-boot-CPU-only.patch /tmp/
```

HOST, in the Buildroot output directory. `patch -p1` applies it to the kernel
source that Buildroot unpacked; `--dry-run` first checks that it applies
without changing anything:

```sh
patch -p1 --dry-run -d build/linux-integration_v6.18-esp32s31 < /tmp/0001-clocksource-esp32s31-systimer-tick-the-boot-CPU-only.patch
patch -p1 -d build/linux-integration_v6.18-esp32s31 < /tmp/0001-clocksource-esp32s31-systimer-tick-the-boot-CPU-only.patch
```

The dry run should print `checking file drivers/clocksource/timer-esp32s31-systimer.c`
and the real run `patching file drivers/clocksource/timer-esp32s31-systimer.c`,
each with nothing else.

The options and the patch live only in this build directory, like the
step 6 menuconfig changes. A `make linux-dirclean` (step 20 section 6 runs
one) throws both away and brings back the one-hart kernel, which is the
intended way out of this experiment.

### Build

HOST, same directory. `linux-rebuild` recompiles the kernel from the patched
source, and `all` repacks the images:

```sh
make linux-rebuild all
ls -l images/xipImage
```

Compare the size with the one from before. The flash slot for the kernel is
7 MiB, so there is room either way.

### The device tree

The kernel learns about harts from the device tree, which today lists only
`cpu@0`. HOST, same directory. If you did step 20 part 1, start from its
edited source, so the DMA pool stays where step 20 put it:

```sh
cp esp32s31-step20.dts esp32s31-smp.dts
```

If `esp32s31-step20.dts` does not exist, turn the built DTB back into source
text instead:

```sh
build/linux-integration_v6.18-esp32s31/scripts/dtc/dtc -I dtb -O dts -o esp32s31-smp.dts images/esp32s31.dtb
```

HOST, same directory. This shows the line numbers of the two places to edit:

```sh
grep -n -e 'bootargs' -e 'cpus {' -e 'cpu@0' -e 'riscv,cpu-intc' esp32s31-smp.dts
```

Edit `esp32s31-smp.dts`. First, append `maxcpus=1` to the boot arguments, so
every boot starts on hart 0 only and hart 1 is started by hand:

```diff
 	chosen {
-		bootargs = "earlycon=sbi console=ttyS0 root=mtd:rootfs rootfstype=cramfs ro swiotlb=128";
+		bootargs = "earlycon=sbi console=ttyS0 root=mtd:rootfs rootfstype=cramfs ro swiotlb=128 maxcpus=1";
 	};
```

Then add a `cpu@1` node after the end of `cpu@0`, inside `cpus`. The
`phandle` number in your file may differ; it only marks where `cpu@0` ends:

```diff
 			interrupt-controller {
 				#interrupt-cells = <0x01>;
 				interrupt-controller;
 				compatible = "riscv,cpu-intc";
 				phandle = <0x01>;
 			};
 		};
+
+		cpu@1 {
+			device_type = "cpu";
+			reg = <0x01>;
+			compatible = "riscv";
+			riscv,isa = "rv32imac_zicsr_zifencei";
+			riscv,isa-base = "rv32i";
+			riscv,isa-extensions = "i\0m\0a\0c\0zicsr\0zifencei";
+			mmu-type = "riscv,sv32";
+			clock-frequency = <0x1312d000>;
+
+			interrupt-controller {
+				#interrupt-cells = <0x01>;
+				interrupt-controller;
+				compatible = "riscv,cpu-intc";
+			};
+		};
 	};
```

- `reg = <0x01>` is the hart ID, the number Linux passes to SBI to start
  this hart.
- The ISA strings, `mmu-type` and `clock-frequency` are copied from
  `cpu@0`, because both cores are the same design.
- The `interrupt-controller` child is the hart's own local interrupt
  controller, which Linux expects under every hart. It needs no `phandle`
  because no other node refers to it.

HOST, same directory. Compile it back. As in step 20, `dtc` warns about the
lost reference names; only an `ERROR` line means the file is broken. The
last command shows the CPU nodes Linux will see:

```sh
build/linux-integration_v6.18-esp32s31/scripts/dtc/dtc -I dts -O dtb -o esp32s31-smp.dtb esp32s31-smp.dts
build/linux-integration_v6.18-esp32s31/scripts/dtc/dtc -I dtb -O dts esp32s31-smp.dtb | grep -n -e 'cpu@' -e 'maxcpus'
```

### Flash and boot

The commands below call `esptool` by name, so its venv must be active
(`. esp32s31-alpine/.venv/bin/activate` from the directory that holds the
clone, as in step 19). Close miniterm (Ctrl+]) and put the board in download
mode (hold **BOOT**, tap **RST**, release **BOOT**).

HOST, in the Buildroot output directory. One command writes both slots, the
DTB at 0x300000 and the kernel at 0x500000:

```sh
esptool --chip esp32s31 --port /dev/ttyUSB0 --baud 1152000 write-flash 0x300000 esp32s31-smp.dtb 0x500000 images/xipImage
```

Open the console (HOST, in the directory that holds the clone) and tap
**RST**:

```sh
esp32s31-alpine/.venv/bin/python -m serial.tools.miniterm --raw /dev/ttyUSB0 115200
```

BOARD, Alpine logged in as root. The first line is the build string, which
now says `SMP`; the next lines show which harts Linux knows (`possible`),
which exist (`present`) and which run (`online`), then the boot messages
about harts and timers:

```sh
uname -v
cat /sys/devices/system/cpu/possible /sys/devices/system/cpu/present /sys/devices/system/cpu/online
dmesg | grep -i -E 'smp|cpu|hsm|systimer|clocksource'
```

- Expect `0-1`, `0-1`, `0`: Linux knows hart 1 but did not start it.
- Expect `smp: Bringing up secondary CPUs ...` followed by
  `smp: Brought up 1 node, 1 CPU`, and the
  `esp32s31-systimer: 16000000 Hz clocksource+clockevent` line as before.

BOARD, same shell. The tick still works if the systimer's interrupt count
grows between the two readings, and if `sleep` returns:

```sh
grep systimer /proc/interrupts
sleep 5
grep systimer /proc/interrupts
```

BOARD, same shell. The numbers to compare with the ones from before:

```sh
dmesg | grep -i 'Memory:'
grep -E 'MemTotal|MemAvailable' /proc/meminfo
```

If the boot stops after `Linux version` or hangs at the first idle moment,
the tick is the first suspect; go back as in section 6 and paste the log.

Paste the output of this section in the thread.

## 3. Hart 1 with today's boot stack

Today's OpenSBI has no code to start hart 1, and U-Boot's device tree, which
OpenSBI reads, lists only hart 0. So Linux will ask and OpenSBI will refuse.
This shows that the kernel side works up to the SBI call.

Do this on Buildroot, not on Alpine: unplug the USB stick and tap **RST**.
After 5 s without the stick, the boot falls back to Buildroot (step 14b). Its
root is the read-only cramfs in flash, so a hang or an RST cannot damage a
file system.

BOARD, Buildroot logged in as root. The first command asks Linux to start
hart 1; the others show what happened:

```sh
echo 1 > /sys/devices/system/cpu/cpu1/online
dmesg | tail -n 5
cat /sys/devices/system/cpu/online
```

Expected from the code, not tested: `echo` prints a write error, `dmesg`
shows `CPU1: failed to start`, and `online` stays `0`. "Failed to start"
is the message for an SBI `hart_start` call that returned an error.

## 4. Espressif's SMP boot stack

This section changes the boot stack: it rebuilds U-Boot SPL, U-Boot and the
OpenSBI inside `u-boot.itb` from Espressif's 2026-10-06 merges, and flashes
two slots.

| Component | Today (most likely) | New | What the new one adds |
| --- | --- | --- | --- |
| OpenSBI | 6597eea | 5395e03 | hart 1 start and stop (`esp32s31-hsm`), per-hart CLIC set-up, a fix to the CLIC configuration register on every hart |
| U-Boot | d8d3bbe | b2ec9ae | `cpu@1` in its device tree, OpenSBI heap capped at 28 KiB |

They go together. OpenSBI reserves an 8 KiB stack per hart, so with two harts
and its default heap it would grow past 0x2F030000, and its protected region
would then cover the DMA pool and, after step 20, the radio's SRAM. U-Boot's
heap cap is what keeps it below (the
[study](../journal/2026-10-10-smp-study.md#sram-opensbi-with-two-harts)
has the numbers).

### Save the two slots

Read them from the flash rather than copying files, so the backup is
exactly what the board boots today. Download mode, then HOST, in the
Buildroot output directory. The SPL slot runs from 0x2000 to 0x100000
(0xFE000 bytes), the U-Boot slot from 0x100000 to 0x300000 (0x200000 bytes):

```sh
esptool --chip esp32s31 --port /dev/ttyUSB0 --baud 1152000 read-flash 0x2000 0xfe000 spl-slot-before-smp.bin
esptool --chip esp32s31 --port /dev/ttyUSB0 --baud 1152000 read-flash 0x100000 0x200000 uboot-slot-before-smp.bin
ls -l spl-slot-before-smp.bin uboot-slot-before-smp.bin
```

`ls` must show 1040384 and 2097152 bytes.

### Point Buildroot at the new commits

HOST, same directory:

```sh
make menuconfig
```

| Setting | Menu location | New value |
| --- | --- | --- |
| `BR2_TARGET_OPENSBI_CUSTOM_REPO_VERSION` | Bootloaders → opensbi → Custom repository version | `5395e03e1cb295257142cfdeaade6334f1543bcb` |
| `BR2_TARGET_UBOOT_CUSTOM_REPO_VERSION` | Bootloaders → U-Boot → Custom repository version | `b2ec9ae21622f92d35462f5ae3bf053466998cc6` |

Today both say a branch name (`integration/v1.6-esp32s31`,
`integration/v2024.07-esp32s31`). Buildroot names its download after this
value and does not fetch again for a name it already has, so the branch name
would keep giving the old code. A full commit hash is a new name, so it
fetches, and it pins exactly the code this guide was written against. `/` in
the menu searches by the setting's name. Save and exit.

HOST, same directory, to check both values:

```sh
grep -E '^BR2_TARGET_(OPENSBI|UBOOT)_CUSTOM_REPO_VERSION=' .config
```

### Build

HOST, same directory. `make` fetches both commits, builds OpenSBI, then
U-Boot with that OpenSBI inside, and repacks the images. The kernel is not
rebuilt, because nothing it depends on changed:

```sh
make
```

### Check the build before flashing

HOST, same directory. The hart 1 start code should now be in the image, and
U-Boot's device tree should list two harts and the heap cap:

```sh
grep -a -o -e esp32s31-reset -e esp32s31-hsm images/u-boot.itb
build/linux-integration_v6.18-esp32s31/scripts/dtc/dtc -I dtb -O dts images/u-boot.dtb | grep -n -e 'cpu@' -e 'heap-size'
```

- Expect both `esp32s31-reset` and `esp32s31-hsm`.
- Expect `cpu@0`, `cpu@1` and `heap-size = <0x7000>`.

HOST, same directory. OpenSBI's own size, as in step 19, now from the new
build directory, which Buildroot names after the version:

```sh
readelf -lW $(find build/opensbi-5395e03e1cb295257142cfdeaade6334f1543bcb -name fw_dynamic.elf)
```

The second `LOAD` line (flags `RW`) is OpenSBI's data. At run time OpenSBI
adds, after the end of that segment rounded up to 4 KiB (0x1000), two 8 KiB
stacks (0x4000) and the 28 KiB heap (0x7000). The sum must be at most
0x2F030000, which is the same as: `VirtAddr + MemSiz` of that line at most
0x2F025000. Step 19 measured 0x2F023B88 for today's build, which gives
0x2F02F000. Paste the `readelf` output in any case.

### Flash and boot

Download mode, then HOST, same directory. This writes the new SPL at 0x2000
and the new `u-boot.itb` (U-Boot and OpenSBI) at 0x100000:

```sh
esptool --chip esp32s31 --port /dev/ttyUSB0 --baud 1152000 write-flash 0x2000 images/spl_app.bin 0x100000 images/u-boot.itb
```

Plug the stick back in, open the console and tap **RST**. The boot log
should look as before: `OpenSBI: ESP32-S31 firmware active`, U-Boot, then
Alpine from the stick. The stick working means the USB host's interrupts
and its DMA pool still work after the CLIC change.

BOARD, Alpine logged in as root. Still one hart (`maxcpus=1`); this checks
that interrupts keep arriving on hart 0 with the new CLIC set-up:

```sh
cat /sys/devices/system/cpu/online
cat /proc/interrupts
sleep 5
cat /proc/interrupts
```

Expect `0`, and growing counts for the systimer and the UART.

## 5. Hart 1 with the new boot stack

Same as section 3, on Buildroot: unplug the stick and tap **RST**.

If hart 1 does start, Linux on hart 0 then waits for hart 1 to run its share
of the start-up work, which it only does after an IPI, and nothing can send
one yet. So the `echo` would never return. The `&` at its end runs it in the
background, so the shell stays usable.

BOARD, Buildroot logged in as root:

```sh
echo 1 > /sys/devices/system/cpu/cpu1/online &
sleep 3
cat /sys/devices/system/cpu/online
dmesg | tail -n 15
cat /proc/interrupts
```

| What you see | What it means |
| --- | --- |
| `CPU1: failed to start` | OpenSBI refused: check the section 4 build checks (`esp32s31-hsm`, `cpu@1`). |
| `CPU1: failed to come online`, after a second | OpenSBI released hart 1, but it never reached Linux: the ROM jump to OpenSBI, OpenSBI's set-up of hart 1, or hart 1 fetching the kernel from flash. |
| `online` shows `0-1`, the `echo` is still running, and `__sbi_send_ipi_v02: hbase = [1] failed (error [-22])` | Hart 1 runs Linux, and stops at the first IPI, because Espressif's OpenSBI has no IPI device and the kernel has no IPI driver. This is the expected result. |

The third row is the expected one, read from the code. `/proc/interrupts`
then has a `CPU1` column with zeros: hart 1 has no timer and no interrupts
routed to it. After about 20 s Linux may also print RCU stall warnings about
CPU 1, for the same reason.

Do not try `echo 0 > /sys/devices/system/cpu/cpu1/online`: taking a hart
offline also needs IPIs. Tap **RST** instead; `maxcpus=1` keeps the next boot
on hart 0.

Paste the output of sections 3 and 5 in the thread.

## 6. Going back

Each part goes back on its own. All commands: download mode first, HOST, in
the Buildroot output directory, venv active.

The boot stack, from the slots saved in section 4:

```sh
esptool --chip esp32s31 --port /dev/ttyUSB0 --baud 1152000 write-flash 0x2000 spl-slot-before-smp.bin 0x100000 uboot-slot-before-smp.bin
```

To build the old boot stack again later, set both custom repository
versions in `make menuconfig` back to the branch names from section 4.

The kernel and its DTB. The kernel is the copy from section 2. The DTB is
the one the board had before this guide. If you did step 20 part 1, that is
`esp32s31-step20.dtb`:

```sh
esptool --chip esp32s31 --port /dev/ttyUSB0 --baud 1152000 write-flash 0x300000 esp32s31-step20.dtb 0x500000 xipImage-before-smp
```

If you did not, it is `images/esp32s31.dtb`. Buildroot builds that file from
the kernel's own device tree source, which this guide does not change:

```sh
esptool --chip esp32s31 --port /dev/ttyUSB0 --baud 1152000 write-flash 0x300000 images/esp32s31.dtb 0x500000 xipImage-before-smp
```

The kernel build directory, if you want the one-hart kernel built again
without a `linux-dirclean`: reverse the patch (`-R`), turn `SMP` off in
`make linux-menuconfig` (the other two options go with it), and rebuild:

```sh
patch -R -p1 -d build/linux-integration_v6.18-esp32s31 < /tmp/0001-clocksource-esp32s31-systimer-tick-the-boot-CPU-only.patch
make linux-menuconfig
make linux-rebuild all
```

## What comes after the experiment

Real SMP needs a timer and an IPI doorbell for each hart, a CLIC driver that
writes the bank of the hart an interrupt belongs to, and, by GrieferPig's
findings, AMO-only spinlocks. The study lists each piece, where GrieferPig's
port has it, and the options: wait for Espressif's kernel half, or port
GrieferPig's pieces onto Espressif's kernel.
