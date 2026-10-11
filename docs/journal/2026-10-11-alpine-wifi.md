# 2026-10-11: Alpine's own Wi-Fi, and channels 12 and 13 (steps 22 and 22a)

Start: Alpine boots from the stick on GrieferPig's stack (his image in
flash, our `/sbin/init` in his persist overlay). Wi-Fi had worked once the
evening before, through his wpa_supplicant in a chroot of his squashfs,
started by hand. The package CI had just published wpa_supplicant, libnl3,
iw and wireless-regdb on Pages.

End: Alpine installs from Pages, its own wpa_supplicant joins the network
at boot, and his image is rebuilt with one driver patch so the board also
uses 2.4 GHz channels 12 and 13.

## Wi-Fi lost after every reboot

The boot script `grieferpig/alpine/etc/local.d/gp-wifi.start` had never
been copied to the stick (`sh: can't open '/etc/local.d/gp-wifi.start'`),
so nothing started Wi-Fi at boot. Started by hand twice, two of his
wpa_supplicants ran on `wlan0` at the same time (`ps`: PIDs 546 and 1095).
Step 22 section 2 now gives the commands by hand, with a warning against a
second instance.

## SCANNING: the router was on channel 13

With one wpa_supplicant left, it stayed in `wpa_state=SCANNING` and `udhcpc`
got no lease. Its debug output had no association attempt at all, and the
scan results had 38 networks, none of them the router's. The frequencies in
the debug log stopped at channel 11:

```
     30 freq=2412
      1 freq=2417
      5 freq=2422
      9 freq=2427
     18 freq=2437
      3 freq=2442
      3 freq=2447
      6 freq=2452
     20 freq=2462
```

The PC's own scan (`iw dev ... scan`, filtered for the network name) found
it at `2472.0` and `5500.0`: the router's
automatic channel choice had put its 2.4 GHz network on channel 13.

- His driver, `drivers/net/wireless/espressif/esp32s31_softmac.c` in
  `linux-esp32-s31` (commit `52dc6ea`), registers an 11-entry channel table
  with mac80211 and drops received frames from channels above 11.
- His radio firmware already allows 1 to 13: `s31_wifi_prepare()` sets the
  ESP-IDF country `CN` with channels 1 to 13, and `S31_WIFI_SET_CHANNEL`
  accepts up to 13.
- Ruled out on the way: the `combo` radio mode, and `bt_enable=-115` in
  `radio_health`, which is the driver's initial value (Bluetooth never
  started, since his BTstack does not run under Alpine).

## His image with channels 12 and 13 (step 22a)

[`grieferpig/patches/linux-esp32-s31/0001-wifi-esp32s31-softmac-channels-12-and-13.patch`](../../grieferpig/patches/linux-esp32-s31/0001-wifi-esp32s31-softmac-channels-12-and-13.patch)
makes the table 13 entries long and the receive check use the table's size.
The driver is part of his radio module, which his build makes together
with the kernel, the radio firmware and the root filesystem, so
[`.github/workflows/grieferpig-image.yml`](../../.github/workflows/grieferpig-image.yml)
runs his whole build at his commit `1c132b4`. Run 38141687653 took 18
minutes and produced one file per slot:

| File | Bytes | Slot size |
| --- | --- | --- |
| `spl_app.bin` | 38560 | 48 KiB |
| `u-boot.itb` | 253208 | 320 KiB |
| `esp32s31_generic.dtb` | 26352 | 64 KiB |
| `radio.bin` | 1514384 | 1.5 MiB |
| `xipImage` | 6228940 | 6 MiB |
| `rootfs.sqfs` | 3125248 | 6 MiB |

The kernel has 62 KiB left in its slot. The six slots were flashed, not
`persist` (step 22a sections 4 and 5). Alpine booted from the stick as before, and
`iw list` now shows:

```
                        * 2467.0 MHz [12] (20.0 dBm) (no IR)
                        * 2472.0 MHz [13] (20.0 dBm)
```

`no IR`: with the world regulatory domain, cfg80211 lets the board only
listen on those channels until it has heard a beacon there. Channel 13 had
lost the mark already, so some network was on it (inferred). By then the
router had been moved to a channel from 1 to 11 and back to automatic, so
an association on channel 13 itself is not recorded yet.

## Pages: `certificate verify failed`

The first HTTPS download on the board failed:

```
Connecting to naelolaiz.github.io (185.199.109.153:443)
34CF7B95:error:0A000086:SSL routines:tls_post_process_server_certificate:certificate verify failed:ssl/statem/statem_clnt.c:2124:
ssl_client: SSL_connect
wget: error getting response: Connection reset by peer
```

Installing `ca-certificates-bundle` from the stick repository and setting
the clock with `ntpd -q` fixed it; the two were not tried separately. The
bundle is the likely cause: the tree from step 13 is `alpine-base` and what
was added since, and none of `alpine-base`, apk-tools, libcrypto3, libssl3,
dropbear or python3 depends on it (checked in the packages' `.PKGINFO`).
Without `/etc/ssl/certs/ca-certificates.crt` (121 certificates in our
build), `ssl_client` has no certificate authority to check github.io
against. Step 22 section 3 now does both before the first download.

## Alpine's wpa_supplicant

GrieferPig's tool had written a plain configuration (`ctrl_interface`,
`update_config=0`, `ap_scan=1`, one `network` block), which Alpine's
wpa_supplicant reads unchanged from `/etc/wpa_supplicant/wpa_supplicant.conf`.
The OpenRC service found `wlan0` without extra options:

```
esp32s31:~# ps | grep '[w]pa_supplicant'
  763 root      0:00 supervise-daemon wpa_supplicant --start --respawn-delay 2 --respawn-max 5 --respawn-period 1800 /sbin/wpa_supplicant -- -iwlan0 -c/etc/wpa_supplicant/wpa_supplicant.conf
  764 root      0:00 /sbin/wpa_supplicant -iwlan0 -c/etc/wpa_supplicant/wpa_supplicant.conf
esp32s31:~# ifup wlan0
udhcpc: started, v1.38.0
udhcpc: broadcasting discover
udhcpc: broadcasting discover
udhcpc: broadcasting discover
udhcpc: broadcasting select for 192.168.0.20, server 192.168.0.1
udhcpc: lease of 192.168.0.20 obtained from 192.168.0.1, lease time 86400
esp32s31:~# wpa_cli -i wlan0 status | grep -e wpa_state -e key_mgmt
key_mgmt=WPA2-PSK
wpa_state=COMPLETED
```

- `supervise-daemon` restarts wpa_supplicant if it dies.
- With `auto wlan0` in `/etc/network/interfaces` and wpa_supplicant in the
  `boot` runlevel, `wlan0` got its address after a reboot without anything
  run by hand, before and after flashing the rebuilt image.
- His wpa_supplicant and the chroot of his squashfs are no longer needed.
  His `/init` still loads the radio module from his squashfs at boot.

## SSH

Dropbear (installed the evening before, step 17) was running and listening
on port 22, yet `ssh root@192.168.0.20` from the PC printed `No route to
host`: the PC got no answer for the board's address. It came after the
router had restarted with a new channel setting, and reconnecting the PC's
Wi-Fi fixed it. Which side kept the stale state was not established.

## Open

- `free` with Alpine's wpa_supplicant running is not measured yet.
- An association on channel 12 or 13 is not recorded yet (see above).
- The channel patch is worth sending to GrieferPig.
