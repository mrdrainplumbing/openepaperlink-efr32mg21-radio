#!/bin/sh
set -eu
cd -P "$(dirname "$0")"
scratch="$(cd ../../.. && pwd)/.scratch"
mkdir -p "$scratch"
export PATH="$scratch/backup-xtensa/usr/bin:$PATH"
for image in probe launch program; do
    xtensa-lx106-elf-gcc -Os -ffreestanding -fno-builtin -nostdlib -Wl,-T,probe.ld -o "$scratch/$image.elf" entry.S "$image.c"
    xtensa-lx106-elf-objcopy -O binary "$scratch/$image.elf" "$scratch/$image.bin"
    xtensa-lx106-elf-nm "$scratch/$image.elf" | awk '$3=="start" {print $1}' > "$scratch/$image.entry"
done
