# Step 20: SRAM and FPU for the radio

Goal: free the internal SRAM the Wi-Fi radio needs, with Ethernet and the USB
stick still working, then let Linux use the F extension. This page covers
part 1, the SRAM. Part 2, the F-only FPU patch, follows once part 1 works.

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
stay as they are. Section 4 makes the change permanent once it works.

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

Edit `esp32s31-step20.dts` at the lines `grep` printed. The node name
carries the start address by convention, and `reg` is start and size:

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

HOST, in the Buildroot output directory. This writes the 18 KiB file at the
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

## 6. Make it permanent

The same change as a kernel patch is
[kernel/patches/0001-riscv-dts-esp32s31-shrink-the-DMA-pool.patch](../../kernel/patches/0001-riscv-dts-esp32s31-shrink-the-DMA-pool.patch).
Do this after section 4 passed: Buildroot applies kernel patches only when
it unpacks the kernel source, so it rebuilds the whole kernel.

HOST, in the Buildroot output directory. Open Buildroot's configuration:

```sh
make menuconfig
```

In **Kernel → Custom kernel patches** (`BR2_LINUX_KERNEL_PATCH`), enter the
absolute path of `kernel/patches` in your clone of this repository. Buildroot
applies every `*.patch` in that directory, in name order. Save and exit.

HOST, same directory. `linux-dirclean` deletes the unpacked kernel, so the
next `make` unpacks it again from the download cache, applies the patch and
builds the kernel, the DTB and the flash image:

```sh
make linux-dirclean
make
build/linux-*/scripts/dtc/dtc -I dtb -O dts images/esp32s31.dtb | grep -A5 'dma-pool@'
```

The last command should show `dma-pool@2f073000` with
`reg = <0x2f073000 0x5000>`, from the DTB Buildroot just built. Flash it at
0x300000 as in section 3, with `images/esp32s31.dtb` as the file. The kernel
code did not change, so the 0x500000 slot can stay as it is.
