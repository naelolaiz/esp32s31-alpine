# 2026-10-07: Alpine programs run on the board (step 12)

First Phase 4 step. The board still boots Espressif's Buildroot system; the
Alpine programs come from the exFAT partition of the USB stick. Guide:
[`docs/steps/12-alpine-binaries.md`](../steps/12-alpine-binaries.md).

## What the board has

```
~ # ls -l /lib/ld-musl* /lib/libc.so
ls: /lib/libc.so: No such file or directory
-rwxr-xr-x    1 root     root        789024 Jan  1 00:00 /lib/ld-musl-riscv32-sf.so.1
-rwxr-xr-x    1 root     root        792744 Jan  1 00:00 /lib/ld-musl-riscv32-sp.so.1
-rwxr-xr-x    1 root     root        741744 Jan  1 00:00 /lib/ld-musl-riscv64.so.1
~ # grep isa /proc/cpuinfo
isa             : rv32imac_zicsr_zifencei_zaamo_zalrsc_zca
hart isa        : rv32imac_zicsr_zifencei_zaamo_zalrsc_zca
```

- The Buildroot root has the loader name Alpine's programs ask for,
  `/lib/ld-musl-riscv32-sf.so.1` (musl adds `-sf` for the soft-float ABI).
- It also has the single-precision float loader (`-sp`, ABI `ilp32f`) and a
  riscv64 one. Probably every loader variant of Espressif's multilib
  toolchain was copied into the image (inferred, not checked). Removing the
  two unused ones would save about 1.5 MB of the 4 MiB cramfs slot before
  compression.
- The CPU's ISA string is Alpine's target `rv32imac_zicsr_zifencei`, spelled
  out: `zaamo` plus `zalrsc` make up the A extension, `zca` is the integer
  part of C.

## The three programs

Unpacked on the PC from the riscv32 repository with `tar -xzf <file>.apk`
(an `.apk` is gzip-compressed tar; tar warns about apk's
`APK-TOOLS.checksum.SHA1` headers, harmlessly), copied to the stick:

| File | Size | `file` says |
| --- | --- | --- |
| `busybox.static` (busybox-static 1.38.0-r7) | 1,091,092 | `static-pie linked` |
| `busybox` (busybox 1.38.0-r7) | 832,700 | `dynamically linked, interpreter /lib/ld-musl-riscv32-sf.so.1` |
| `ld-musl-riscv32-sf.so.1` (musl 1.2.6-r5) | 694,084 | `shared object` |

On the board, with the stick mounted at `/mnt` (`mount -t exfat /dev/sda1 /mnt`):

```
~ # /mnt/alpine-on-board/busybox.static uname -m
riscv32
~ # /mnt/alpine-on-board/ld-musl-riscv32-sf.so.1
musl libc (riscv32-sf)
Version 1.2.6
Dynamic Program Loader
Usage: /mnt/alpine-on-board/ld-musl-riscv32-sf.so.1 [options] [--] pathname [args]
~ # /mnt/alpine-on-board/ld-musl-riscv32-sf.so.1 /mnt/alpine-on-board/busybox uname -m
riscv32
~ # /mnt/alpine-on-board/busybox uname -m
riscv32
```

1. **Static.** Alpine's compiler output runs on the ESP32-S31 core: no
   illegal instruction, no FPU instruction in the soft-float build.
2. **Alpine's loader as a command.** The loader maps the program itself, so
   the board's `/lib` plays no part. Alpine's busybox needs
   `libc.musl-riscv32.so.1`. musl's loader treats every name starting with
   `libc.` as itself (`ldso/dynlink.c`, the `reserved` list), so the single
   file on exFAT, which has no symbolic links, is enough.
3. **Direct start.** The kernel follows `PT_INTERP` to the board's own
   `/lib/ld-musl-riscv32-sf.so.1`, so Alpine's busybox runs on Buildroot's C
   library. Its version was not printed.

The exFAT files are executable because the exfat driver gives every file the
mode from the mount's `fmask` (by default the umask, `022`).

## What it means

- Alpine userspace runs on the board without changes. What is left for
  step 13 is the root file system itself: Unix permissions, symbolic links and
  device nodes need ext4, not exFAT.
- The kernel gets its root device from the command line, and a USB stick
  appears only after USB enumeration, so step 13 needs `rootwait`.

## Next

Step 13: Alpine's root file system on an ext4 partition of a USB stick,
booted with `root=` pointing at that partition plus `rootwait`, Buildroot's
cramfs kept in flash as the rescue system.
