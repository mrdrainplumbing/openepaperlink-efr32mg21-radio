#!/bin/sh
set -eu
cd -P "$(dirname "$0")"
: "${GSDK:?Set GSDK to the Gecko SDK v4.5.1 checkout}"
: "${RAIL_ARCHIVE:?Set RAIL_ARCHIVE to the xG21 single-protocol GCC archive}"
cross=${CROSS_COMPILE:-arm-none-eabi-}
out=${BUILD_DIR:-../../.scratch/firmware-build}
rail=$GSDK/platform/radio/rail_lib
mode=${1:-ram}
case "$mode" in
    ram) extra=-Wl,--defsym,RAM_ONLY=1 ;;
    flash) extra= ;;
    *) exit 2 ;;
esac
mkdir -p "$out"
"${cross}gcc" -mcpu=cortex-m33 -mthumb -mcmse -mfpu=fpv5-sp-d16 -mfloat-abi=hard \
   ${RADIO_CPPFLAGS:-} -Os -g -ffreestanding -fshort-enums -fno-builtin -ffunction-sections -fdata-sections \
   -nostdlib -fno-use-linker-plugin -Wall -Wextra -DEFR32MG21A020F768IM32 \
   -I"$GSDK/platform/Device/SiliconLabs/EFR32MG21/Include" \
   -I"$GSDK/platform/emlib/inc" -I"$GSDK/platform/common/inc" \
   -I"$GSDK/platform/CMSIS/Core/Include" -I"$rail/common" \
   -I"$rail/chip/efr32/efr32xg2x" -I"$rail/protocol/ieee802154" -I../oepl \
   -o "$out/radio_$mode.elf" $extra \
   -Wl,-T,radio.ld,--gc-sections,--build-id=none,-Map,"$out/radio_$mode.map" \
   "$GSDK/platform/emlib/src/em_core.c" "$GSDK/platform/emlib/src/em_system.c" \
   "$GSDK/platform/Device/SiliconLabs/EFR32MG21/Source/system_efr32mg21.c" \
   radio_main.c board_uart.c radio.c runtime.c "${OEPL_SOURCE:-../oepl/oepl.c}" "$RAIL_ARCHIVE" \
   -lgcc
"${cross}objcopy" -O binary "$out/radio_$mode.elf" "$out/radio_$mode.bin"
"${cross}size" "$out/radio_$mode.elf"
