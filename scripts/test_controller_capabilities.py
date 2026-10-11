#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Synthetic-only tests: no console, network, real captures or keys."""

import io
import json
from pathlib import Path
import random
import struct
import subprocess
import sys
import unittest

import read_controller_capabilities as reader

HEADER = b'btsnoop\0'+struct.pack('>II', 1, 1002)
CAPS = bytes([0, 4, 1, 0x34, 0x12, 8, 1, 16, 1])
VERSION = struct.pack('<BBHBHH', 0, 8, 0x1234, 8, 15, 0x2409)


def complete(opcode=0xfd53, payload=CAPS):
    return bytes([4, 0x0e, 3+len(payload), 1])+struct.pack('<H', opcode)+payload


def command(opcode=0xfd53, payload=b''):
    return b'\1'+struct.pack('<HB', opcode, len(payload))+payload


def record(packet, flags=3, original=None, drops=0, timestamp=1):
    return struct.pack('>IIIIQ', len(packet) if original is None else original,
                       len(packet), flags, drops, timestamp)+packet


def parse(*records, header=HEADER):
    data = header+b''.join(records)
    return reader.read_capture(io.BytesIO(data), len(data))


class CapabilityReaderTests(unittest.TestCase):
    def test_success_with_request_and_identity(self):
        result = parse(record(command(), 2), record(complete()),
                       record(command(0x1001), 2), record(complete(0x1001, VERSION)))
        cap = result['current_epoch']['vendor_capabilities']
        self.assertTrue(cap['filtering_reported'])
        self.assertEqual(cap['max_filters_reported'], 16)
        self.assertEqual(cap['scan_storage_bytes_reported'], 0x1234)
        self.assertTrue(cap['matching_request_seen'])
        self.assertIsNone(cap['free_filter_slots'])
        self.assertFalse(result['deep_sleep_wake_verified'])
        self.assertFalse(result['capture_origin_verified'])
        self.assertEqual(result['current_epoch']['local_version']['manufacturer_id'], 15)

    def test_response_without_request_is_marked(self):
        result = parse(record(complete()))
        self.assertFalse(result['current_epoch']['vendor_capabilities']['matching_request_seen'])

    def test_query_without_reply_differs_from_no_query_observed(self):
        result = parse(record(command(), flags=2))
        self.assertEqual(result['current_epoch']['vendor_capabilities']['outcome'], 'not_observed')
        self.assertEqual(result['observed_requests_in_current_epoch']['vendor_capabilities'], 1)
        self.assertEqual(parse()['observed_requests_in_current_epoch']['vendor_capabilities'], 0)
        result = parse(record(command(), flags=2), record(command(0x0c03), flags=2))
        self.assertEqual(result['observed_requests_in_current_epoch']['vendor_capabilities'], 0)

    def test_error_is_not_unsupported_hardware(self):
        for status in (1, 12, 255):
            with self.subTest(status=status):
                result = parse(record(complete(payload=bytes([status]))))
                cap = result['current_epoch']['vendor_capabilities']
                self.assertEqual(cap['outcome'], 'query_failed')
                self.assertNotIn('filtering_reported', cap)

    def test_failed_response_ignores_trailing_data(self):
        cap = reader.decode_return(0xfd53, b'\1'+b'SYNTHETIC-PRIVATE-CANARY')
        self.assertEqual(cap, {'kind': 'vendor_capabilities', 'status': 1, 'outcome': 'query_failed'})

    def test_no_filter_is_distinct_from_missing(self):
        caps = bytearray(CAPS); caps[6:8] = b'\0\0'
        cap = parse(record(complete(payload=caps)))['current_epoch']['vendor_capabilities']
        self.assertEqual(cap['outcome'], 'reported')
        self.assertFalse(cap['filtering_reported'])
        self.assertEqual(parse()['current_epoch']['vendor_capabilities']['outcome'], 'not_observed')

    def test_extensions_are_not_guessed(self):
        cap = reader.decode_return(0xfd53, CAPS+b'\0\x60'+b'SYNTHETIC-PRIVATE-CANARY')
        self.assertEqual(cap['extension_bytes_not_interpreted'], 26)
        self.assertNotIn('SYNTHETIC', json.dumps(cap))

    def test_incomplete_success_and_invalid_boolean(self):
        for length in tuple(range(9))+(10,):
            with self.subTest(length=length), self.assertRaises(ValueError):
                reader.decode_return(0xfd53, (CAPS+b'\0')[:length])
        for flag in (2, 255):
            bad = bytearray(CAPS); bad[6] = flag
            with self.assertRaises(ValueError): reader.decode_return(0xfd53, bad)

    def test_wrong_opcode_and_oversized_return(self):
        for opcode, payload in ((0x200d, CAPS), (0xfd53, bytes(253)), (0x1001, bytes(8)), (0x1001, bytes(10))):
            with self.assertRaises(ValueError): reader.decode_return(opcode, payload)

    def test_hci_container_without_h4(self):
        head = b'btsnoop\0'+struct.pack('>II', 1, 1001)
        result = parse(record(complete()[1:]), header=head)
        self.assertEqual(result['current_epoch']['vendor_capabilities']['max_filters_reported'], 16)

    def test_other_traffic_does_not_become_metadata(self):
        # Synthetic stand-ins for key events, ACL/HID and unrelated responses.
        canary = b'SYNTHETIC-PRIVATE-CANARY'
        items = [record(b'\4\x18'+bytes([len(canary)])+canary),
                 record(b'\2'+canary, flags=1),
                 record(command(0x201a, canary), flags=2),
                 record(complete(0x200d, canary))]
        result = parse(*items)
        self.assertNotIn('SYNTHETIC', json.dumps(result))
        self.assertEqual(result['current_epoch']['vendor_capabilities']['outcome'], 'not_observed')

    def test_command_status_is_not_complete(self):
        result = parse(record(b'\4\x0f\4\0\1\x53\xfd'))
        self.assertEqual(result['current_epoch']['vendor_capabilities']['outcome'], 'not_observed')

    def test_sensitive_packet_body_not_read(self):
        packet = b'\4\x18\x10'+b'Q'*16
        data = HEADER+record(packet)

        class Guarded(io.BytesIO):
            def read(self, count=-1):
                start = self.tell()
                if start < len(data) and start+count > 43:
                    raise AssertionError('Non-allowlisted event body read')
                return super().read(count)

        reader.read_capture(Guarded(data), len(data))

    def test_reset_prevents_stale_identity_merge(self):
        result = parse(record(complete(0x1001, VERSION)), record(complete()),
                       record(command(0x0c03), flags=2), record(complete()))
        self.assertEqual(result['current_reset_epoch'], 1)
        self.assertEqual(result['metadata_replies_in_earlier_epochs'], 2)
        self.assertEqual(result['current_epoch']['local_version']['outcome'], 'not_observed')

    def test_conflicts_fail_closed_and_repeats_are_counted(self):
        result = parse(record(complete()), record(complete(payload=b'\1')))
        cap = result['current_epoch']['vendor_capabilities']
        self.assertEqual(cap['outcome'], 'conflicting_replies')
        self.assertNotIn('filtering_reported', cap)
        cap = parse(record(complete()), record(complete()))['current_epoch']['vendor_capabilities']
        self.assertEqual(cap['reply_count'], 2)

    def test_known_gaps_are_visible_and_not_decoded(self):
        result = parse(record(complete()[:8], original=len(complete()), drops=3))
        self.assertTrue(result['capture_has_known_gaps'])
        self.assertEqual(result['truncated_records'], 1)
        self.assertEqual(result['reported_drops'], 3)
        self.assertEqual(result['current_epoch']['vendor_capabilities']['outcome'], 'not_observed')

    def test_bad_container_headers(self):
        for head in (b'x'*16, b'btsnoop\0'+struct.pack('>II', 2, 1002),
                     b'btsnoop\0'+struct.pack('>II', 1, 2001)):
            with self.assertRaises(ValueError): parse(header=head)

    def test_all_nonboundary_truncations(self):
        data = HEADER+record(complete())
        for length in range(len(data)):
            if length == 16: continue  # A valid empty file is inconclusive.
            with self.subTest(length=length), self.assertRaises(ValueError):
                reader.read_capture(io.BytesIO(data[:length]), length)

    def test_record_bounds_flags_direction_and_timestamps(self):
        bad = [record(complete(), original=1), record(complete(), flags=4),
               record(complete(), flags=2), record(command(), flags=3),
               record(b'\xff'), record(b'\4\x0e\1\0'),
               record(b'\4\x0e\0\0'), record(command(payload=b'\0'), flags=2)]
        for item in bad:
            with self.subTest(item_length=len(item)), self.assertRaises(ValueError): parse(item)
        with self.assertRaises(ValueError):
            parse(record(complete(), timestamp=2), record(complete(), timestamp=1))

    def test_metadata_and_size_limits(self):
        with self.assertRaises(ValueError): parse(*[record(complete())]*(reader.MAX_METADATA+1))
        with self.assertRaises(ValueError): reader.read_capture(io.BytesIO(HEADER), reader.MAX_BYTES+1)

    def test_bounded_mutations_never_claim_wake_or_leak_payload(self):
        original = HEADER+record(complete())
        rng = random.Random(731)
        for _ in range(1000):
            mutated = bytearray(original)
            for _ in range(rng.randrange(1, 5)):
                mutated[rng.randrange(len(mutated))] ^= rng.randrange(1, 256)
            try:
                result = reader.read_capture(io.BytesIO(mutated), len(mutated))
            except ValueError:
                continue
            self.assertFalse(result['deep_sleep_wake_verified'])
            self.assertFalse(result['raw_payloads_exported'])
            self.assertFalse(result['capture_origin_verified'])

    def test_cli_errors_do_not_echo_private_path_or_partial_report(self):
        script = Path(reader.__file__).resolve()
        for target in (script.parent/'SYNTHETIC-PRIVATE-CANARY-missing', script.parent, script):
            result = subprocess.run([sys.executable, str(script), str(target)],
                                    capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 2)
            self.assertEqual(result.stdout, '')
            self.assertNotIn(str(target), result.stderr)
            self.assertNotIn('SYNTHETIC', result.stderr)


if __name__ == '__main__':
    unittest.main()
