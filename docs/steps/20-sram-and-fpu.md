# Step 20: SRAM and FPU for the radio

Goal: free the internal SRAM the Wi-Fi radio needs, with Ethernet and the USB
stick still working, then let Linux use the F extension.

- Part 1 (sections 1 to 5) moves the kernel's DMA pool with a trial DTB.
  Done on 2026-10-10 ([journal](../journal/2026-10-10-dma-pool.md)).
- Part 2 (sections 6 and 7) adds F-only FPU support to the kernel and a test
  program for it.
- Sections 8 to 10 rebuild the kernel once with all of it, flash it and
  check it on the board.

Step 19's results are in the
[journal](../journal/2026-10-10-native-wifi-board-checks.md). The one thing
in the radio's way is the kernel's DMA pool: 256 KiB of SRAM at 0x2F030000,
where the radio needs its heap, buffers and exception stack.

## 1. What the DMA pool is for

The Ethernet controller (EMAC) and the USB host controller read and write
memory on their own, by DMA, while the CPU does other work. Some of that
memory is shared with the CPU all the time, mainly the descriptor rings: the
lists of buffers the controller fills or sends. Linux calls this memory
"coherent" and allocates it with `dma_alloc_coherent()`.

On this chip the data cache sits in front of external memory only (flash and
PSRAM), and the DMA controllers do not see what is still in the cache. Linux
on PSRAM would therefore need a cache flush before every look at a
descriptor. Espressif's kernel avoids that by taking coherent memory from a
pool in internal SRAM, which the cache does not cover (from Espressif's cache
driver, `drivers/cache/esp32s31_cache.c`, and its OpenSBI, which only accepts
cache operations on flash and PSRAM addresses). The device tree describes the
pool as:

```
reserved-memory {
	dma_pool: dma-pool@2f030000 {
		compatible = "shared-dma-pool";
		linux,dma-default;
		no-map;
		reg = <0x2f030000 0x40000>;
	};
};
```

- `shared-dma-pool` and `linux,dma-default` make it the pool every
  `dma_alloc_coherent()` uses, for every device marked `dma-noncoherent` (the
  EMAC and the USB host).
- `no-map` keeps Linux from using it as ordinary RAM. It is outside the
  16 MiB of PSRAM anyway, so moving it changes no RAM figure.
- The packet and disk data themselves go through PSRAM buffers with cache
  maintenance, or through the `swiotlb` bounce buffer in PSRAM. None of that
  uses the pool.

So the pool only needs room for the descriptors. From reading the drivers
(an estimate, which section 3 checks on the board):

| User | Allocation | Pool pages (4 KiB each) |
| --- | --- | --- |
| EMAC receive ring | 64 descriptors of 32 bytes, at `ip link set eth0 up` | 1 |
| EMAC transmit ring | 128 descriptors of 32 bytes, at `ip link set eth0 up` | 1 |
| USB host (dwc2) | 64-byte status buffer, at boot | 1 |

The pool hands out whole pages, rounded up to a power of two, so this is
about 12 KiB of the 256 KiB.

## 2. Where the pool goes

Internal SRAM, as the radio port wants it (step 19 journal, from
GrieferPig's memory map):

| Range | Use with the radio |
| --- | --- |
| 0x2F000000-0x2F030000 | OpenSBI |
| 0x2F030000-0x2F072380 | radio main area and its exception stack |
| 0x2F073000-0x2F078000 | **the DMA pool, 20 KiB** |
| 0x2F078C00-0x2F07CFB0 | radio high heap |
| 0x2F07CFB0-0x2F080000 | ROM data |

- 0x2F072380 to 0x2F078C00 is the gap where GrieferPig keeps the DMA
  descriptors of his own drivers. Our kernel keeps descriptors in the pool,
  so the pool goes there.
- The pool starts and ends on page boundaries (0x2F073000, 0x2F078000),
  because it is handed out in pages. 20 KiB is 5 pages, about twice the
  estimate above.
- U-Boot and its SPL run in this area while booting, but they are gone when
  Linux starts. U-Boot copies the device tree to PSRAM (step 19 journal), so
  nothing of the boot stack stays here.

## 3. Try the new pool with a changed DTB

The quickest test changes only the device tree that is already built, and
flashes only its slot (0x300000), so the kernel and the root on the stick
stay as they are. Section 8 makes the change permanent.

### Make the DTB

HOST, in the Buildroot output directory of your Espressif build (the one
with `images/` and `build/`). The kernel build compiled its own copy of the
device tree compiler, `dtc`; this checks that the path below matches one
file:

```sh
ls build/linux-*/scripts/dtc/dtc
```

HOST, same directory. This turns the flashed DTB back into source text
(`-I dtb -O dts`), so that one node can be edited:

```sh
build/linux-*/scripts/dtc/dtc -I dtb -O dts -o esp32s31-step20.dts images/esp32s31.dtb
grep -n -A5 'dma-pool@' esp32s31-step20.dts
```

Edit `esp32s31-step20.dts` at the lines `grep` printed, replacing the two
lines rather than adding new ones: `dtc` refuses a node that has the same
property twice (`ERROR (duplicate_property_names)`). The node name carries
the start address by convention, and `reg` is start and size:

```diff
-		dma-pool@2f030000 {
+		dma-pool@2f073000 {
 			compatible = "shared-dma-pool";
 			linux,dma-default;
 			no-map;
-			reg = <0x2f030000 0x40000>;
+			reg = <0x2f073000 0x5000>;
 		};
```

HOST, same directory. This compiles it back (`-I dts -O dtb`). The
decompiled text has lost the names of references, so `dtc` prints warnings;
only an `ERROR` line means the file is broken:

```sh
build/linux-*/scripts/dtc/dtc -I dts -O dtb -o esp32s31-step20.dtb esp32s31-step20.dts
ls -l images/esp32s31.dtb esp32s31-step20.dtb
cmp -l images/esp32s31.dtb esp32s31-step20.dtb | wc -l
```

- Both files should have the same size: the new name and numbers are as long
  as the old ones.
- `cmp -l` prints one line per differing byte. Expect 6: two characters of
  the name and two bytes each of the start and the size (expected if the
  round trip through text changes nothing else; paste the output if the
  count is different).

### Flash only the DTB slot

The commands below call `esptool` by name, so its venv must be active in this
terminal (`. esp32s31-alpine/.venv/bin/activate` from the directory that
holds the clone of this repository, as in step 19). Close miniterm first
(Ctrl+]), because esptool needs the serial port. Put the board in download
mode (hold **BOOT**, tap **RST**, release **BOOT**).

HOST, in the Buildroot output directory. This writes the 6 KiB file at the
DTB slot only:

```sh
esptool --chip esp32s31 --port /dev/ttyUSB0 --baud 1152000 write-flash 0x300000 esp32s31-step20.dtb
```

esptool resets the board when it is done, and it starts booting. Open the
console as in step 19 and tap **RST** to see the boot from the start. HOST,
in the directory that holds the clone of this repository:

```sh
esp32s31-alpine/.venv/bin/python -m serial.tools.miniterm --raw /dev/ttyUSB0 115200
```

- U-Boot should print the same `ERROR: reserving fdt memory region failed`
  line, now with `addr=2f073000 size=5000` (expected, not tested).
- Alpine's login prompt means the USB host still works, because the root is
  on the stick. If the Buildroot login comes up instead, the stick was not
  found (the step 14b fallback); see section 5.

## 4. Check Ethernet and USB on the board

BOARD, Alpine logged in as root. This shows the new pool and confirms the
root is the stick:

```sh
dmesg | grep -i 'reserved mem'
ls /proc/device-tree/reserved-memory/
mount | grep ' / '
```

- Expect `0x2f073000..0x2f077fff (20 KiB) nomap non-reusable
  dma-pool@2f073000` and the directory `dma-pool@2f073000`.
- Expect `/dev/sda1 on / type ext4`.

BOARD, same shell. `eth0` has no `auto` line since the cable is unplugged,
so its rings are not allocated yet. Bringing it up allocates them from the
pool, with or without a cable; bringing it down again frees them:

```sh
ip link set eth0 up
ip link show eth0
dmesg | tail -n 8
ip link set eth0 down
```

- Expect `<NO-CARRIER,BROADCAST,MULTICAST,UP>`: the interface is up, only
  the cable is missing.
- A pool that is too small shows as an `Out of memory` error from the first
  command and `DMA descriptors allocation failed` in `dmesg`.

BOARD, same shell. This reads 64 MiB from the stick to keep the USB host
busy for a while, then shows any USB errors:

```sh
dd if=/dev/sda of=/dev/null bs=64k count=1024
dmesg | tail -n 5
```

`dd` prints the speed, to compare with later steps. Paste the output of this
section in the thread.

## 5. Going back

If Alpine did not come up, or `eth0` failed, put the original DTB back.
Download mode, then HOST, in the Buildroot output directory:

```sh
esptool --chip esp32s31 --port /dev/ttyUSB0 --baud 1152000 write-flash 0x300000 images/esp32s31.dtb
```

Before that, if the Buildroot login came up, log in there and run
`dmesg | grep -i -E 'dwc2|dma|usb' | head -30`: it shows why the USB host
did not start, and with it how much more pool it needs.

## 6. Why Linux ignores the FPU today

The cores have the F extension (single-precision floating point) but not D
(double precision). The evidence is indirect: Espressif's Wi-Fi libraries
for this chip are built for `rv32imafc` with the single-float ABI, and
GrieferPig's kernel runs them with F on (step 19 study). Three things keep
Linux from using it:

- The device tree lists no `f` (`riscv,isa = "rv32imac_zicsr_zifencei"`), so
  the kernel does not know about it. The boot line `riscv: base ISA
  extensions acim` shows that.
- Even with `f` listed, stock Linux drops F without D
  (`arch/riscv/kernel/cpufeature.c`, "This kernel does not support systems
  with F but not D").
- The kernel saves and restores a process's float registers on every
  context switch with `fsd`/`fld`, the double-precision store and load, which
  are illegal instructions without D (`arch/riscv/kernel/fpu.S`).

With F off, the floating point unit is disabled for every process
(`sstatus.FS` stays "Off"), and any float instruction kills the process with
`SIGILL`. Alpine's user space is soft-float and never uses one, so nothing
changes for it. The radio code in step 21 does use F, which is why it is
needed.

The two patches:

- [kernel/patches/0002-riscv-support-harts-with-F-but-without-D.patch](../../kernel/patches/0002-riscv-support-harts-with-F-but-without-D.patch)
  adds `CONFIG_FPU_F_ONLY` (**Platform type → FPU support for harts with F
  but without D**, under **FPU support**). It is on by default for
  Espressif SoCs (`ARCH_ESPRESSIF`), so no configuration change is needed.
  With it, the kernel accepts F without D, and the context switch uses
  `fsw`/`flw` (single precision) into the low half of the same 64-bit slots,
  so the signal frame and the ptrace layout stay as they are. It ignores D,
  because the upper halves would be lost.
- [kernel/patches/0003-riscv-dts-esp32s31-describe-the-F-extension.patch](../../kernel/patches/0003-riscv-dts-esp32s31-describe-the-F-extension.patch)
  adds `f` to the CPU node of the device tree.

The kernel code alone changes nothing: without `f` in the device tree, F
stays off. That makes going back easy: flashing a DTB without `f` turns it
off again with the same kernel.

## 7. A test program for F

[tools/fpu-test.c](../../tools/fpu-test.c) runs a float loop in two
processes at once and checks that each one gets the same bits in all five
rounds. A process killed by `SIGILL` means F is not available; different
bits between rounds would mean the kernel mixed up the float registers of the
two processes when it switched between them.

CONTAINER `alpine-rv32` (`podman exec -it -u $(id -un) alpine-rv32 sh`), in
`/work`. `-march=rv32imafc` lets the compiler use F instructions, while
`-mabi=ilp32` keeps the soft-float calling convention of Alpine's libraries,
so it links against them as usual. `-static` makes the binary independent of
what is installed on the stick:

```sh
cd /work
riscv32-alpine-linux-musl-gcc -static -O2 -march=rv32imafc -mabi=ilp32 -o fpu-test esp32s31-alpine/tools/fpu-test.c
riscv32-alpine-linux-musl-objdump -d fpu-test | grep -c -E 'f(add|sub|mul|madd)\.s'
qemu-riscv32 ./fpu-test
```

- The `grep` count must be more than 0: the binary really contains F
  instructions.
- `qemu-riscv32` emulates a CPU with F, so the test must print `OK` here
  before it means anything on the board.

The board has no network now, so the program goes on the stick. Power the
board off at its prompt with `poweroff` and wait for `reboot: Power down`
(it stays halted, see the open item on `reboot`), then move the stick to the
PC. HOST, in the porting directory (the one `/work` is in the container):

```sh
sudo mkdir -p /mnt/alpine-root
sudo mount /dev/disk/by-label/alpine-root /mnt/alpine-root
sudo cp fpu-test /mnt/alpine-root/root/
sudo umount /mnt/alpine-root
```

Keep the stick out of the board until section 9: the kernel flash comes
first.

## 8. Rebuild the kernel with all patches

Buildroot applies kernel patches only when it unpacks the kernel source, so
this rebuilds the whole kernel from fresh source, with patches 0001 to 0003.

### Keep the kernel options from steps 4 and 6

Unpacking the kernel again also throws away its configuration in the build
directory, and Buildroot makes a new one from Espressif's defconfig plus the
fragments named in `BR2_LINUX_KERNEL_CONFIG_FRAGMENT_FILES`. Step 6 set the
USB storage options in `linux-menuconfig`, which only changed the build
directory. If the fragments are not named, the new kernel has no USB storage,
cannot mount the stick, and the board falls back to Buildroot.

HOST, in the Buildroot output directory. The first command keeps a copy of
today's kernel configuration to compare against after the rebuild, the
second today's kernel, which is the way back if the new one does not boot
(the rebuild overwrites `images/xipImage`); the third shows which fragments
Buildroot applies:

```sh
cp build/linux-*/.config kernel-config-before-step20
cp images/xipImage xipImage-before-step20
grep BR2_LINUX_KERNEL_CONFIG_FRAGMENT_FILES .config
```

The line must name both `kernel/fragments/10-block.config` and
`kernel/fragments/20-usb-storage.config` of your clone, with absolute paths.
If it is empty or misses one, set it in the next step.

### Name the patches and the fragments

Your clone of this repository must be on the branch that has the patches
(`claude/native-wifi-ensw69` until it is merged), because Buildroot reads
them from the clone. HOST, same directory. Open Buildroot's configuration:

```sh
make menuconfig
```

- **Kernel → Custom kernel patches** (`BR2_LINUX_KERNEL_PATCH`): the absolute
  path of `kernel/patches` in your clone. Buildroot applies every `*.patch`
  in that directory, in name order.
- **Kernel → Additional configuration fragment files**
  (`BR2_LINUX_KERNEL_CONFIG_FRAGMENT_FILES`), only if the `grep` above
  missed one: the absolute paths of `kernel/fragments/10-block.config` and
  `kernel/fragments/20-usb-storage.config`, separated by a space.

Save and exit.

### Rebuild and compare

HOST, same directory. `linux-dirclean` deletes the unpacked kernel, so the
next `make` unpacks it again from the download cache, applies the patches
and the fragments, and builds the kernel, the DTB and the flash image:

```sh
make linux-dirclean
make
diff kernel-config-before-step20 build/linux-*/.config
build/linux-*/scripts/dtc/dtc -I dtb -O dts images/esp32s31.dtb | grep -E 'dma-pool@|reg = <0x2f07|riscv,isa'
```

- `diff` compares the kernel configuration before and after. Expect only
  `CONFIG_FPU_F_ONLY=y`, the new option from patch 0002. Any other `CONFIG_`
  line is an option that was set by hand and is not in the fragments; paste
  it before flashing.
- The last command should show `dma-pool@2f073000`,
  `reg = <0x2f073000 0x5000>` and `riscv,isa = "rv32imafc_zicsr_zifencei"`
  from the DTB Buildroot just built.

## 9. Flash and check

Both the kernel and the DTB changed. Download mode (hold **BOOT**, tap
**RST**, release **BOOT**), esptool venv active, miniterm closed. HOST, in
the Buildroot output directory. This writes the two slots in one go:

```sh
esptool --chip esp32s31 --port /dev/ttyUSB0 --baud 1152000 write-flash 0x300000 images/esp32s31.dtb 0x500000 images/xipImage
```

Plug the stick back in, open the console as in section 3 and tap **RST**.

- If F is missing after all, the kernel itself should fault with an illegal
  instruction early, around the start of `/sbin/init`, because it restores
  float registers that do not exist (expected, not tested). Then go back as
  in section 10.
- Otherwise Alpine boots as before.

BOARD, Alpine logged in as root. The first two show what the kernel made of
the ISA, the third runs the test:

```sh
dmesg | grep -i -E 'isa|F but not D'
grep isa /proc/cpuinfo
/root/fpu-test
```

- Expect `riscv: base ISA extensions acfim` and no `F but not D` line.
- `/proc/cpuinfo` should list `rv32imafc_zicsr_zifencei`.
- `fpu-test` should print two `process N: ... in all 5 rounds` lines with the
  same values as in QEMU, and `OK`.

Paste the output.

## 10. Going back

The DTB without `f` turns F off with the new kernel. Download mode, then
HOST, in the Buildroot output directory. `esp32s31-step20.dtb` is part 1's
trial DTB, with the small pool and no `f`:

```sh
esptool --chip esp32s31 --port /dev/ttyUSB0 --baud 1152000 write-flash 0x300000 esp32s31-step20.dtb
```

If the new kernel does not boot even with that DTB, put the old kernel back
too. HOST, same directory:

```sh
esptool --chip esp32s31 --port /dev/ttyUSB0 --baud 1152000 write-flash 0x300000 esp32s31-step20.dtb 0x500000 xipImage-before-step20
```

That is exactly the state at the end of part 1.
