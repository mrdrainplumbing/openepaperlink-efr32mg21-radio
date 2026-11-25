# OpenEPaperLink radio for the NZ-M21

Use a Silicon Labs EFR32MG21 module as the 2.4 GHz IEEE 802.15.4 radio for an ESP32-S3 running OpenEPaperLink. The S3 hosts Wi-Fi, the dashboard and tag content. The MG21 exchanges images and check-ins with tags over the air, and talks to the S3 over a 115200-baud UART.

The tested combination is an **ESP32-S3-DevKitC-N16R8**, **Yellow AP 2.91**, and an **NZ-M21 with EFR32MG21A020F768IM32**, 768 KiB flash and 64 KiB SRAM. A Hanshow Stellar M3N running compatible tag-side firmware displayed an image through this radio. Provision the S3 and tags separately. [OpenEPaperLink project](https://github.com/OpenEPaperLink/OpenEPaperLink)

## Radio

Bought off Taobao
<img width="848" height="683" alt="image" src="https://github.com/user-attachments/assets/7f0d4a00-b73a-44bd-a355-b9ea280fa652" />


## Wiring

With power disconnected, orient the NZ-M21 with its antenna at the top. The left-edge pads count down from 1. Use the S3's **USB-to-UART** connector for the CP2102 connection to Linux. The S3 temporarily acts as the SWD programmer through GPIO16 and GPIO20.

| NZ-M21 pad | ESP32-S3 pin | Signal |
|---|---|---|
| Left, 4th down · pad 4, PB01 | GPIO18 | Radio TX → S3 RX |
| Left, 5th down · pad 5, PB00 | GPIO17 | Radio RX ← S3 TX |
| Left, 7th down · pad 7, PA01 | GPIO16 | SWD clock |
| Left, 8th down · pad 8, PA02 | GPIO20 | SWD data |
| Bottom-left · pad 10, VDD | 3V3 | 3.3 V power |
| Bottom-right · pad 21, GND | GND | Common ground |

The tested module schematic places RESET on pad 30 and marks pad 29 NC. The tools reset the MG21 through SWD. Leave the reset lead unwired.

## First installation on a blank MG21

Run these commands from the project root on Linux. Install `just`, `uv`, and the GNU Xtensa LX106 cross-compiler and binutils for the S3 RAM probes. The probes run temporarily on the S3 and reboot it into its installed AP firmware afterward.

1. Connect through USB-to-UART. Set `MG21_PORT` to the S3 serial device. Build the probes and save a private backup. This reads the complete 768 KiB twice and requires the results to match.

   ```sh
   export MG21_PORT=/dev/ttyUSB0
   just build-probes
   mkdir -p backups
   just backup backups/mg21-main.bin
   ```

2. Check the published image checksum and run the installer's preflight. It requires a backup of erased flash and accepts a target containing erased words or matching words from the same image. Keep the original backup if the module already contains firmware.

   ```sh
   image=radio/releases/mg21-oepl-v1.bin
   echo "a179e4b7b08ab0e737a9f7f6cab8945b8fc8d7feee102536c99335f6004f95f0  $image" | sha256sum -c -
   uv run --no-project --with esptool==5.6.0 python radio/tools/backup/install_blank.py --port "$MG21_PORT" "$image"
   ```

3. After preflight succeeds, program the radio. The installer writes the reset vectors last, verifies all 768 KiB in a fresh debug session, then resets the MG21. For an interrupted installation, rerun with the exact same image and original backup.

   ```sh
   uv run --no-project --with esptool==5.6.0 python radio/tools/backup/install_blank.py --port "$MG21_PORT" --program "$image"
   ```

4. Open the S3 dashboard and check that the radio comes online at version 1. Yellow AP 2.91 can label the radio “ESP32-C6” because of its build profile. A tag check-in confirms over-the-air communication. A visibly changed panel confirms the display update. [S3 probe and installer details](radio/tools/backup/README.md)

## Build and test

Run `just test` with a host C compiler to exercise tag discovery, 20 queue slots and a 4,096-byte transfer. For the device build, use GNU Arm Embedded GCC 14.2.1, Gecko SDK v4.5.1 and its xG21 RAIL release archive. Set `GSDK` and `RAIL_ARCHIVE` as described in the build guide. [Firmware build guide](radio/firmware/README.md)

```sh
just test
just build-radio ram
just build-radio flash
```

The outputs are `.scratch/firmware-build/radio_ram.bin` and `.scratch/firmware-build/radio_flash.bin`. To try the volatile image with the same wiring, run `just run-sram .scratch/firmware-build/radio_ram.bin`. An MG21 reset returns to its flash image.

## Validation and development

Codex, OpenAI’s AI coding agent, was used to write and adapt the firmware and tools. Host tests exercised the queue and block-transfer logic. On the bench, the AP recognized the SRAM build, and a physical Stellar M3N displayed an uploaded image. The flash build passed full-chip readback. After an MG21 system reset, the vector table pointed at flash, Yellow AP reported radio version 1 online, and a tag checked in. Physical validation covers the hardware and firmware versions listed above. The 20-slot result comes from the host test.

## Licensing

mrdrainplumbing's original contributions are available under MIT. The adapted OpenEPaperLink protocol is CC BY-NC-SA 4.0. The compiled image incorporates Silicon Labs RAIL and other third-party code. See the component notices for redistribution terms. [MIT license](LICENSE) · [Third-party notices](THIRD-PARTY.md)

For a public release, package the project sources, release image and license notices from version control. Local `backups/` and `.scratch/` can contain device identifiers, captured packets and tag images.
