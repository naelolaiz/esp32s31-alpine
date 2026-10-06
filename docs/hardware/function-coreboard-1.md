# ESP32-S31 Function-CoreBoard-1

Board in use: Function-Core Board V1.0, ESP32-S31-WROOM-3 module.
Source: [Espressif user guide](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s31/esp32-s31-function-coreboard-1/user_guide.html).

## On board

| Interface | Notes |
| --- | --- |
| RJ45 Ethernet | 10/100/1000, PHY on the RGMII interface (Motorcomm per Espressif's kernel config) |
| USB 2.0 Type-A | Host port (USB-HS), DWC2 controller |
| USB Type-C | USB Serial/JTAG |
| USB Type-C | USB-to-UART, the Linux console |
| Audio | ES8311 codec, NS4150B amplifier, microphone, speaker connector |
| RGB LED | GPIO60 |
| microSD | **No slot.** SDMMC slot 0 pads are broken out on the header |

## microSD wiring (step 5)

The Korvo-1 board puts its microSD slot on the same SDMMC slot 0 pads, so the
community dw_mmc driver targets these pins. Exact GPIO-to-signal mapping must
be confirmed against the user guide's header table before wiring.

| Signal | GPIO (header) |
| --- | --- |
| SDIO_CLK, SDIO_CMD, SDIO_DATA0 to 3 | GPIO20 to GPIO25 (confirm each) |
| Power | 3.3 V and GND from the header |

Use a 3.3 V microSD breakout with pull-ups on CMD and DATA (add 10 kΩ if the
breakout has none). Keep wires short; start at a low clock if transfers fail.
