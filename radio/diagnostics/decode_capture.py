"""Decode a finished RCF1 MG21 capture to JSON and optionally Wireshark pcapng."""
import argparse
import json
from pathlib import Path
import struct
import sys


HEADER = struct.Struct("<8I")
RECORD = struct.Struct("<IBbBB127sB")
CHANNELS = range(11, 27)
SWEEPS = 3
SLOTS = 3
BUCKETS = SWEEPS * len(CHANNELS)
SIZE = HEADER.size + 4 * BUCKETS + BUCKETS * SLOTS * RECORD.size
FRAME_TYPES = ("beacon", "data", "ack", "command", "reserved", "multipurpose", "fragment", "extended")


def crc16(psdu):
    crc = 0
    for byte in psdu:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0x8408 if crc & 1 else 0)
    return crc


class Cursor:
    def __init__(self, data):
        self.data = data
        self.offset = 0

    def take(self, length):
        if self.offset + length > len(self.data):
            raise ValueError("truncated frame fields")
        value = self.data[self.offset:self.offset + length]
        self.offset += length
        return value

    def number(self, size=1):
        return int.from_bytes(self.take(size), "little")


def pan_fields(version, dest, src, compressed):
    if version < 2:
        if compressed and not (src and dest):
            raise ValueError("invalid legacy PAN compression")
        return bool(dest), bool(src and not compressed)
    if not dest and not src:
        return compressed, False
    if not dest:
        return False, not compressed
    if not src or dest == src == 3:
        return not compressed, False
    return True, not compressed


def decode_beacon(cursor):
    superframe = cursor.number(2)
    gts_count = cursor.number() & 7
    if gts_count:
        cursor.take(1 + 3 * gts_count)
    pending = cursor.number()
    short_count, long_count = pending & 7, (pending >> 4) & 7
    cursor.take(short_count * 2 + long_count * 8)
    payload = cursor.data[cursor.offset:]
    info = {
        "superframe_spec": f"0x{superframe:04x}", "beacon_order": superframe & 15,
        "superframe_order": (superframe >> 4) & 15, "pan_coordinator": bool(superframe & 0x4000),
        "association_permit": bool(superframe & 0x8000), "gts_count": gts_count,
        "pending_short_count": short_count, "pending_extended_count": long_count,
        "payload_hex": payload.hex(),
    }
    if len(payload) == 15 and payload[0] == 0 and payload[1] >> 4 == 2 and payload[2] & 3 == 0:
        info["zigbee"] = {
            "protocol_id": payload[0], "protocol_version": payload[1] >> 4, "stack_profile": payload[1] & 15,
            "router_capacity": bool(payload[2] & 4), "device_depth": (payload[2] >> 3) & 15,
            "end_device_capacity": bool(payload[2] & 0x80), "extended_pan_id": payload[3:11][::-1].hex(":"),
            "tx_offset": int.from_bytes(payload[11:14], "little"), "update_id": payload[14],
        }
    return info


def decode_mac(psdu):
    mac = {}
    cursor = Cursor(psdu[:-2])
    try:
        fcf = cursor.number(2)
        frame_type = fcf & 7
        version = (fcf >> 12) & 3
        dest, src = (fcf >> 10) & 3, (fcf >> 14) & 3
        compressed = bool(fcf & 0x40)
        mac.update(frame_control=f"0x{fcf:04x}", frame_type=FRAME_TYPES[frame_type], frame_version=version,
                   security=bool(fcf & 8), ack_request=bool(fcf & 32), pan_compression=compressed,
                   destination_address_mode=dest, source_address_mode=src)
        if frame_type > 3 or version == 3 or dest == 1 or src == 1:
            raise ValueError("unsupported frame format")
        if version < 2 or not fcf & 0x100:
            mac["sequence"] = cursor.number()
        dest_pan, src_pan = pan_fields(version, dest, src, compressed)
        for prefix, mode, pan in (("destination", dest, dest_pan), ("source", src, src_pan)):
            if pan:
                mac[prefix + "_pan"] = f"0x{cursor.number(2):04x}"
            if mode == 2:
                mac[prefix] = f"0x{cursor.number(2):04x}"
            elif mode == 3:
                mac[prefix] = cursor.take(8)[::-1].hex(":")
        if dest and src and dest_pan and not src_pan and compressed:
            mac["source_pan_inferred"] = mac["destination_pan"]
        if not mac["security"] and version < 2:
            if frame_type == 0:
                mac["beacon"] = decode_beacon(cursor)
            elif frame_type == 3:
                mac["command_id"] = cursor.number()
        if frame_type == 1 and not mac["security"] and not fcf & 0x200:
            mac["payload_hex"] = cursor.data[cursor.offset:].hex()
    except ValueError as exc:
        mac["decode_error"] = str(exc)
    return mac


def decode(blob):
    if len(blob) != SIZE:
        raise ValueError(f"expected {SIZE} capture bytes, got {len(blob)}")
    magic, version, finished, sweep, tuned, errors, tx, stride = HEADER.unpack_from(blob)
    if magic != 0x31464352 or version not in (1, 2) or stride != RECORD.size or finished != 1:
        raise ValueError("expected a finished version 1 or 2 RCF1 capture with 136-byte records")
    counts = struct.unpack_from(f"<{BUCKETS}I", blob, HEADER.size)
    frames = []
    buckets = []
    records_start = HEADER.size + 4 * BUCKETS
    for idx, count in enumerate(counts):
        sweep_idx, channel_idx = divmod(idx, len(CHANNELS))
        channel = CHANNELS[channel_idx]
        if version == 1:
            first = idx * SLOTS
            kept = min(count, SLOTS)
        else:
            first = len(frames)
            kept = min(count, BUCKETS * SLOTS - first)
        buckets.append({"sweep": sweep_idx, "channel": channel, "received": count, "stored": kept})
        for slot in range(kept):
            offset = records_start + RECORD.size * (first + slot)
            timestamp, ch, rssi, lqi, length, data, flags = RECORD.unpack_from(blob, offset)
            if ch != channel or not 2 <= length <= len(data):
                raise ValueError(f"invalid record in sweep {sweep_idx}, channel {channel}, slot {slot}")
            psdu = data[:length]
            recorded_fcs = int.from_bytes(psdu[-2:], "little")
            frames.append({
                "sweep": sweep_idx, "slot": slot, "channel": ch, "timestamp_us": timestamp,
                "timestamp_source": "frame_end" if flags & 1 else "poll", "flags": flags,
                "rssi_dbm": rssi, "lqi": lqi, "length": length, "psdu_hex": psdu.hex(),
                "fcs": f"0x{recorded_fcs:04x}", "fcs_valid": crc16(psdu[:-2]) == recorded_fcs,
                "mac": decode_mac(psdu),
            })
    return {
        "format": "RCF1", "version": version, "last_sweep": sweep, "last_channel": tuned,
        "timestamp_origin": "device counter, microseconds; no wall clock synchronization",
        "tune_errors": errors, "accepted_tx_requests": tx, "received": sum(counts), "stored": len(frames),
        "dropped": sum(counts) - len(frames), "buckets": buckets, "frames": frames,
    }


def aligned(data):
    return data + bytes((-len(data)) % 4)


def option(code, data):
    return struct.pack("<HH", code, len(data)) + aligned(data)


def block(kind, payload):
    size = len(payload) + 12
    return struct.pack("<II", kind, size) + payload + struct.pack("<I", size)


def pcapng(capture):
    end = option(0, b"")
    note = capture["timestamp_origin"].encode()
    yield block(0x0A0D0D0A, struct.pack("<IHHq", 0x1A2B3C4D, 1, 0, -1) + option(1, note) + end)
    for channel in CHANNELS:
        # LINKTYPE_IEEE802_15_4_WITHFCS (195). Default timestamp resolution is microseconds.
        interface = f"MG21 802.15.4 channel {channel}".encode()
        yield block(1, struct.pack("<HHI", 195, 0, 127) + option(2, interface) + end)
    for frame in capture["frames"]:
        psdu = bytes.fromhex(frame["psdu_hex"])
        timestamp = frame["timestamp_us"]
        fields = struct.pack("<IIIII", frame["channel"] - 11, 0, timestamp, len(psdu), len(psdu))
        note = "sweep={sweep} rssi={rssi_dbm}dBm lqi={lqi} timestamp={timestamp_source}".format(**frame)
        yield block(6, fields + aligned(psdu) + option(1, note.encode()) + end)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("--json", type=Path, help="JSON destination (defaults to stdout)")
    parser.add_argument("--pcapng", type=Path, help="additional Wireshark output with one interface per channel")
    args = parser.parse_args()
    paths = [p.resolve() for p in (args.capture, args.json, args.pcapng) if p]
    if len(paths) != len(set(paths)):
        parser.error("input and output paths must be distinct")
    try:
        capture = decode(args.capture.read_bytes())
        report = json.dumps(capture, indent=2) + "\n"
        if args.json:
            args.json.write_text(report)
        else:
            sys.stdout.write(report)
        if args.pcapng:
            with args.pcapng.open("wb") as dest:
                dest.writelines(pcapng(capture))
    except (OSError, ValueError) as exc:
        parser.exit(1, f"{parser.prog}: {exc}\n")


if __name__ == "__main__":
    main()
