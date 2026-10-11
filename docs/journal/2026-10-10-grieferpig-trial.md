# 2026-10-10: Wi-Fi on this board with GrieferPig's image (step 19, section 5)

The optional trial from the [step 19 guide](../steps/19-native-wifi-study.md),
section 5. GrieferPig's release image `s31_full_flash.bin` was flashed over
a full backup of our flash, with the Alpine stick unplugged. It joined a
WPA2 network, with Linux on both cores. So the radio, the antenna and his
driver work on the Function-CoreBoard-1 before the port in step 21 starts.

## The flash backup

`read-flash 0 0x1000000` saved all 16 MiB of our flash first (step 20
state: patched kernel and DTB, step 14b cramfs). The file was 16777216
bytes. `erase-flash` then emptied the chip, and `write-flash 0x0` wrote his
image.

## The password prompt and miniterm

The first `esp32-config wifi connect` failed without waiting for input:

```
~ # esp32-config wifi connect 'NETWORK'
Wi-Fi password:
esp32-config: The password must contain 8 to 63 bytes.
```

miniterm sends Enter as CR and LF by default. The board's terminal turns
the CR into a newline, which ends the command, and the LF that follows is
read as an empty password. His tool reads the password with one
`IFS= read -r` after turning echo off (`read_secret` in
`overlay/usr/lib/esp32-config/common.sh`). Starting miniterm with
`--eol CR` fixed it, and the guide now uses that option.

## On the board

```
~ # esp32-config wifi connect 'NETWORK'
Wi-Fi password:
# reading passphrase from stdin
Connecting to the wireless network...
Connected to the wireless network; getting an IP address...
Connected: 192.168.0.20/24
~ # ip addr show wlan0
2: wlan0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc noqueue state UP qlen 1000
    inet 192.168.0.20/24 brd 192.168.0.255 scope global wlan0
~ # ping -c 3 192.168.0.14
64 bytes from 192.168.0.14: seq=0 ttl=64 time=104.555 ms
~ # cat /sys/devices/system/cpu/online
0-1
~ # free
              total        used        free      shared  buff/cache   available
Mem:          15124        5476        5624         156        4024        8644
Swap:             0           0           0
```

(Network name and MAC address left out.)

- WPA2-Personal with AES (CCMP) associates and DHCP gives an address. The
  ping went over Wi-Fi only, since the Ethernet cable is unplugged. The PC
  is on Wi-Fi as well, which fits the 100 ms round trip (step 15 also saw
  slow pings to it). Only one ping was sent before it was stopped.
- `0-1`: his kernel runs Linux on both harts. Ours runs on hart 0 only
  (`0`, step 19).
- 8644 KiB available with Wi-Fi up, against 8892 KiB on our Alpine after
  boot without Wi-Fi (step 15). That is not a like-for-like comparison: his
  user space is a small Buildroot squashfs, ours is Alpine with OpenRC. Most
  of the radio's memory is internal SRAM (0x2F030000 to 0x2F07CFB0), which
  is not part of `total`, so Wi-Fi costs less of the 15 MB than its size
  suggests (inferred, not measured on our kernel).

Afterwards the backup goes back with `write-flash 0x0`.

## What this decides

The trial removes the biggest unknown of the port: that this radio and
antenna work with GrieferPig's code at all. Whether our Alpine gets Wi-Fi
by porting his radio driver into the Espressif kernel (step 21) or by
switching to his whole boot stack and kernel is the next decision.
