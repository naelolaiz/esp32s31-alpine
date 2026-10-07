# 2026-10-07: Ethernet and apk on the board (step 15)

Alpine on the board brings up `eth0` with DHCP at boot, reads the riscv32
repository built in steps 9 to 11 from the PC over HTTP, checks its
signature, and installs nano with `apk add`. Guide:
[`docs/steps/15-network-apk.md`](../steps/15-network-apk.md).

## Ethernet

`/etc/network/interfaces` got `lo` (loopback) and `eth0` (dhcp), and the
`networking` service went into the `boot` runlevel. After
`rc-service networking start`:

```
esp32s31:~# ip addr show eth0
2: eth0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc mq state UP qlen 1000
    link/ether 5a:67:21:eb:1c:2f brd ff:ff:ff:ff:ff:ff
    inet 192.168.0.18/24 scope global eth0
       valid_lft forever preferred_lft forever
esp32s31:~# ping -c 3 192.168.0.14
PING 192.168.0.14 (192.168.0.14): 56 data bytes
64 bytes from 192.168.0.14: seq=0 ttl=64 time=200.721 ms
64 bytes from 192.168.0.14: seq=1 ttl=64 time=341.541 ms
^C
```

- `valid_lft forever`: BusyBox's `/usr/share/udhcpc/default.script` adds the
  address without a lifetime; udhcpc keeps running and renews the lease.
- 192.168.0.14 is the PC. The third ping was cut off by Ctrl+C, not lost.
- The round trips, 200 and 341 ms, are far above the few milliseconds usual
  on a local network. The PC is on Wi-Fi; power saving on its Wi-Fi link is
  the likely cause (inferred, not measured). apk is not affected.

## The repository over HTTP

The PC served the porting directory's `.local/share/abuild` with
`python3 -m http.server 8080 --directory .local/share/abuild`, and the board
got `http://192.168.0.14:8080/main` in `/etc/apk/repositories`.

The first `apk update` printed `Connection refused`: the PC answered, but
nothing listened on port 8080 yet. With the server running, it answered on
the PC itself:

```
$ curl -I http://192.168.0.14:8080/main/riscv32/APKINDEX.tar.gz
HTTP/1.0 200 OK
Server: SimpleHTTP/0.6 Python/3.14.8
Content-type: application/gzip
Content-Length: 20879
```

The PC has no firewall rules (`nft list ruleset` empty, iptables policies
ACCEPT), and the next `apk update` from the board worked.

## The signature check

`/etc/apk/keys` holds two public keys. Step 13 copied every `*.rsa.pub` from
the abuild key directory, so that directory holds two key pairs; only the
one `abuild.conf` names in `PACKAGER_PRIVKEY` signs. With both moved away,
and back:

```
esp32s31:~# apk update
WARNING: updating and opening http://192.168.0.14:8080/main/riscv32/APKINDEX.tar.gz: UNTRUSTED signature
1 unavailable, 0 stale; 25 distinct packages available
...
esp32s31:~# apk update
main  [http://192.168.0.14:8080/main]
OK: 224 distinct packages available
```

Without the key, apk refuses the index and offers only the 25 installed
packages. With it, all 224 packages of the riscv32 repository, subpackages
included, are available.

## apk add nano

```
esp32s31:~# apk add nano
(1/3) Installing ncurses-terminfo-base (6.6_p20260822-r0)
(2/3) Installing libncursesw (6.6_p20260822-r0)
(3/3) Installing nano (9.2-r0)
Executing busybox-1.38.0-r7.trigger
OK: 8381 KiB in 28 packages
esp32s31:~# nano --version
 GNU nano, version 9.2
 (C) 2026 the Free Software Foundation and various contributors
 Compiled options: --disable-libmagic --disable-nls --enable-utf8
```

nano and ncurses are the packages built natively in step 11 under QEMU user
emulation; this is their first run on the board, and the first package
installed on the board itself.

## Reboot

`reboot` ran the shutdown runlevel, and the board then stayed silent: no ROM
banner, no new boot. The kernel ends a restart by printing
`reboot: Restarting system` (`kernel/reboot.c`) and calling the restart
handlers. Here that is the SBI SRST call (`SBI SRST extension detected` in
every boot log), so OpenSBI does the reset. Espressif's OpenSBI
(`platform/generic/espressif/esp32s31/esp32s31.c`, branch
`integration/v1.6-esp32s31`, read at 5395e03) handles every reset type,
shutdown included, the same way:

```c
static void s31_system_reset(u32 type, u32 reason)
{
	writel(HPCORE_SW_RESET, (void *)LP_AONCLKRST_HPCORE0_RESET_CTRL_REG);

	/* If the reset doesn't take effect (e.g. shutdown on a chip with no
	 * power-down path), spin in WFI so we don't return into garbage. */
	while (1)
		__asm__ __volatile__("wfi");
}
```

The register write is a software reset of HP CPU core 0 only (bit 20 of
`LP_AONCLKRST_HPCORE0_RESET_CTRL_REG`). On this board the chip does not come
back from it; why is open (the rest of the chip is not reset with the core,
inferred). `poweroff` takes the same path, so it, too, ends with the CPU
halted rather than powered down. The exact OpenSBI commit of the board's
build is not pinned yet (`bsp/versions.md`).

Tapping RST booted normally. The kernel mounted the stick without
`EXT4-fs (sda1): recovery complete`, so the shutdown had finished before the
hang. OpenRC brought the network up by itself:

```
 * Starting networking ... *   lo ... [ ok ]
 *   eth0 ...udhcpc: started, v1.38.0
udhcpc: broadcasting discover
udhcpc: broadcasting discover
udhcpc: broadcasting select for 192.168.0.19, server 192.168.0.1
udhcpc: lease of 192.168.0.19 obtained from 192.168.0.1, lease time 86400
 [ ok ]
```

The MAC address was new (`1e:cd:f0:c1:3e:2a`), and so was the address. The
first `apk update` after the boot printed `Host is unreachable`: the PC did not answer the board's ARP request in
time, and apk used its saved index (`1 stale`). The second one worked.

## RAM

Right after logging in, before apk ran:

```
esp32s31:~# free
              total        used        free      shared  buff/cache   available
Mem:          14736        4352        7204          36        3180        8892
```

| KiB | Step 14, no network | Step 15, `eth0` up |
| --- | --- | --- |
| `MemFree` | 7944 | 7204 |
| `MemAvailable` | 9852 | 8892 |
| `Buffers` + `Cached` | 3396 | 3180 |
| `Slab` | 1600 | 1712 |
| `KernelStack` | 232 | 248 |
| `AnonPages` | 160 | 216 |
| `PageTables` + `Percpu` + `VmallocUsed` | 164 | 180 |
| not attributed | 1240 | 1996 |

- The network costs 960 KiB of `MemAvailable`.
- `AnonPages` grew by 56 KiB, most likely udhcpc, the one new process;
  slab grew by 112 KiB.
- Most of it, 756 KiB, is memory `/proc/meminfo` has no line for. The
  Ethernet driver allocates its receive buffers as whole pages when the
  interface comes up, and those show in no counter; they are the likely
  explanation (inferred). Comparing `MemFree` before and after `ifdown eth0`
  would show it.
- Right after `apk add nano`, `free` had shown 8316 KiB available; the
  fresh-boot number above is the one to compare.

## Next

Step 16: pendrive hotplug and automount.
