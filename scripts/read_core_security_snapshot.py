#!/usr/bin/env python3
"""Decode the bounded, metadata-only audit file locally. No console access."""
import argparse
import json
from pathlib import Path
import struct

SIZE = 192

def checksum(data):
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xffffffff
    return value

def decode(data):
    if len(data) != SIZE:
        raise ValueError("Wrong exact record size")
    magic, version, begin, end, opened, query, validation, reserved = struct.unpack_from("<IIQQIIII", data)
    recorded_hash, reserved1 = struct.unpack_from("<II", data, 184)
    if (magic, version) != (0x41525341, 1) or reserved or reserved1 or recorded_hash != checksum(data[:184]):
        raise ValueError("Invalid header/checksum/reserved bytes")
    wire_magic, wire_version, count, overflow = struct.unpack_from("<IIII", data, 40)
    if (wire_magic, wire_version) != (0x41525345, 1) or count > 8 or (overflow and count != 8):
        raise ValueError("Invalid metadata wire")
    events = []
    for i in range(8):
        time, kind, detail = struct.unpack_from("<QII", data, 56 + 16 * i)
        if i >= count:
            if time or kind or detail:
                raise ValueError("Nonempty unused entry")
            continue
        if not time or kind not in (2, 3, 4) or (kind == 2 and detail) or (kind == 3 and detail > 255):
            raise ValueError("Invalid event")
        if events and time < events[-1]["boot_ms"]:
            raise ValueError("Unordered timestamps")
        events.append({"boot_ms": time, "kind": {2: "PIN_request", 3: "SSP_request", 4: "raw_link_subtype"}[kind], "detail": detail})
    eligible = opened == query == validation == 0
    if eligible:
        if not begin or end < begin or end - begin > 2000 or any(e["boot_ms"] > end for e in events):
            raise ValueError("Invalid collection clock bounds")
    elif count or overflow:
        raise ValueError("Failed getter contains unsanitized evidence")
    return {
        "format_valid": True, "collection_results": {"open": f"0x{opened:08x}", "getter": f"0x{query:08x}", "validation": f"0x{validation:08x}"},
        "collection_begin_boot_ms": begin, "collection_end_boot_ms": end,
        "eligible_metadata": eligible, "target_count": count, "overflow": overflow,
        "events": events, "bond_completion_proven": False, "encryption_or_persistence_proven": False,
        "interpretation": "Requests/raw link metadata only. Zero count or getter failure is inconclusive, not proof of no bond. Core event ownership and post-reboot persistence are not established by this record.",
    }

def self_test():
    data = bytearray(SIZE)
    struct.pack_into("<IIQQIIII", data, 0, 0x41525341, 1, 1000, 1001, 0, 0, 0, 0)
    struct.pack_into("<IIII", data, 40, 0x41525345, 1, 1, 0)
    struct.pack_into("<QII", data, 56, 999, 3, 1)
    struct.pack_into("<II", data, 184, checksum(data[:184]), 0)
    assert decode(data)["target_count"] == 1
    for i in range(SIZE):
        bad = bytearray(data); bad[i] ^= 1
        try:
            decode(bad)
        except ValueError:
            pass
        else:
            raise AssertionError("Corruption admitted")
    for length in (0, 1, 191, 193):
        try:
            decode(bytes(length))
        except ValueError:
            pass
        else:
            raise AssertionError("Wrong length admitted")
    for offset, fmt, value in ((48, "<I", 9), (56, "<Q", 1002), (64, "<I", 1), (68, "<I", 256), (8, "<Q", 0), (16, "<Q", 3001)):
        bad = bytearray(data); struct.pack_into(fmt, bad, offset, value)
        struct.pack_into("<I", bad, 184, checksum(bad[:184]))
        try:
            decode(bad)
        except ValueError:
            pass
        else:
            raise AssertionError("Malformed checksum-valid record admitted")
    print("PASS metadata decoder: exact192 bytes, every-byte corruption, malformed checksum-valid fields and timing rejection; no bond inference")

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("snapshot", nargs="?", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
    elif args.snapshot:
        with args.snapshot.open("rb") as stream:
            data = stream.read(SIZE + 1)
        print(json.dumps(decode(data), indent=2))
    else:
        parser.error("Specify a local snapshot or --self-test")

if __name__ == "__main__":
    main()
