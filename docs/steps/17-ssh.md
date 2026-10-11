# Step 17: SSH

Goal: log in to the board from the PC over the network, so the serial
console is only needed when the network is down.

Alpine on the board has Wi-Fi from [step 22](22-alpine-wifi.md) (or Ethernet
from [step 15](15-network-apk.md) with the cable plugged in). The SSH server
listens on every interface, so this step is the same for both.

## 1. Which server

Dropbear. It is the small SSH server Alpine uses on small systems, and it
was among the first packages built natively (alpine-riscv32 step 11), so it
is in the stick repository and on the Pages repository. OpenSSH is in the
package CI list as well (alpine-riscv32 `ci/native-packages.txt`), but its
server is larger, and the board has about 8 MiB of RAM left for programs.

| Package | What it holds |
| --- | --- |
| `dropbear` | `/usr/sbin/dropbear` (the server) and `/usr/bin/dropbearkey` (makes host keys) |
| `dropbear-openrc` | `/etc/init.d/dropbear` and `/etc/conf.d/dropbear`, the OpenRC service |

## 2. Install

BOARD, Alpine, as root:

```sh
apk add dropbear dropbear-openrc
```

With the Pages repositories from step 22 this needs no options. Without
the network, the stick repository has the same packages:
`apk add --repositories-file /dev/null -X /root/repo/main dropbear dropbear-openrc`.

## 3. A root password

BOARD:

```sh
passwd
```

The root account of the tree from step 13 has no password. The serial
console lets root log in without one, but dropbear refuses an empty
password, and the login would fail with `Permission denied`.

## 4. Start it, and start it at boot

BOARD:

```sh
rc-update add dropbear default
rc-service dropbear start
netstat -tln | grep ':22 '
```

- `default` is the runlevel OpenRC enters after `boot`, once the network
  is up: the service declares `need net`.
- The first start makes the host keys, RSA, ECDSA and Ed25519, in
  `/etc/dropbear` (`check_config` in `/etc/init.d/dropbear`). They stay on
  the stick, so the PC sees the same keys after every reboot. Making them
  takes longer than later starts.
- `netstat` should print `0.0.0.0:22` and `:::22` with `LISTEN`: dropbear
  accepts connections on every IPv4 and IPv6 address.

## 5. Log in from the PC

BOARD, for the address:

```sh
ip -4 addr show wlan0
```

HOST, any directory, with the address `ip` printed (on 2026-10-11 the
router gave the board 192.168.0.20):

```sh
ssh root@192.168.0.20
```

- `root@` is needed: without it, `ssh` uses the PC's user name, which does
  not exist on the board.
- The first connection shows the host key's fingerprint and asks whether
  to trust it. To check it against the board, BOARD:
  `dropbearkey -y -f /etc/dropbear/dropbear_ed25519_host_key`. Its
  `Fingerprint:` line should match what `ssh` shows for the ED25519 key.

## 6. Key login (optional)

HOST, any directory:

```sh
ssh-copy-id root@192.168.0.20
```

`ssh-copy-id` asks for the board's password once and appends the PC's
public key to `/root/.ssh/authorized_keys` on the board, which dropbear
reads. Later logins need no password. If the PC has no key yet,
`ssh-keygen -t ed25519` makes one.

Once key login works, the password login can be turned off. BOARD:

```sh
sed -i 's/^DROPBEAR_OPTS=.*/DROPBEAR_OPTS="-s"/' /etc/conf.d/dropbear
rc-service dropbear restart
```

`-s` makes dropbear refuse password logins (`dropbear -h` lists the
options; `-w` would refuse root logins, which needs another user first).
Keep the serial console at hand while trying this.

## 7. If it does not connect

| `ssh` prints | Meaning | What to do |
| --- | --- | --- |
| `Permission denied` | Wrong or empty password, or no `root@` | Section 3, and `ssh root@...` |
| `Connection refused` | The board answers, but nothing listens on port 22 | Section 4: `rc-service dropbear start`, then `netstat` |
| `No route to host` | The PC gets no answer for the board's address on the local network | Check `ip -4 addr show wlan0` on the board. On 2026-10-11 this came after the router restarted; reconnecting the PC's Wi-Fi fixed it. |
| `Connection timed out` | Packets get lost on the way | Same as above, then `ping` the board from the PC |

The board's address comes from the router by DHCP and can change. A
reservation for the board in the router's DHCP settings keeps it fixed.
