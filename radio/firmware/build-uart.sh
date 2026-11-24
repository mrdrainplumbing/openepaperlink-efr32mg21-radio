#!/bin/sh
set -eu
cd -P "$(dirname "$0")"
: "${GSDK:?Set GSDK to a Gecko SDK 4.5 checkout}"
cross=${CROSS_COMPILE:-arm-none-eabi-}
out=${BUILD_DIR:-../../.scratch/firmware-build}
mkdir -p "$out"
"${cross}gcc" -mcpu=cortex-m33 -mthumb -Os -g -std=gnu11 -ffreestanding \
    -Wall -Wextra -Werror -fno-builtin -fno-use-linker-plugin -nostdlib -DSL_TRUSTZONE_SECURE \
    -I"$GSDK/platform/Device/SiliconLabs/EFR32MG21/Include" \
    -I"$GSDK/platform/CMSIS/Core/Include" \
    -Wl,-T,uart_probe.ld,--build-id=none,-Map,"$out/uart_probe.map" \
    -o "$out/uart_probe.elf" uart_probe.c board_uart.c
"${cross}objcopy" -O binary "$out/uart_probe.elf" "$out/uart_probe.bin"
"${cross}size" "$out/uart_probe.elf"
