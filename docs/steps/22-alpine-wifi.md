# Step 22: Wi-Fi with Alpine's own wpa_supplicant

Goal: Alpine on the board installs packages over Wi-Fi from the repository
on GitHub Pages, and joins the Wi-Fi network at boot with its own
wpa_supplicant, started by OpenRC.

Since 2026-10-10 the board runs GrieferPig's boot stack and kernel
([trial](../journal/2026-10-10-grieferpig-trial.md)). His `/init` loads the
radio module, so `wlan0` exists before Alpine starts, and
[`grieferpig/persist/sbin/init`](../../grieferpig/persist/sbin/init) then
switches to Alpine on the stick. Until now Alpine had no wpa_supplicant of
its own: Wi-Fi came from his copy, run from his read-only root in a chroot
(by hand, or by the stopgap script
[`gp-wifi.start`](../../grieferpig/alpine/etc/local.d/gp-wifi.start)). The radio driver port of step
21 is not needed on his kernel.

GitHub Actions now builds wpa_supplicant, libnl3 and iw and publishes them
on Pages ([CI guide](https://github.com/naelolaiz/alpine-riscv32/blob/main/docs/steps/23-package-ci.md)).

## 1. What changes

| What | Before | After |
| --- | --- | --- |
| Package repository | directories on the stick (`/root/repo/...`) | `https://naelolaiz.github.io/alpine-riscv32/...` |
| Signing keys | `-6ac57722.rsa.pub` (the PC's key) | also `alpine-riscv32-ci.rsa.pub` (the workflow's key) |
| wpa_supplicant | GrieferPig's, in a chroot | Alpine's, started by its OpenRC service |
| Wi-Fi profile | `/etc/esp32-conf/wpa_supplicant.conf` in his overlay | `/etc/wpa_supplicant/wpa_supplicant.conf` on the stick |
| DHCP on `wlan0` | `udhcpc` by hand | the `networking` service, from `/etc/network/interfaces` |

## 2. Network for now

The steps below download from the internet, so Wi-Fi has to work once more
with GrieferPig's wpa_supplicant. BOARD, Alpine logged in as root, on the
serial console. First check which MTD partitions hold his root and his
overlay:

```sh
grep -e rootfs -e persist /proc/mtd
```

On this board `mtd7` is `"rootfs"` (his squashfs) and `mtd5` is `"persist"`
(his JFFS2 overlay); `/dev/mtdblockN` is the block device of `mtdN`. Then,
after a fresh boot:

```sh
mkdir -p /mnt/gp /mnt/persist
mount -t squashfs -o ro /dev/mtdblock7 /mnt/gp
mount -t jffs2 -o ro /dev/mtdblock5 /mnt/persist
mount -o bind /dev /mnt/gp/dev; mount -t proc proc /mnt/gp/proc; mount -t sysfs sysfs /mnt/gp/sys
mount -t tmpfs -o mode=0755 tmpfs /mnt/gp/run; mount -t tmpfs -o mode=1777 tmpfs /mnt/gp/tmp
cp /mnt/persist/upper/etc/esp32-conf/wpa_supplicant.conf /mnt/gp/run/wpa.conf
ip link set wlan0 up
chroot /mnt/gp wpa_supplicant -B -D nl80211 -i wlan0 -c /run/wpa.conf
sleep 10; chroot /mnt/gp wpa_cli -p /run/wpa_supplicant -i wlan0 status | grep wpa_state
udhcpc -i wlan0
```

- His squashfs has his wpa_supplicant with the libraries it was built
  against; `chroot` runs it there. His overlay has the Wi-Fi profile that
  `esp32-config wifi connect` wrote.
- `dev`, `proc` and `sys` give the chroot the same kernel interfaces as
  Alpine. His root is read-only, so `run` (wpa_supplicant's control socket)
  and `tmp` (wpa_cli's reply socket) get a tmpfs each.
- `wpa_state=COMPLETED` means the board joined the network; `udhcpc` then
  prints `lease of ... obtained`.
- Start it once only. A second wpa_supplicant on the same `wlan0` makes
  both scan forever (`wpa_state=SCANNING`, seen on 2026-10-11), and
  mounting the tmpfs again hides the first one's control socket.
  `ps | grep '[w]pa_supplicant'` should print one line;
  `killall wpa_supplicant` stops them all.

`SCANNING` that never ends with only one wpa_supplicant running can also
mean the router is on a channel the board does not scan. GrieferPig's
driver (`drivers/net/wireless/espressif/esp32s31_softmac.c`) registers only
2.4 GHz channels 1 to 11 (2412 to 2462 MHz). On 2026-10-11 the router's
automatic channel choice had moved its 2.4 GHz network to channel 13
(2472 MHz), and the board saw 38 other networks but not that one. Setting
the router to a fixed channel 1, 6 or 11 fixes it;
[step 22a](22a-grieferpig-rebuild.md) instead rebuilds his image with
channels 12 and 13. To check which channels the board hears:

```sh
timeout 20 chroot /mnt/gp wpa_supplicant -d -D nl80211 -i wlan0 -c /run/wpa.conf > /tmp/wpa-debug.log 2>&1
grep -o 'freq=[0-9]*' /tmp/wpa-debug.log | sort | uniq -c
```

The stopgap script
[`gp-wifi.start`](../../grieferpig/alpine/etc/local.d/gp-wifi.start) does
the same at boot, when it is installed in `/etc/local.d`. After this step
it is not needed.

## 3. Certificates and the clock

BOARD:

```sh
apk add --repositories-file /dev/null -X /root/repo/main ca-certificates-bundle
ntpd -n -q -p pool.ntp.org
date
```

- `ca-certificates-bundle` is `/etc/ssl/certs/ca-certificates.crt`, the
  certificate authorities that `wget` (through `ssl_client`) and `apk` check
  the github.io certificate against. The board's root was made from
  `alpine-base` in step 13, which does not depend on this package. Without
  it `wget` stops with `certificate verify failed`.
- The board has no RTC. OpenRC's `swclock` sets the clock to the last clean
  shutdown, which can be days ago (a reset with RST saves nothing). The
  github.io certificate is only valid between two dates, and `wget` and
  `apk` refuse the connection when the clock is outside them, with the same
  `certificate verify failed`.

## 4. The workflow's key and the Pages repositories

apk installs only packages and indexes signed by a key in `/etc/apk/keys`.
The board has the PC's key since step 13. What GitHub Actions builds is
signed with the workflow's key, so that one goes in too. BOARD:

```sh
wget -O /etc/apk/keys/alpine-riscv32-ci.rsa.pub https://naelolaiz.github.io/alpine-riscv32/alpine-riscv32-ci.rsa.pub
ls /etc/apk/keys
```

- Keep the file name: the signature in each index and package names the
  key file apk has to check it with.
- `ls` should list both `-6ac57722.rsa.pub` and `alpine-riscv32-ci.rsa.pub`.
- If `wget` prints `ssl_client: not found`, BusyBox `wget` is missing its
  HTTPS helper. Install it from the stick, then run `wget` again:
  `apk add --repositories-file /dev/null -X /root/repo/main ssl_client`.
- Optional, to check that this is the key you made in the
  [CI guide](https://github.com/naelolaiz/alpine-riscv32/blob/main/docs/steps/23-package-ci.md)
  section 2: `sha256sum alpine-riscv32-ci.rsa.pub` on the HOST, in the
  directory holding `aports`, and
  `sha256sum /etc/apk/keys/alpine-riscv32-ci.rsa.pub` on the BOARD print
  the same value.

BOARD. One `printf` writes the whole file, because a multi-line paste over
the serial console can lose a line:

```sh
printf 'https://naelolaiz.github.io/alpine-riscv32/main\nhttps://naelolaiz.github.io/alpine-riscv32/community\nhttps://naelolaiz.github.io/alpine-riscv32/testing\n' > /etc/apk/repositories
apk update
apk upgrade
```

- The file replaces the stick's directories. The stick repository still
  works without the network, with `--repositories-file /dev/null -X
  /root/repo/main` as before.
- `apk update` downloads the three indexes (apk adds
  `/riscv32/APKINDEX.tar.gz` to each line) and checks their signatures. An
  `UNTRUSTED signature` line means a key is missing in `/etc/apk/keys`.
- `apk upgrade` installs newer builds of what is already installed. Pages
  carries the packages-2026-10-10 release over unchanged, so expect few
  changes or none.

## 5. Install wpa_supplicant and iw

BOARD:

```sh
apk add wpa_supplicant iw
ls /etc/init.d/wpa_supplicant
```

- wpa_supplicant brings libnl3, the netlink library it talks to the kernel's
  Wi-Fi layer (nl80211) with.
- `wpa_supplicant-openrc`, the service script, comes along by itself
  because openrc is installed; `ls` checks that it is there.
- iw is not needed for the connection. It shows the link from the kernel's
  side (`iw dev wlan0 link`), which helps when something does not work.

## 6. The Wi-Fi profile

GrieferPig's `esp32-config wifi connect` wrote a plain wpa_supplicant
configuration in his overlay, which Alpine's wpa_supplicant reads as it is.
Section 2 mounted that overlay read-only on `/mnt/persist`.
BOARD:

```sh
install -D -m 600 /mnt/persist/upper/etc/esp32-conf/wpa_supplicant.conf /etc/wpa_supplicant/wpa_supplicant.conf
sed -E 's/^([[:space:]]*)(ssid|psk)=.*/\1\2=(hidden)/' /etc/wpa_supplicant/wpa_supplicant.conf
```

- `/etc/wpa_supplicant/wpa_supplicant.conf` is where Alpine's service looks.
- `-m 600`: the file holds the Wi-Fi key, so only root may read it.
- `sed` prints the file with the network name and key hidden, safe to
  paste. His tool writes `ctrl_interface=/run/wpa_supplicant`,
  `update_config=0`, `ap_scan=1` and one `network={...}` block.

## 7. Alpine's wpa_supplicant, started by hand

First tell the networking service about `wlan0`. BOARD, one `printf` for the
whole file again:

```sh
printf 'auto lo\niface lo inet loopback\n\nauto wlan0\niface wlan0 inet dhcp\n\niface eth0 inet dhcp\n' > /etc/network/interfaces
cat /etc/network/interfaces
```

- `auto wlan0` makes the `networking` service (in the boot runlevel since
  step 15) bring `wlan0` up at boot. `inet dhcp` makes BusyBox `ifup` start
  `udhcpc -b`, which keeps asking for an address in the background until
  the association is done.
- `eth0` stays as in step 15: no `auto`, `ifup eth0` by hand with the cable.

Now replace GrieferPig's wpa_supplicant with Alpine's. BOARD, on the serial
console (this drops SSH for a moment):

```sh
killall wpa_supplicant udhcpc
rc-service wpa_supplicant start
ps | grep '[w]pa_supplicant'
ifup wlan0
wpa_cli -i wlan0 status | grep -e wpa_state -e key_mgmt
ip -4 addr show wlan0
```

- `killall` stops GrieferPig's wpa_supplicant (the chroot's) and the
  `udhcpc` of section 2.
- `rc-service` starts `/sbin/wpa_supplicant` from Alpine with the profile
  from section 6. `ps` shows the command line the service chose; it should
  name `wlan0` and `/etc/wpa_supplicant/wpa_supplicant.conf`.
- `ifup wlan0` starts `udhcpc` the way the networking service will at boot.
- `wpa_state=COMPLETED` means the board joined the network and finished
  the WPA2 handshake. `ip` should show an address again.

If `ps` shows no `wlan0`, the service did not find the interface by itself:
add `wpa_supplicant_args="-i wlan0"` to `/etc/conf.d/wpa_supplicant` and run
`rc-service wpa_supplicant restart`.

## 8. At boot

BOARD:

```sh
rc-update add wpa_supplicant boot
rc-update add ntpd default
rc-update show | grep -e networking -e wpa_supplicant -e ntpd -e local
reboot
```

- `boot`: the same runlevel as `networking`. Which of the two finishes
  first does not matter, because `udhcpc -b` keeps trying.
- `ntpd` is BusyBox's NTP client as a service (`busybox-openrc`, options in
  `/etc/conf.d/ntpd`: `-N -p pool.ntp.org`). It sets the clock once the
  network is up and keeps it right, so section 3's `ntpd -q` is not needed
  after a reboot.
- If you installed the stopgap script, turn it off before the reboot with
  `chmod -x /etc/local.d/gp-wifi.start`. OpenRC's `local` service runs only
  executable `.start` files, and the file stays, for going back.
- If the board does not restart after `reboot`, tap RST.

After the boot, log in on the serial console:

```sh
ip -4 addr show wlan0
rc-status boot | grep -e wpa_supplicant -e networking
rc-status default | grep ntpd
date
free
```

The three services should be `[ started ]`, `wlan0` should have an address
without anything run by hand, and `date` should print the current time.
`free` shows what Alpine's wpa_supplicant leaves of the RAM (8548 KiB
available with no Wi-Fi on 2026-10-10). SSH from the PC then works with the
address `ip` printed ([step 17](17-ssh.md)).

Do not run `setup-alpine`, which the login message suggests. It is Alpine's
installer: `setup-apkrepos` would point `/etc/apk/repositories` at the
official mirrors, which have no riscv32 packages, `setup-interfaces` would
rewrite `/etc/network/interfaces`, and `setup-disk` partitions and formats
a disk, here the stick Alpine runs from. Single parts are safe on their own,
for example `setup-timezone` (after `apk add tzdata`) or `setup-hostname`.

## 9. Going back

To GrieferPig's wpa_supplicant, BOARD: stop Alpine's from starting at boot,
then start his by hand as in section 2 (or `chmod +x` the stopgap script, if
installed):

```sh
rc-update del wpa_supplicant boot
```

To the stick repository, BOARD:

```sh
printf '/root/repo/main\n/root/repo/community\n/root/repo/testing\n' > /etc/apk/repositories
```
