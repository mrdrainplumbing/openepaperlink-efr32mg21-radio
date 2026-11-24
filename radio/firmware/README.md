# NZ-M21 MG21 firmware

The target is EFR32MG21A020F768IM32 with 768 KiB of flash and 64 KiB of SRAM. PB00 is USART0 RX, PB01 is USART0 TX. The serial link uses 115200 baud, 8N1. The default OEPL RF channel is 25. The radio uses Silicon Labs' built-in 2.4 GHz IEEE 802.15.4 PHY, which requires a 38.4 MHz HFXO on xG21. The program waits for HFXO readiness at startup. A ready indication does not measure absolute frequency or board-specific trim. It leaves the oscillator capacitor settings as found.

## Build

Use GNU Arm Embedded GCC (tested with 14.2.1), its Cortex-M33 hard-float libgcc multilib, Arm binutils, newlib headers and Gecko SDK v4.5.1 (commit `8a2efd2cb191cb250fd4c1e655b16b8a80be1997`). The RAIL library is supplied by Silicon Labs. Its Git repository entry uses Git LFS. Use the actual library from the SDK release archive or Git LFS checkout.

```sh
export GSDK=/absolute/path/to/gecko_sdk
export RAIL_ARCHIVE="$GSDK/platform/radio/rail_lib/autogen/librail_release/librail_efr32xg21_gcc_release.a"
# Expected SHA256: 7c49366a5d7d9b18e00d64dbad39dd5cc6517daf3f796b17ed6a16b7877ce525
./build-radio.sh ram
./build-radio.sh flash
./build-uart.sh
```

The default build directory is `../../.scratch/firmware-build`. Run these commands in this directory. The build scripts do not program any hardware. The RAM build links the vector table, executable and globals into SRAM and initializes globals when entered. Its load address is 0x20000000. Use the first two vector words from that exact image for the initial stack and Thumb program counter. The flash version links executable code at 0 and globals into SRAM. Always check the target's actual flash contents and bootloader layout before considering the flash artifact.

The UART-only build prints `MG21-UART` on start, responds to `RDY?` and reports a diagnostic type of `FE` to `NFO?`. Its version return verifies the programming and UART paths only.

## RF diagnostics

Build the radio scan into a separate output directory. This builds the same radio driver and includes standard beacon and MAC command frames in its receive filter.

```sh
BUILD_DIR=../../.scratch/scan OEPL_SOURCE=../diagnostics/rf_scan.c RADIO_CPPFLAGS=-DMG21_RF_SCAN ./build-radio.sh ram
```

Look up the addresses for each linked image before inspecting SRAM:

```sh
arm-none-eabi-nm -n ../../.scratch/scan/radio_ram.elf | grep -E 'radio_(stage|rx_count|tx_count|tx_errors|channel)$'
```

Stage 2 is the HFXO wait, 10 is entry into RAIL init, 11 is FIFO and calibrations, 12 configures the MAC and PHY, 13 sets the PA and radio transitions, 14 starts receive, and 15 means RAIL reports RX state. A value starting with 0xE000 identifies the failing stage by its low byte. 0xFA17xxxx indicates a processor exception, and 0xAA0000xx is a RAIL assertion. The RX count increments only for successfully received frames with consistent length, and TX count only after a packet-sent event. A TX count alone cannot prove reception at another device.

[Gecko SDK v4.5.1](https://github.com/SiliconLabs/gecko_sdk/releases/tag/v4.5.1) · [xG21 reference manual](https://www.silabs.com/documents/public/reference-manuals/efr32xg21-rm.pdf)
