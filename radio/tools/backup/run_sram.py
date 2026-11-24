"""Launch a Cortex-M image in MG21 SRAM, read a memory word, or reset to flash."""

import argparse
import struct
from pathlib import Path

import esptool

from read_flash import ROOT, num, read_exact, upload


def dispatch(args, mode):
    rom = esptool.detect_chip(args.port)
    try:
        if rom.CHIP_NAME != "ESP32-S3":
            raise RuntimeError(f"unexpected S3: {rom.CHIP_NAME}")
        scratch = ROOT / ".scratch"
        upload(rom, 0x40378000, (scratch / "launch.bin").read_bytes())
        data = b""
        if mode == 1:
            data = args.image.read_bytes()
            if not 8 <= len(data) <= 65536:
                raise ValueError("expected MG21 SRAM image 8..65536 bytes")
            data += b"\x00" * (-len(data) % 4)
            sp, pc = struct.unpack_from("<II", data)
            if not (
                0x20000000 < sp <= 0x20010000
                and sp % 4 == 0
                and pc & 1
                and 0x20000000 <= pc < 0x20000000 + len(data)
            ):
                raise ValueError(f"invalid SRAM vectors SP={sp:#x} PC={pc:#x}")
            upload(rom, 0x3FCA5000, data)
        upload(rom, 0x3FCA0000, struct.pack("<III", len(data), mode, args.peek or 0))
        rom.mem_finish(int((scratch / "launch.entry").read_text(), 16))
        port = rom._port
        port.timeout = 15
        port.write(b"K")
        header = read_exact(port, 4)
        if header == b"Bad!":
            req, value = struct.unpack("<II", read_exact(port, 8))
            raise RuntimeError(f"probe failed at {req:#010x}, detail {value:#010x}")
        if mode == 1 and header == b"RUN!":
            dhcsr, vtor = struct.unpack("<II", read_exact(port, 8))
            print(
                f"readback verified {len(data)} SRAM bytes; DHCSR={dhcsr:#010x}, VTOR={vtor:#010x}"
            )
            if dhcsr & ((1 << 17) | (1 << 19)):
                raise RuntimeError("target reports halt or lockup after resume")
        elif mode == 2 and header == b"END!":
            print("MG21 debug halt disabled, system reset requested")
        elif mode == 3 and header == b"REG!":
            print(
                f"{args.peek:#010x}: {struct.unpack('<I', read_exact(port, 4))[0]:#010x}"
            )
        else:
            raise RuntimeError(f"unexpected response {header!r}")
    finally:
        rom.hard_reset()
        rom._port.close()


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    op = p.add_mutually_exclusive_group(required=True)
    op.add_argument("--image", type=Path)
    op.add_argument("--reset", action="store_true")
    op.add_argument("--peek", type=num)
    p.add_argument("--port", default="/dev/ttyUSB0")
    args = p.parse_args()
    mode = 1 if args.image else (2 if args.reset else 3)
    if mode == 3 and (args.peek & 3 or not 0 <= args.peek <= 0xFFFFFFFC):
        p.error("--peek needs aligned uint32 address")
    try:
        dispatch(args, mode)
    except (RuntimeError, TimeoutError) as exc:
        if mode == 1:
            print(f"launch failed ({exc}); attempting recovery reset")
            dispatch(args, 2)
        raise
