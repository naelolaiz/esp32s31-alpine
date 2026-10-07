# 2026-10-06: riscv32 cross toolchain (steps 7 and 8)

Phase 3 runs on the PC, in the sibling `alpine-riscv32` repository: its
`docs/steps/` hold the guides, `patches/` the aports changes.

## Step 7: build environment and aports edits

- Alpine's `scripts/bootstrap.sh` needs abuild and apk, so it runs in an
  Alpine edge podman container, `--userns=keep-id`, with the directory holding
  aports and both repositories mounted at `/work`.
- Lessons from the container: under `keep-id`, `HOME` is the working directory
  (`/work`), so abuild's key and output land on the host; the `abuild` group
  only applies when entering by user name (`podman exec -u <name>`), not with
  the default numeric user; and `podman run -v "$PWD":/work` mounts whatever
  directory it is started from, which once put the signing key inside a git
  repository (moved out, `.gitignore` added).
- aports pinned at 71ed78a7 (master, 2026-10-06). Five patches: bootstrap.sh
  (key path, a generic bug: abuild moved its key directory to
  `~/.config/abuild`; libatomic for riscv32), gcc (`rv32imac_zicsr_zifencei`,
  `ilp32`, D, libitm and Ada off), musl, openssl (`linux32-riscv32`), binutils
  (no gold).
- Review caught `linux64-riscv32` for openssl, which would have failed many
  packages into the bootstrap; fixed with `git commit --fixup` and
  `rebase --autosquash`.

## Step 8: cross toolchain

`./scripts/bootstrap.sh riscv32 fortify-headers linux-headers musl` built:

| Runs on | Packages |
| --- | --- |
| x86_64 (the PC) | binutils-riscv32 2.45.1, gcc-pass2-riscv32, gcc-riscv32 and g++-riscv32 15.2.0, libgcc-static-riscv32, libstdc++-dev-riscv32, build-base-riscv32 |
| riscv32 (the board) | musl 1.2.6, libgcc, libstdc++, libucontext 1.5.2, linux-headers 7.2.1, fortify-headers 3.0.2 |

- `ftp.gnu.org` refused connections and Alpine's distfiles mirror had no
  binutils 2.45.1, so GNU sources come from `mirrors.kernel.org/gnu` into
  abuild's `SRCDEST`, on the host. Checksums are verified as usual.
- abuild uninstalls build dependencies after each package, so the toolchain
  and the sysroot are installed from the local repository before use.

Checks:

- Library search path: `/work/sysroot-riscv32/lib/` and `/usr/lib/`, no
  `lib32/ilp32`; `-print-multi-os-directory` prints `.`. Alpine's riscv64
  multilib patch also works for rv32.
- Hello world, static and dynamic: `ELF 32-bit LSB pie executable, UCB RISC-V,
  RVC, soft-float ABI`; both print `hello riscv32` under `qemu-riscv32`.

## What surprised us

- The dynamic loader is `/lib/ld-musl-riscv32-sf.so.1`, not
  `ld-musl-riscv32.so.1`: musl appends `-sf` for soft-float ABIs. The plan and
  the alpine-riscv32 README had the wrong name. Step 12 checks whether
  Buildroot's musl on the board uses the same name.
- libucontext needed no change: it supports riscv32 upstream.
- Binaries are PIE by default (static-pie for `-static`), as everywhere in Alpine.

## Next

Step 9: the rest of the bootstrap list (zlib, openssl, busybox, apk-tools,
openrc, alpine-baselayout, ...).
