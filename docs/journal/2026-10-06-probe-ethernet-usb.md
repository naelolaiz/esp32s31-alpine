# 2026-10-06: Ethernet and USB host work (step 3)

## Ethernet

- `ip link set eth0 up`: PHY found as **YT8531** (Motorcomm gigabit), `rgmii-id`,
  PHY interrupt polled (`irq=POLL`).
- Link up at **100 Mbps full duplex** with flow control. The PHY is gigabit, so
  either the switch port is 100M or the link negotiated down; worth checking on a
  known gigabit port later.
- BusyBox `udhcpc` got 192.168.0.15/24 from 192.168.0.1; DNS set.
- Ping gateway: 4.1 to 8.4 ms (first packet slower); ping 1.1.1.1: 16.7 ms, 0% loss.
- **The MAC address changes on every boot** (26:3d:84:b9:3f:70 on the first boot,
  42:d6:3a:6f:40:0d on this one): no MAC is stored, so the driver picks a random
  one. The DHCP lease and IP will change between boots until we pin a MAC
  (device tree `local-mac-address`, U-Boot `ethaddr`, or one derived from the chip's eFuse).

## USB host

- Plugging a Lenovo keyboard enumerated it on the dwc2 host as a low-speed device
  (17ef:6099). No input driver is bound because `CONFIG_INPUT` is off in
  `esp32s31_minimal`; enumeration is what this step needed.
- So the host controller, PHY and port power work. USB mass storage (step 6) is a
  kernel-config job, not driver bring-up.

## Consequences for the plan

- Ethernet is the network path for apk from Phase 4 on; the NFS-root fallback is viable.
- Phase 2 storage on USB needs only config fragments; microSD still needs the
  dw_mmc port and wiring.
- New small task: pin the Ethernet MAC (tracked as an open question in plan.md).
