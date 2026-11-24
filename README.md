# nicAI950

nicAI950 is firmware for the **Radtel RT-950 Pro** handheld radio. It runs on the ArteryTek AT32A403AVGT7 and uses the radio's existing bootloader.

For keypad controls, channel memories, squelch, broadcast radio, GPS, and APRS, see the [Operation Manual](OPERATION_MANUAL.md).

## Features

- Three selectable VFOs and 32 shared channel memories.
- Simplex FM receive and transmit with independent RX and TX bandwidths and CTCSS or DCS tones.
- EXNOISE-based voice squelch with nine thresholds and a monitor setting.
- A 240 × 320 colour display with keypad and rotary encoder navigation.
- An Si4732 broadcast receiver for FM, longwave, mediumwave, and shortwave.
- GPS position and UTC display, plus 1200-baud APRS position beacons and packet reception.
- Automatic storage of settings and memory channels in external SPI flash.

## Build

The project targets Windows with **Keil MDK-ARM µVision**, **Arm Compiler 6.23**, and the **ArteryTek AT32A403A Device Family Pack 2.0.8**.

1. Open [nicai950.uvprojx](nicai950.uvprojx) in µVision.
2. Select **Target_1** and rebuild the target.
3. Check the build log for `Created binary: Objects\nicai950.btf`. The post-build step uses the included `Hex2Btf950.exe` to package the Intel HEX image.

To rebuild from a shell in the project root, run the following with `uVision.com` on your path.

```powershell
uVision.com -r nicai950.uvprojx -j0 -o Objects\build.txt
```

| Build output | Purpose |
| --- | --- |
| `Objects/nicai950.btf` | Firmware package for the radio's bootloader |
| `Objects/nicai950.hex` | Intel HEX image used by the packager |
| `Objects/nicai950.axf` | Linked ARM image |
| `Listings/nicai950.map` | Linker map for checking image addresses and memory use |

## Flash

1. For a first installation, keep a copy of your original firmware and back up your own radio's flash. [RT-950 Pro backup and restore instructions](https://github.com/nicsure/RT-950-Pro-Web-Flasher#nand-backup-and-restore)
2. With the radio off, hold the two lowest side buttons while turning it on to reach the **UPDATE** screen. These are side keys 3 and 4 in Radtel's numbering. [Radtel update instructions](https://manuals.plus/m/d81dcc9dcd074f03a970d443bac362ccbef39a46bcc26a8acd40a16dfb87d969.pdf)
3. Connect the USB programming cable. Open the flasher in a browser with Web Serial support, such as Chrome or Edge. [RT-950 Pro Web Flasher](https://nicsure.github.io/RT-950-Pro-Web-Flasher/)
4. Choose `Objects/nicai950.btf`, select **Connect serial device**, choose the programming port, then select **Flash firmware**. Keep the radio powered and connected until flashing completes.
5. Start the radio normally and follow the [quick start](OPERATION_MANUAL.md#quick-start).

## Source and memory layout

| Path | Contents |
| --- | --- |
| [Source/main.c](Source/main.c) | Screens, menus, VFOs, channel memories, and application settings |
| [Source/board.c](Source/board.c) | Clock setup, GPIO, keypad, encoder, ADC, DAC, and GPS UART |
| [Source/radio.c](Source/radio.c) | BK4829 register access, RF routing, and voice squelch |
| [Source/display.c](Source/display.c) | ST7789 panel initialization, drawing, and fonts |
| [Source/services.c](Source/services.c) | GPS parsing, Si4732 tuner, and SPI flash storage |
| [Source/packet.c](Source/packet.c) | APRS modulation, demodulation, and AX.25 frames |
| [Device/Source/system_at32a403a.c](Device/Source/system_at32a403a.c) | Startup and interrupt vector relocation |
| [BK4829.Registers.Table.pdf](BK4829.Registers.Table.pdf) | BK4829 register reference |

The MCU is configured for **200 MHz**. The application starts at `0x08003000`. Keep the two bootloader signatures at **`0x080033E0`** and **`0x080037E0`**. Startup copies the interrupt vector table into the reserved SRAM at `0x20000000`. The linked RAM region starts at `0x20000800`.

Settings are stored in four 4 KiB external SPI flash sectors at `0x0C0000` through `0x0C3FFF`. The firmware rotates saves and checks each record before using it.

## License

The project's original source code is licensed under the GNU General Public License version 2 only (`GPL-2.0-only`). Bundled third-party components retain their existing license terms and notices. [Full license text](LICENSE)
