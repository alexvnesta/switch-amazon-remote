#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Read selected metadata from an existing local btsnoop capture, never a device.

Only Read Local Version and LE Get Vendor Capabilities command completions
are decoded. Other packet bodies are skipped, not printed or exported. This
does not start a capture, send commands, configure filters or establish wake.
Sources: AOSP HCI requirements; BlueZ src/shared/btsnoop.{c,h} and hci.h.
"""

import argparse
import json
import os
from pathlib import Path
import stat
import struct

MAX_BYTES = 64 * 1024 * 1024
MAX_RECORD_BYTES = 65540
MAX_RECORDS = 1_000_000
MAX_METADATA = 64
NAMES = {0x1001: 'local_version', 0xfd53: 'vendor_capabilities'}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def decode_return(opcode, payload):
    """Decode an already-selected completion's return parameters, not H4 data."""
    require(opcode in NAMES, 'Command is outside the metadata allowlist')
    require(1 <= len(payload) <= 252, 'Invalid return-parameter length')
    result = {'kind': NAMES[opcode], 'status': payload[0]}
    if payload[0]:
        # Error replies need not contain the success fields. Never read them.
        return dict(result, outcome='query_failed')
    if opcode == 0x1001:
        require(len(payload) == 9, 'Invalid local-version response length')
        _, hci, revision, lmp, manufacturer, subversion = struct.unpack('<BBHBHH', payload)
        return dict(result, outcome='reported', hci_version=hci,
                    hci_revision=revision, lmp_version=lmp,
                    manufacturer_id=manufacturer, lmp_subversion=subversion)
    # The legacy prefix is 9 bytes including status. Later formats append
    # version and more fields. Do not guess extension layouts or byte order.
    require(len(payload) == 9 or len(payload) >= 11, 'Incomplete capability prefix/version')
    require(payload[6] in (0, 1), 'Invalid filtering-support value')
    return dict(result, outcome='reported', filtering_reported=bool(payload[6]),
                max_filters_reported=payload[7],
                scan_storage_bytes_reported=int.from_bytes(payload[3:5], 'little'),
                extension_bytes_not_interpreted=len(payload)-9,
                free_filter_slots=None, filter_retention_during_sleep=None,
                deep_sleep_wake_verified=False)


def read_capture(stream, size):
    """Read one bounded, seekable btsnoop v1 HCI/UART file.

    The enclosing file must remain unchanged. CLI checks regular-file identity
    and timestamps; callers of this function must provide an immutable stream.
    Unknown data-link formats and malformed framing fail without partial output.
    """
    require(16 <= size <= MAX_BYTES, 'Capture size outside the supported limit')
    stream.seek(0)
    header = stream.read(16)
    require(len(header) == 16 and header[:8] == b'btsnoop\0', 'Not a btsnoop capture')
    version, link_type = struct.unpack('>II', header[8:])
    require(version == 1 and link_type in (1001, 1002), 'Unsupported btsnoop version/link type')
    records = truncated = dropped = epoch = 0
    metadata = []
    requests = {'local_version': 0, 'vendor_capabilities': 0}
    last_timestamp = None

    def take(n, end):
        require(stream.tell()+n <= end, 'Incomplete packet framing')
        data = stream.read(n)
        require(len(data) == n, 'Capture changed or was truncated')
        return data

    while stream.tell() < size:
        record_header = take(24, size)
        original, included, flags, drops, timestamp = struct.unpack('>IIIIQ', record_header)
        records += 1
        require(records <= MAX_RECORDS, 'Record limit exceeded')
        require(0 < original <= MAX_RECORD_BYTES and included <= original,
                'Invalid capture-record size')
        require(flags <= 3, 'Unsupported record flags')
        require(last_timestamp is None or timestamp >= last_timestamp,
                'Non-monotonic capture timestamps')
        last_timestamp = timestamp
        dropped = max(dropped, drops)
        end = stream.tell()+included
        require(end <= size, 'Truncated capture record')
        if original != included:
            truncated += 1
            stream.seek(end)
            continue
        if link_type == 1002:
            packet_type = take(1, end)[0]
            require(packet_type in (1, 2, 3, 4, 5), 'Unsupported H4 packet type')
        else:
            packet_type = (4 if flags & 1 else 1) if flags & 2 else 2

        if packet_type == 1:
            require(not flags & 1, 'Command has incoming direction')
            opcode, length = struct.unpack('<HB', take(3, end))
            require(end-stream.tell() == length, 'Command length mismatch')
            if opcode in NAMES or opcode == 0x0c03:
                require(length == 0, 'Unexpected metadata/reset command parameters')
                if opcode == 0x0c03:
                    # Separate results across reset attempts, even if completion
                    # is absent. Never merge identity from one epoch with another.
                    epoch += 1
                    requests = dict.fromkeys(requests, 0)
                else:
                    requests[NAMES[opcode]] += 1
        elif packet_type == 4:
            require(bool(flags & 1), 'Event has outgoing direction')
            event, length = take(2, end)
            require(end-stream.tell() == length, 'Event length mismatch')
            if event == 0x0e:  # Command Complete, not Command Status.
                require(length >= 3, 'Short Command Complete envelope')
                _, opcode = struct.unpack('<BH', take(3, end))
                if opcode in NAMES:
                    require(len(metadata) < MAX_METADATA, 'Metadata record limit exceeded')
                    entry = decode_return(opcode, take(length-3, end))
                    metadata.append(dict(entry, epoch=epoch, record=records,
                                         matching_request_seen=requests[NAMES[opcode]] > 0))
        # ACL, SCO, ISO, advertisements, keys and unrelated events are skipped.
        # No bytes, peer addresses, names, absolute timestamps or paths exported.
        stream.seek(end)
    require(stream.tell() == size and not stream.read(1), 'Capture changed during read')
    current = [entry for entry in metadata if entry['epoch'] == epoch]
    outcomes = {}
    for kind in NAMES.values():
        selected = [entry for entry in current if entry['kind'] == kind]
        if not selected:
            outcomes[kind] = {'outcome': 'not_observed'}
            continue
        semantic = [{k: v for k, v in entry.items()
                     if k not in ('record', 'epoch', 'matching_request_seen')} for entry in selected]
        if any(item != semantic[0] for item in semantic[1:]):
            outcomes[kind] = {'outcome': 'conflicting_replies', 'reply_count': len(selected)}
        else:
            outcomes[kind] = dict(semantic[0], reply_count=len(selected),
                                 matching_request_seen=all(x['matching_request_seen'] for x in selected))
    return {
        'schema': 1, 'format': 'btsnoop_v1_hci' if link_type == 1001 else 'btsnoop_v1_h4',
        'records': records, 'current_reset_epoch': epoch,
        'observed_requests_in_current_epoch': requests,
        'metadata_replies_in_earlier_epochs': len(metadata)-len(current),
        'truncated_records': truncated, 'reported_drops': dropped,
        'capture_has_known_gaps': bool(truncated or dropped),
        'current_epoch': outcomes,
        'capture_origin_verified': False, 'deep_sleep_wake_verified': False,
        'radio_commands_sent': False, 'raw_payloads_exported': False,
        'interpretation': 'Reported capabilities are not enabled filters, free slots, '
                          'sleep retention or wake proof. Missing replies are inconclusive. '
                          'Origin and controller identity require independent provenance.',
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path, help='Existing private local btsnoop file')
    args = parser.parse_args()
    try:
        # O_NONBLOCK avoids hanging on a mistaken FIFO path before fstat. No
        # sockets, device API, capture invocation or output-file creation exists.
        fd = os.open(args.capture, os.O_RDONLY | os.O_NONBLOCK)
        with os.fdopen(fd, 'rb') as stream:
            before = os.fstat(stream.fileno())
            require(stat.S_ISREG(before.st_mode), 'Input must be an ordinary file')
            result = read_capture(stream, before.st_size)
            after = os.fstat(stream.fileno())
            require((before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns, before.st_ctime_ns) ==
                    (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns, after.st_ctime_ns),
                    'Capture changed during read')
    except (OSError, ValueError):
        # Avoid exceptions that echo private filenames or source payloads.
        parser.exit(2, 'Capture rejected: unsupported, malformed, changing or inaccessible input. No partial report emitted.\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
