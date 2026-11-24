# MG21 SWD tools

The S3 runs these probes from RAM through its CP2102 UART. GPIO16 connects to
MG21 SWCLK and GPIO20 to SWDIO. Each hardware invocation reboots the S3 into
its existing application on exit.

Build from the project root with the Debian xtensa-lx106 C compiler and
binutils on PATH, or extract the two Debian packages under
`.scratch/backup-xtensa`. The build writes S3 RAM images and their entry
addresses to `.scratch/`.

```sh
radio/tools/backup/build.sh
mkdir -p backups
uv run --no-project --with 'esptool==5.6.0' python radio/tools/backup/read_flash.py backups/mg21-main.bin
uv run --no-project --with 'esptool==5.6.0' python radio/tools/backup/run_sram.py --peek 0xe000ed08
uv run --no-project --with 'esptool==5.6.0' python radio/tools/backup/run_sram.py --image .scratch/firmware-build/uart_probe.bin
uv run --no-project --with 'esptool==5.6.0' python radio/tools/backup/run_sram.py --reset
```

The backup checks SWD parity on each word and CRC32 on each serial block. It
compares two complete reads before publishing a new owner-readable file. The
SRAM launcher verifies each downloaded word before setting the core registers
and running the image. The next MG21 reset restores its normal boot path.

The dedicated installer accepts flash-linked images for the 64 KiB RAM,
768 KiB flash MG21A020 part. Its default invocation reads the full main flash
and checks the saved factory backup against the erased pattern. The flash
image must start at zero and have a multiple of four bytes. Add any necessary
erased padding while generating the final image.

```sh
uv run --no-project --with 'esptool==5.6.0' python radio/tools/backup/install_blank.py .scratch/firmware-build/radio_flash.bin
```

Passing `--program` to this command enables writes. The program compares each
word with the desired value, refuses to change an already-programmed word,
and writes the reset vectors last. It then re-reads the complete flash in a
fresh debugger session, compares the loaded image and erased remainder, and
restarts the radio. After an interrupted install, rerun with the same image.

The Series 2 implementation follows the write sequence from Silicon Labs
emlib. [MSC_WriteWord and writeBurst](https://github.com/SiliconLabs/gecko_sdk/blob/8a2efd2cb191cb250fd4c1e655b16b8a80be1997/platform/emlib/src/em_msc.c)
