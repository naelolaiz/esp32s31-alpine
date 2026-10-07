# Plan

Snapshot of the living plan document, 2026-10-06. Change this file as the
plan changes; record why in the journal.

Twenty-four steps in six phases, each needing only the steps before it.
Storage comes before Alpine boots on the board, because an ext4 root on
microSD removes the 4 MiB flash limit. Wi-Fi comes last, because native
Wi-Fi changes the boot chain.

Ground rule: the boot stack (SPL, OpenSBI, U-Boot) stays as Espressif ships
it until Phase 5. Kernel config changes and driver patches are allowed.

Phase 3 needs no board, so it can overlap Phase 2 while board work waits on
parts. Scripts and notes are committed from step 1 onward.

```text
1 Baseline ──┬── 2 Storage (microSD, USB) ──┬── 4 Alpine on the board ── 5 Wi-Fi ── 6 Automate
             └── 3 Toolchain (QEMU) ────────┘
```

## Phase 1: Baseline on the board

1. **Repositories.** Create `alpine-riscv32` and `esp32s31-alpine` and start the journal.
2. **Espressif baseline.** Build `espressif_esp32s31_function_core_board_nor_defconfig`
   from esp-buildroot-external, flash with esptool 5.3+, boot to the Buildroot shell.
   Record free RAM, cramfs size against the 4 MiB slot, kernel size, the console
   device name and the flash layout.
3. **Probe the existing drivers.** Ethernet: `udhcpc -i eth0`, then ping the gateway.
   USB host: plug in a keyboard or hub and check that `dmesg` shows it.

Done when: the baseline boots repeatably and the numbers are in the journal.

## Phase 2: Storage in the kernel

4. **First own kernel.** Rebuild Espressif's kernel with a fragment adding
   `CONFIG_BLOCK`, `CONFIG_EXT4_FS` and `CONFIG_VFAT_FS`; confirm it still boots.
   Measures the size and RAM cost and teaches the rebuild-and-flash loop.
5. **microSD.** Wire a 3.3 V breakout to GPIO20 to 25 (see
   [hardware notes](hardware/function-coreboard-1.md)), enable `CONFIG_MMC` and
   `CONFIG_MMC_DW`, port the community dw_mmc esp32s31 patch from Linux 7.1 to 6.18,
   add the SDMMC node to the device tree. Test: a 1 MiB random file has the same
   checksum on the board and on the PC.
6. **USB pendrives.** Add `CONFIG_SCSI`, `CONFIG_BLK_DEV_SD`, `CONFIG_USB_STORAGE`
   and `CONFIG_EXFAT_FS` as a separate change (one community port reports boot
   hangs with USB storage). Same checksum test on `/dev/sda1`.

Done when: microSD and a pendrive both read and write under Buildroot.
Steps 5 and 6 are independent; step 6 runs first because it needs no wiring.
Step 5 is parked (2026-10-06): no microSD breakout yet. Phase 4 uses a USB
pendrive as the root device until it comes back.

## Phase 3: Alpine riscv32 on the host

7. **Patch aports** (in alpine-riscv32): gcc rv32 case
   (`--with-arch=rv32imac --with-abi=ilp32`, review the multilib patch), musl
   `ARCH=riscv32`, openssl `linux32-riscv32`, binutils arch lists, libatomic for
   rv32 in bootstrap.sh. Classify every other `riscv64` hit as generic or rv64-only.
8. **Cross toolchain.** `scripts/bootstrap.sh riscv32` through gcc and musl;
   static and dynamic hello world under `qemu-riscv32`.
9. **Base repository.** busybox, openssl, apk-tools, openrc, alpine-baselayout,
   alpine-keys, alpine-conf; signed with a local key; BusyBox and `apk --version`
   under qemu-user.
10. **Full-system QEMU.** Mainline 6.18 `rv32_defconfig` plus virtio under
    `qemu-system-riscv32 -M virt`; OpenRC to a login; `apk add` from the local repo.
11. **Native build chroot.** riscv32 chroot with qemu-user binfmt; abuild builds
    dropbear, curl, ca-certificates, iproute2, e2fsprogs, exfatprogs, strace,
    gdbserver, nano.

Done when: OpenRC boots in QEMU and the chroot builds packages bootstrap.sh does not.

## Phase 4: Alpine on the board

12. **Alpine binaries on Buildroot.** `busybox-static`, then dynamic BusyBox with
    `/lib/ld-musl-riscv32-sf.so.1` (soft-float loader name), from the Buildroot shell.
    Check first whether Buildroot's musl uses the same name: `ls /lib/ld-musl*`.
13. **Alpine root on a pendrive** (microSD once step 5 is done). ext4 partition from
    `apk --root --arch riscv32 --initdb`; boot `root=/dev/sda2 rootwait init=/bin/sh`. Buildroot cramfs stays in
    flash as the rescue system.
14. **OpenRC.** devfs, dmesg, mdev, hostname, bootmisc, local and a getty on the
    console found in step 2; login prompt.
15. **Network and apk.** Ethernet via OpenRC; repository served over HTTP from the
    PC; `apk update`, `apk add curl`, signature verified.
16. **Pendrive hotplug.** mdev rule or OpenRC service mounting sticks under `/media`.
17. **SSH.** Dropbear with devpts; `rc-update add dropbear default`.

Done when: the original plan's "Definition of Success" console session runs on the
board. Fallbacks if microSD fails: pendrive root (step 6) or NFS root over Ethernet;
if both fail, Alpine in the 4 MiB cramfs with apk-tools built against mbedtls.

## Phase 5: Wi-Fi

18. **Optional early Wi-Fi: USB dongle.** cfg80211, mac80211, one in-tree driver
    and firmware, Alpine's wpa_supplicant. Measure RAM before keeping it.
19. **Study the native design.** Community chain (ROM, ESP-IDF second stage,
    loader, OpenSBI, Linux) versus Espressif's (ROM, SPL, OpenSBI, U-Boot, Linux).
    Decide: port into Espressif's chain or switch. Record why in the journal.
20. **Hart 0 firmware.** Reserve hart 0 and the shared SRAM window in OpenSBI and
    the device tree; ESP-IDF Wi-Fi firmware on hart 0, Linux on hart 1.
    The `esp32s31-wifi` repository starts here.
21. **Linux driver.** Port `esp32s31-wifi` until `wlan0` associates and pings.
22. **Alpine Wi-Fi.** OpenRC service sets credentials through sysfs, runs `udhcpc -i wlan0`.

Done when: the board reaches the repository over Wi-Fi with Ethernet unplugged.

## Phase 6: Automation and upstream

23. **Reproducible builds.** `scripts/build-rootfs.sh` (SD image and rescue cramfs),
    `scripts/build-esp32s31-image.sh` reusing esp-linux-bsp packaging; QEMU CI,
    then hardware CI over UART.
24. **Upstream.** Submit generic riscv32 aports patches separately, coordinating in
    Alpine's #alpine-ports channel.

## Risks

| Risk | Fallback |
| --- | --- |
| Ethernet does not work on the preview kernel | overlayfs on tmpfs, packages baked into the image |
| microSD wiring or dw_mmc port fails | pendrive root, then NFS root |
| RAM (about 12 MB free) too tight for OpenRC, Dropbear, apk | fewer services, BusyBox init as a stopgap |
| Espressif BSP rebases (developer preview) | pin commits in `bsp/versions.md` |
| Native Wi-Fi takes hart 0 from Linux; community code is on 7.1 | USB dongle stays as the Wi-Fi path |

## Open questions

- [x] Does Ethernet link up on the preview kernel? Yes: YT8531 PHY, DHCP and internet work, link at 100 Mbps (step 3).
- [ ] Pin the Ethernet MAC: it is random on every boot (step 3).
- [ ] Why 100 Mbps on a gigabit PHY? Check on a known gigabit port.
- [x] Does the core implement the F extension? No: the hart reports `rv32imac_zicsr_zifencei_zaamo_zalrsc_zca` (step 2).
- [ ] Can the flash layout give the rootfs more than 4 MiB? Kernel slot is 7 MiB (0x500000 to 0xC00000) with the kernel at 3.7 MiB, so moving `SLOT_ROOTFS` down is possible (layout in ground-truth.md).
- [x] Does libucontext support riscv32? Upstream has `arch/riscv32`; aports passes `ARCH=$CARCH`, so no aports change expected (check the 1.5.2 tarball in step 8).
- [ ] Buildroot forces `CONFIG_BLK_DEV_INITRD=y` (kconfig fixup in `linux/linux.mk`, confirmed: the defconfig sets `BR2_TARGET_ROOTFS_CPIO=y` with gzip), which pulls in all initramfs decompressors. Drop the cpio image to save kernel flash, once nothing on the board needs it.
- [ ] The block layer costs about 1 MiB of free RAM after boot but only 48 KiB statically; find where it goes (step 4).
