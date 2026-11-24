# Commands that talk to the S3 use its CP2102 UART port.
port := env("MG21_PORT", "/dev/ttyUSB0")
python := "uv run --no-project --with esptool==5.6.0 python"

default:
    @just --list

build-probes:
    radio/tools/backup/build.sh

read-word address:
    {{python}} radio/tools/backup/run_sram.py --port {{quote(port)}} --peek {{quote(address)}}

backup path:
    {{python}} radio/tools/backup/read_flash.py --port {{quote(port)}} --baud 460800 {{quote(path)}}

run-sram image:
    {{python}} radio/tools/backup/run_sram.py --port {{quote(port)}} --image {{quote(image)}}

test:
    mkdir -p .scratch
    cc -std=c11 -Wall -Wextra -Werror -O1 -fsanitize=address,undefined radio/oepl/oepl.c radio/oepl/tests/transfer.c -o .scratch/test-oepl
    .scratch/test-oepl

build-radio mode="ram":
    radio/firmware/build-radio.sh {{quote(mode)}}
