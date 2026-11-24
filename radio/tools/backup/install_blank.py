"""Check or install a flash-linked MG21A020F768 image on the verified blank radio."""

import argparse
import binascii
import hashlib
import io
import struct
from pathlib import Path
from types import SimpleNamespace

import esptool

from read_flash import ROOT, read_exact, read_pass, upload
from run_sram import dispatch

FLASH_SIZE = 768 * 1024
BLANK = b"\xff" * FLASH_SIZE


def validate_image(data):
    if not 8 <= len(data) <= FLASH_SIZE or len(data) % 4:
        raise ValueError("expected a word-aligned image from 8 bytes through 768 KiB")
    sp, pc = struct.unpack_from("<II", data)
    if not (
        0x20000000 < sp <= 0x20010000 and sp % 4 == 0 and pc & 1 and 8 <= pc < len(data)
    ):
        raise ValueError(
            f"expected vectors linked for flash at zero, got {sp:#x} {pc:#x}"
        )


def assess_live(data, live):
    """Accept only all-FF words or exact matches with the candidate."""
    if len(live) != FLASH_SIZE:
        raise ValueError("incomplete flash preflight")
    if live[len(data) :] != BLANK[len(data) :]:
        raise ValueError("nonblank data after candidate image")
    for i in range(0, len(data), 4):
        word = live[i : i + 4]
        if word != b"\xff" * 4 and word != data[i : i + 4]:
            raise ValueError(f"target contains different data at {i:#x}")


def snapshot(port, baud):
    output = io.BytesIO()
    read_pass(
        SimpleNamespace(
            port=port, baud=baud, start=0, size=FLASH_SIZE, ap=0, csw=0x22000012
        ),
        output,
    )
    return output.getvalue()


def write_image(port_name, data):
    rom = esptool.detect_chip(port_name)
    try:
        if rom.CHIP_NAME != "ESP32-S3":
            raise RuntimeError(f"unexpected S3: {rom.CHIP_NAME}")
        scratch = ROOT / ".scratch"
        upload(rom, 0x40378000, (scratch / "program.bin").read_bytes())
        upload(rom, 0x3FCA0000, struct.pack("<II", len(data), 0x21BB004E))
        rom.mem_finish(int((scratch / "program.entry").read_text(), 16))
        port = rom._port
        port.timeout = 20
        port.write(b"K")
        offset = 8
        vectors = False
        while True:
            header = read_exact(port, 4)
            if header == b"Bad!":
                cause, addr = struct.unpack("<II", read_exact(port, 8))
                raise RuntimeError(f"MSC probe failed: {cause:#x}, detail {addr:#x}")
            if header == b"END!" and vectors:
                break
            if header != b"PGM!":
                raise RuntimeError(f"invalid MSC response {header!r}")
            address, count = struct.unpack("<II", read_exact(port, 8))
            if offset < len(data):
                expected = min(1024 - (offset & 1023), len(data) - offset)
                if (address, count) != (offset, expected):
                    raise RuntimeError("invalid payload request")
                offset += count
            elif not vectors and (address, count) == (0, 8):
                vectors = True
            else:
                raise RuntimeError("out-of-order vector request")
            block = data[address : address + count]
            port.write(block + struct.pack("<I", binascii.crc32(block)))
    finally:
        rom.hard_reset()
        rom._port.close()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("image", type=Path)
    p.add_argument("--baseline", type=Path, default=ROOT / "backups/mg21-main.bin")
    p.add_argument("--port", default="/dev/ttyUSB0")
    p.add_argument("--read-baud", type=int, default=460800)
    p.add_argument(
        "--program", action="store_true", help="enable programming after preflight"
    )
    args = p.parse_args()
    data = args.image.read_bytes()
    validate_image(data)
    if args.baseline.read_bytes() != BLANK:
        raise ValueError("baseline is not a blank 768 KiB backup")
    digest = hashlib.sha256(data).hexdigest()
    print(f"{args.image}: {len(data)} bytes, sha256={digest}", flush=True)
    live = snapshot(args.port, args.read_baud)
    assess_live(data, live)
    if not args.program:
        print("Preflight passed. No persistent write requested.")
        return
    if live[: len(data)] != data:
        write_image(args.port, data)
    verified = snapshot(args.port, args.read_baud)
    if verified != data + BLANK[len(data) :]:
        raise RuntimeError(
            "independent full-chip readback mismatch; MG21 has not been restarted"
        )
    print(
        f"Verified complete flash against SHA-256 {digest} and erased remainder.",
        flush=True,
    )
    dispatch(SimpleNamespace(port=args.port, peek=None), 2)


if __name__ == "__main__":
    main()
