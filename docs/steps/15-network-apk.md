# Step 15: Ethernet and apk on the board

Goal: Alpine on the board gets an address over Ethernet at boot, and
`apk add nano` installs a package from the repository built in steps 9 to 11.
The PC serves that repository over HTTP, and apk checks its signature.

The board boots Alpine from the stick by itself since step 14b. Plug in the
Ethernet cable before the reset: step 3 showed the board's port working
under Buildroot with `udhcpc -i eth0`.

## 1. What Alpine needs

| What | Why |
| --- | --- |
| `/etc/network/interfaces` | Lists the interfaces to bring up at boot and how. Nothing in the tree from step 13 configures `eth0` yet. |
| The `networking` service | Runs `ifup` for every `auto` interface in that file. Alpine's installer puts it in the `boot` runlevel. |
| A repository URL in `/etc/apk/repositories` | Step 13 passed the repository on the command line (`apk add --repository`), which apk does not save. |
| The signing key in `/etc/apk/keys` | Already there: step 13 copied the local abuild public key into the tree. apk refuses an index whose signature it cannot check against these keys. |

Everything used here is already in the 25 packages from step 13. BusyBox
provides `ifup`, `ip`, `udhcpc` (the DHCP client), `ntpd` and `wget`. The
busybox package also installs `/usr/share/udhcpc/default.script`, which
`udhcpc` runs to apply a lease: the address, the default route and
`/etc/resolv.conf`.

## 2. Bring up Ethernet

BOARD, Alpine logged in as root. One `printf` writes the whole file, because
a multi-line paste over the serial console can lose a line:

```sh
printf 'auto lo\niface lo inet loopback\n\nauto eth0\niface eth0 inet dhcp\n' > /etc/network/interfaces
cat /etc/network/interfaces
```

```
auto lo
iface lo inet loopback

auto eth0
iface eth0 inet dhcp
```

- `lo` is the loopback interface (`127.0.0.1`). The kernel creates it but
  leaves it down, and local programs that talk to themselves over the network
  need it.
- `auto eth0` makes the service bring `eth0` up at boot. `inet dhcp` makes
  BusyBox `ifup` start `udhcpc -b -R -p /var/run/udhcpc.eth0.pid -i eth0`.
  `-b` sends udhcpc to the background if no lease arrives in time, so a boot
  without a cable does not hang. `-R` releases the lease when udhcpc stops.
  udhcpc keeps running afterwards, to renew the lease.

Enable the service for the next boots and start it now:

```sh
rc-update add networking boot
rc-service networking start
ip addr show eth0
ip route
cat /etc/resolv.conf
```

- `rc-service networking start` prints ` * Starting networking`, then a line
  for `lo` and one for `eth0`.
- `ip addr show eth0` should show an `inet` line with the address from your
  router, `ip route` a `default via` line, and `/etc/resolv.conf` a
  `nameserver` line.
- The board's MAC address changes at every boot (step 3, open item in the
  plan), so the router may hand out a different address each time. For apk
  that does not matter: the board only connects out.

## 3. Internet and the clock

BOARD. This checks that names resolve and the internet answers, then sets
the clock once from the network:

```sh
ping -c 3 pool.ntp.org
ntpd -n -q -p pool.ntp.org
date
```

- `ntpd -n -q`: `-n` stays in the foreground, `-q` quits once the clock is
  set, `-p` names the server. Without an RTC, the clock has run from the last
  shutdown time since step 14. Now it is right, and `swclock` saves it at the
  next `poweroff`.
- Alpine's `ntpd` service (`rc-update add ntpd default`) would keep the clock
  in sync with a daemon running all the time. It is left out for now to keep
  RAM for the steps ahead; the one-shot command is enough after a boot.

## 4. Serve the repository from the PC

The packages built in steps 9 to 11 are on the PC, under
`.local/share/abuild/main/riscv32/` in the porting directory (the directory
mounted at `/work` in the containers). apk downloads an index,
`APKINDEX.tar.gz`, and then the `.apk` files it lists, with plain HTTP GET
requests, so Python's built-in web server is enough.

HOST, in the porting directory. The first command checks that the index and
nano are there; the second prints the PC's address on the local network:

```sh
ls .local/share/abuild/main/riscv32/ | grep -E '^(APKINDEX|nano|ncurses)'
ip -4 -o addr show scope global
```

HOST, same directory. This serves `.local/share/abuild` on port 8080 until
you stop it with Ctrl+C, and logs each request:

```sh
python3 -m http.server 8080 --directory .local/share/abuild
```

Leave it running in its own terminal. Everyone on the local network can read
the packages while it runs; nothing else of the PC is served.

## 5. apk on the board

BOARD. Replace the address with the one `ip -4 -o addr` printed on the PC
(only the address, without the `/24`):

```sh
echo http://192.168.1.10:8080/main > /etc/apk/repositories
apk update
```

- apk adds the architecture to the URL and fetches
  `http://.../main/riscv32/APKINDEX.tar.gz`; the PC's terminal logs the
  request.
- `apk update` ends with a line counting the packages available.

If apk prints a warning instead, its last words say how far the request got:

| Warning ends with | Meaning |
| --- | --- |
| `Connection refused` | The PC answered, but nothing listens on port 8080: the server is not running, or a firewall rule rejects the connection. |
| `Host is unreachable` | The PC did not answer the board's ARP request ("who has this address?") at all. A PC on Wi-Fi can answer late; run `apk update` again. |
| `UNTRUSTED signature` | The index arrived, but no key in `/etc/apk/keys` matches its signature (the test below). |

With a warning, apk keeps the index it saved last time, if any, and says
`1 stale`.

Now the signature check. The index is signed with the abuild key from step
7, and apk accepts it only because the public key is in `/etc/apk/keys`. Move
the key away, update, and put it back:

```sh
ls -l /etc/apk/keys
mkdir /root/keys-off
mv /etc/apk/keys/* /root/keys-off/
apk update
mv /root/keys-off/* /etc/apk/keys/
rmdir /root/keys-off
apk update
```

Without the key, `apk update` reports an error about an untrusted signature
and uses no packages from the repository. With the key back, it works
again.

Install nano and try it:

```sh
apk add nano
nano --version
apk info | wc -l
free
```

- `apk add nano` pulls in ncurses' libraries and terminal descriptions, as in
  step 11's QEMU test.
- `nano` draws with the `TERM` the getty set, `vt100` (step 14). Exit with
  Ctrl+X.
- `free` shows the RAM with udhcpc running, to compare with step 14's 9912
  KiB available.

## 6. Reboot

BOARD:

```sh
reboot
```

OpenRC runs the shutdown runlevel, and the kernel prints its last line,
`reboot: Restarting system`. The board then stays silent: Espressif's
OpenSBI answers the restart request by resetting CPU core 0 only, and the
chip does not come back from that (journal of this step). Tap **RST** once
that line has appeared; the root is read-only by then, so nothing is lost.

The stick boots Alpine again (step 14b). OpenRC now prints
` * Starting networking`, with `lo` and `eth0`, in the boot runlevel. After
logging in, `apk add` works without any setup, as long as the PC still
serves the repository.

Done when: `eth0` gets an address at boot, `apk update` fails without the
key and works with it, and `apk add nano` installs nano from the PC.
Next: step 16, pendrive hotplug.
