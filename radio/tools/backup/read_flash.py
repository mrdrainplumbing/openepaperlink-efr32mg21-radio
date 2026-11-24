"""Read and verify MG21 data through an ESP32-S3 RAM-resident SWD probe."""

import argparse
import binascii
import hashlib
import os
import struct
import tempfile
from pathlib import Path

import esptool

ROOT = Path(__file__).resolve().parents[3]


def num(text):
    return int(text, 0)


def upload(rom, addr, data):
    data += b"\x00" * (-len(data) % 4)
    rom._upload_segment(data, addr)


def read_exact(port, length):
    result = port.read(length)
    if len(result) != length:
        raise TimeoutError(f"received {len(result)}/{length} serial bytes")
    return result


def read_pass(args, output):
    rom = esptool.detect_chip(args.port)
    try:
        if rom.CHIP_NAME != "ESP32-S3":
            raise RuntimeError(f"unexpected ESP: {rom.CHIP_NAME}")
        if args.baud != 115200:
            rom.change_baud(args.baud)
        upload(rom, 0x40378000, (ROOT / ".scratch/probe.bin").read_bytes())
        upload(
            rom,
            0x3FCA0000,
            struct.pack("<4I", args.start, args.size, args.csw, args.ap),
        )
        entry = int((ROOT / ".scratch/probe.entry").read_text(), 16)
        rom.mem_finish(entry)
        port = rom._port
        port.timeout = 5
        port.write(b"K")
        while True:
            marker = read_exact(port, 4)
            if marker == b"Bad!":
                req, detail = struct.unpack("<II", read_exact(port, 8))
                raise RuntimeError(
                    f"probe failed on request 0x{req:x}, detail 0x{detail:x}"
                )
            if marker == b"END!":
                if output.tell() != args.size:
                    raise RuntimeError("early stream termination")
                break
            if marker != b"BLK!":
                raise RuntimeError(f"invalid marker: {marker!r}")
            address, length = struct.unpack("<II", read_exact(port, 8))
            if address != args.start + output.tell() or length != min(
                args.size - output.tell(), 1024 - (address & 1023)
            ):
                raise RuntimeError(f"invalid block: {address:#x}, {length}")
            block = read_exact(port, length)
            (crc,) = struct.unpack("<I", read_exact(port, 4))
            if crc != binascii.crc32(block):
                raise RuntimeError(f"CRC mismatch at {address:#x}")
            output.write(block)
            if output.tell() % 65536 == 0:
                print(f"received {output.tell()}/{args.size}", flush=True)
    finally:
        rom.hard_reset()
        rom._port.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--port", default="/dev/ttyUSB0")
    parser.add_argument("--start", type=num, default=0)
    parser.add_argument("--size", type=num, default=768 * 1024)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--csw", type=num, default=0x22000012)
    parser.add_argument("--ap", type=num, default=0)
    args = parser.parse_args()
    if (
        args.size <= 0
        or (args.size | args.start) & 3
        or not 0 <= args.start <= 0xFFFFFFFF - args.size + 1
    ):
        parser.error(
            "address and size must be word aligned and in the 32-bit address space"
        )
    if args.output.exists():
        parser.error("output already exists")
    with tempfile.TemporaryDirectory(dir=args.output.parent) as directory:
        tmp = Path(directory)
        for index in range(2):
            path = tmp / str(index)
            with path.open("wb") as file:
                os.fchmod(file.fileno(), 0o600)
                read_pass(args, file)
        a, b = (tmp / "0").read_bytes(), (tmp / "1").read_bytes()
        if a != b:
            raise RuntimeError("independent passes differ")
        os.link(tmp / "0", args.output)
        print(
            f"verified {len(a)} bytes sha256={hashlib.sha256(a).hexdigest()}, non-FF bytes={sum(n != 255 for n in a)}"
        )
