# 2026-10-07: native riscv32 builds, dropbear over SSH (step 11)

Phase 3, in the sibling `alpine-riscv32` repository: the guide is its
`docs/steps/11-native-build.md`.

## Why native

`scripts/bootstrap.sh` cross-builds only APKBUILDs that split their build
dependencies into `makedepends_build` (x86_64 tools) and `makedepends_host`
(riscv32 libraries). Most of aports has a plain `makedepends`. abuild's
`calcdeps()` installs that on the x86_64 side only, so the riscv32 sysroot
lacks e.g. nano's `ncurses-dev`. Alpine builds everything else natively on
each architecture; here the "machine" is a riscv32 container that the PC's
kernel runs through binfmt_misc and `qemu-riscv32`.

## Scope

Dependency counts from the APKBUILDs (`makedepends`, `depends`,
`depends_dev`, read with `BOOTSTRAP=1`), counting source packages not built
yet; approximate:

| Package | Not yet built |
| --- | --- |
| nano | 2 |
| dropbear | 12 more |
| exfatprogs, strace, gdb, iproute2, curl, e2fsprogs | 24 to 118 each, about 145 together, 40 of them Python modules |

The big six pull in python3, cmake with its sphinx manual, elfutils,
util-linux and glib, mostly for documentation and optional features.
Decision: step 11 builds nano and dropbear; the six wait for patches that
drop those dependencies when bootstrapping. vim, htop, neofetch, mc and
fastfetch were requested and then parked until after Wi-Fi; their analysis
and two prepared APKBUILD edits are at the end of the guide.

## The build container

1. **binfmt rule with `C`.** abuild installs build dependencies through
   `abuild-apk`, a setuid-root helper. For a program run through binfmt_misc,
   the kernel normally takes the credentials from the interpreter (qemu,
   not setuid), so the helper would fail with `setuid(0) failed`. The rule
   was registered again with flags `OCFP`: `C` takes the credentials from the
   riscv32 program, `O` (implied) hands qemu an open file descriptor.
2. **The tree.** As root in the x86_64 container:
   `apk add --root /var/tmp/rv32-native --initdb --arch riscv32` with
   alpine-baselayout, busybox, busybox-binsh, apk-tools, abuild and
   build-base from the local repository; the local key copied in first;
   `/etc/apk/repositories` pointing at the shared repository.
3. **The container.** `tar -cf - .` inside the x86_64 container, piped into
   `podman import -` on the host, then `podman run` with `--userns=keep-id`
   and the porting directory at `/work:z` (lowercase: two containers share it
   now), and `addgroup <user> abuild`.
4. **Check.** `uname -m` printed `riscv32`, `id` listed `300(abuild)`, and
   `abuild-apk --version` printed `apk-tools 3.0.8-r0, compiled for riscv32.`
   The last one goes through the setuid helper, so the `C` flag works.

Downloads run in the x86_64 container (`abuild fetch verify`) into the shared
`.cache/distfiles`, where the riscv32 abuild only checks the sums. The GNU
tarballs (m4, autoconf, automake, texinfo) came from `mirrors.kernel.org/gnu`
as in steps 8 and 9.

## Built natively

All with `abuild -r` in the riscv32 container:

| Package | Version |
| --- | --- |
| ncurses | 6.6_p20260822-r0 |
| nano | 9.2-r0 |
| skalibs, execline, s6, utmps | utmps 0.1.3.4-r0 |
| bzip2, tzdata, perl | perl 5.44.0-r0 |
| texinfo, m4, autoconf, automake | |
| dropbear | 2026.94-r0 |

- No APKBUILD needed a change.
- dropbear's `prepare()` runs `autoreconf -fvi`, which is why autoconf,
  automake, m4 and perl are in the chain.
- `ABUILD_BOOTSTRAP=1` for the dropbear chain skips `check()` and
  `checkdepends`: m4's checkdepends is diffutils, which is not built, and
  perl's test suite would run entirely under emulation.
- Going by message times, the twelve-package loop, perl included, finished
  within about two hours of being started.

## dropbear in the QEMU VM

The step 10 VM got networking and dropbear: `apk add` of dropbear and nano
over 9p, `/etc/network/interfaces` with `eth0` on DHCP, a root password (the
VM only), `rc-service networking start`, `rc-service dropbear start`.

First attempt, with `-nic user,model=virtio-net-pci,...`: no `eth0`.

- `ls /sys/class/net` showed only `lo` and `sit0`, and `ifup eth0` failed
  with `ip: ioctl 0x8913 failed: No such device` (0x8913 is `SIOCGIFFLAGS`).
- The PCI scan in `dmesg` had two virtio devices: `[1af4:1009]` (9p) and
  `[1af4:1001]` (disk). A virtio network card would be `[1af4:1000]`.
- `ps` showed the running QEMU had received the `-nic` option, and in that
  VM `dmesg | grep 1af4:1000` found nothing.

The cause is QEMU's riscv `virt` board. `-nic` only describes a card that the
board code must create, and `hw/riscv/virt.c` has no NIC code
(`hw/arm/virt.c` calls `pci_init_nic_devices()`). QEMU only warns
`requested NIC ... was not created (not supported by this machine?)` and
boots without a card. The fix is to plug the card in explicitly:

```sh
-netdev user,id=net0,hostfwd=tcp:127.0.0.1:2222-:22 -device virtio-net-pci,netdev=net0
```

With that, from a second shell in the x86_64 container:

```
$ ssh -p 2222 root@127.0.0.1 uname -m
riscv32
```

## What it means for the board

- dropbear for step 17 exists and works as an SSH server on riscv32.
- Step 15 tests apk with `apk add nano`; apk fetches with its own code, so
  curl is not needed for that.
- Without e2fsprogs there is no `fsck.ext4`; the pendrive root gets pass `0`
  in `fstab` until it exists.
- The riscv32 container can keep building in the background while the board
  work goes on.

## Next

Phase 4, step 12: Alpine binaries on the board under Buildroot, starting with
which dynamic loader name Buildroot's musl uses (`ls /lib/ld-musl*`).
