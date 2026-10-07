# 2026-10-07: Alpine base system for riscv32 (step 9)

Phase 3, in the sibling `alpine-riscv32` repository: the guide is its
`docs/steps/09-base-system.md`, the busybox fix its `patches/0006`.

## What ran

`./scripts/bootstrap.sh riscv32` with no package list cross-builds Alpine's
whole bootstrap list in the container. Each package is built for riscv32 with
the cross toolchain from step 8, signed with the local key and added to the
local riscv32 repository. Packages from step 8 are skipped as up to date.

Result: 156 files in the riscv32 repository, among them:

| Package | Version |
| --- | --- |
| musl | 1.2.6-r5 |
| busybox | 1.38.0-r7 (with our hwclock patch) |
| apk-tools | 3.0.8-r0 |
| openrc | 0.63.2-r1 |
| alpine-baselayout | 3.7.2-r1 |
| alpine-base | 3.25.0_alpha20260805-r0 |
| openssl | 3.5.9 |
| gcc, binutils (native) | 15.2.0, 2.45.1 |

Plus zlib, pkgconf, gmp, mpfr, mpc, isl, zstd, make, file, patch, build-base,
ca-certificates, libmd, libbsd, libcap, alpine-conf, alpine-keys, attr, acl,
fakeroot, tar, pax-utils, lzip and abuild. Only busybox needed a code change.

## Check

As container root, alpine-base was installed into an empty tree with
`apk add --root ... --initdb --arch riscv32 --no-scripts alpine-base`: 25
packages, 7696 KiB. Then, under `qemu-riscv32 -L <tree>`:

- `busybox uname -m` prints `riscv32`.
- `apk --version` prints `apk-tools 3.0.8-r0, compiled for riscv32.`

7.5 MiB is what a minimal Alpine root needs on disk. It would not fit the
board's 4 MiB cramfs slot, which is one reason Phase 4 puts the root on a USB
pendrive.

## Failures and fixes

1. **busybox: `'SYS_settimeofday' undeclared` in `util-linux/hwclock.c:143`.**
   - riscv32 is time64-only: its kernel never had the old settimeofday
     syscall, so musl's riscv32 syscall list has no `SYS_settimeofday`.
     riscv64 has it as 170.
   - busybox calls the raw syscall to set the kernel timezone, because musl's
     `settimeofday()` ignores the timezone argument.
   - Fix: patch `0043-hwclock-no-settimeofday-syscall-on-riscv32.patch` in the
     busybox package makes that step a no-op when the syscall does not exist,
     so `hwclock -s` still sets the clock. This is aports patch 0006.
   - Buildroot carries a fix for the same error that returns failure instead;
     that would stop `hwclock -s` before it sets the time.
   - The error was not in the last lines of the log. make runs several jobs
     at once and the others keep printing, so it was found with
     `grep -n ' error: '`.
2. **The first patch file was empty.** Its sha512 began `cf83e135`, the
   SHA-512 of empty input: `diff` had found no difference because the edit
   was not saved. abuild checksummed and applied the empty file without
   complaint, and the build failed exactly as before. The guide now checks
   the edit with `grep -c` and names that hash.
3. **attr hung at `Connecting to mirror.accum.se`.**
   `download.savannah.nongnu.org` only redirects to a mirror, and the one it
   picked never answered; busybox wget has no timeout. Fix: fetch attr and acl
   from `download-mirror.savannah.gnu.org` into abuild's `SRCDEST`.
4. **fakeroot's tarball failed its sha512.** The source is a GitLab "archive"
   link on salsa.debian.org, generated on request, and its bytes had changed.
   Alpine's copy on `distfiles.alpinelinux.org` matched. The renamed file's
   suffix (`.bba207ae`) is the expected sum's start, not the file's.
5. **alpine-base would not install: `mdev-conf (no such package)`.**
   alpine-base depends on busybox-mdev-openrc, which depends on mdev-conf, and
   bootstrap.sh's list lacks it. Built with
   `./scripts/bootstrap.sh riscv32 mdev-conf`. This is a gap in Alpine's
   bootstrap, not a riscv32 problem.

## What we learned

- A cross bootstrap resumes cheaply: abuild skips every package whose apk is
  newer than its APKBUILD, so after a fix the rerun starts at the failed
  package.
- Checksums caught both download problems (the empty patch was the exception,
  because we computed its sum ourselves).
- Generated archive tarballs (GitLab `/-/archive/`) are fragile sources; the
  distribution's own distfiles copy is the stable one.

## Next

Step 10: a mainline 6.18 kernel (`rv32_defconfig`) and an ext4 image of
alpine-base, booted with `qemu-system-riscv32 -M virt` to an OpenRC login.
