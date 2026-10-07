# Step 12: Alpine binaries on the board, under Buildroot

Goal: run programs from the Alpine riscv32 repository (sibling repository
`alpine-riscv32`, steps 7 to 11) on the ESP32-S31, while the board still
boots Espressif's Buildroot system. The board's root file system is a
read-only cramfs, so the programs come from the USB stick (step 6).

Three programs, each testing one more piece:

| Program | Package | What it tests |
| --- | --- | --- |
| `busybox.static` | busybox-static | the instruction set: Alpine's compiler output runs on this CPU |
| `ld-musl-riscv32-sf.so.1` | musl | Alpine's C library and dynamic loader |
| `busybox` (dynamic) | busybox | a normal Alpine program, loaded by Alpine's loader |

## 1. What the board has

BOARD, the Buildroot shell on the serial console:

```sh
ls -l /lib/ld-musl* /lib/libc.so
grep isa /proc/cpuinfo
busybox | head -1
```

- `ls` shows the name of Buildroot's dynamic loader. Every dynamic program
  carries the loader's path in its ELF header (`PT_INTERP`), and the kernel
  starts that file first. Alpine's programs ask for
  `/lib/ld-musl-riscv32-sf.so.1`; musl adds `-sf` for the soft-float ABI
  (`arch/riscv32/reloc.h` in musl). If Buildroot's musl has the same name,
  Alpine programs start directly, with Buildroot's C library; if not, they
  fail with `not found` even though the file exists, because the missing
  file is the loader, not the program.
- The `isa` lines (one per core) are the instruction set the kernel reports.
  Alpine's compiler targets `rv32imac_zicsr_zifencei` (step 7).
- The first line of `busybox` is Buildroot's BusyBox version, for comparison.

## 2. The files, from the PC

HOST, in the directory holding `aports`. An `.apk` package is gzip-compressed
tar (signature, metadata, then the files), so GNU tar unpacks it. tar warns
`Ignoring unknown extended header keyword 'APK-TOOLS.checksum.SHA1'`: that
is the per-file checksum apk adds, and it is harmless here.

```sh
mkdir -p apk-x alpine-on-board
tar -xzf .local/share/abuild/main/riscv32/busybox-static-1.38.0-r7.apk -C apk-x
tar -xzf .local/share/abuild/main/riscv32/busybox-1.38.0-r7.apk -C apk-x
tar -xzf .local/share/abuild/main/riscv32/musl-1.2.6-r5.apk -C apk-x
cp apk-x/bin/busybox.static apk-x/bin/busybox apk-x/lib/ld-musl-riscv32-sf.so.1 alpine-on-board/
file alpine-on-board/*
```

`file` shows all three as `ELF 32-bit LSB ... UCB RISC-V, RVC, soft-float
ABI`; `busybox` also names its interpreter, `/lib/ld-musl-riscv32-sf.so.1`.
In Alpine's musl package the loader is the real file and
`libc.musl-riscv32.so.1` only a link to it, so one file is the whole C
library.

Plug the stick into the PC. Ventoy formats its data partition exFAT with the
label `Ventoy`; `findmnt` prints where your desktop mounted it:

```sh
lsblk -o NAME,SIZE,FSTYPE,LABEL,MOUNTPOINTS
findmnt -no TARGET LABEL=Ventoy
cp -r alpine-on-board "$(findmnt -no TARGET LABEL=Ventoy)"/
sync
```

If `findmnt` prints nothing, the partition is not mounted or has another
label: `lsblk` shows both. `sync` writes everything out before you unplug
the stick.

## 3. Run them on the board

Boot the board, then plug the stick in, as in step 6. BOARD:

```sh
mount -t exfat /dev/sda1 /mnt
ls -l /mnt/alpine-on-board
/mnt/alpine-on-board/busybox.static uname -m
/mnt/alpine-on-board/busybox.static | head -1
```

- exFAT has no Unix permissions. The kernel gives every file the mode from
  the mount's `fmask` (by default the mounting process's umask, `022`), so
  `ls` shows them as executable.
- `busybox.static` needs no loader and no library: it prints `riscv32`, then
  `BusyBox v1.38.0 ...`, Alpine's build.

Next, Alpine's loader. Run without arguments, it prints its own version.
With a program as argument, the loader maps that program itself instead of
the kernel doing it, so the board's `/lib` plays no part:

```sh
/mnt/alpine-on-board/ld-musl-riscv32-sf.so.1
/mnt/alpine-on-board/ld-musl-riscv32-sf.so.1 /mnt/alpine-on-board/busybox uname -m
```

- The first line prints `musl libc (riscv32-sf)`, `Version 1.2.6` and a usage
  line, and exits with status 1; that is normal.
- The second prints `riscv32`. Alpine's busybox lists
  `libc.musl-riscv32.so.1` as needed. musl's loader treats every name
  starting with `libc.` as itself (`ldso/dynlink.c`, the `reserved` list), so
  no second file is loaded.

Last, the direct start, where the kernel follows `PT_INTERP` into the
board's `/lib`:

```sh
/mnt/alpine-on-board/busybox uname -m
```

- With a Buildroot loader of the same name (section 1), it prints `riscv32`,
  running Alpine's program on Buildroot's C library. If Buildroot's musl is
  older than Alpine's 1.2.6, an `Error relocating ... symbol not found` here
  means the program uses a function that version lacks.
- Otherwise it prints `not found`, which is the explanation from section 1.

Unplug only after `umount /mnt`.

Done when: `busybox.static` and the dynamic `busybox` through Alpine's loader
print `riscv32` on the board, and the loader name of Buildroot's musl is
recorded.
Next: step 13, the Alpine root file system on an ext4 partition of a USB
stick.
