#!/usr/bin/env python3
"""
tests/cli/test_obs_cli.py — Unit tests for the Observatory CLI tool

Builds .iqtrace binaries in memory and verifies library parse functions.

@author  Ritesh Anand
@company embediq.com | ritzylab.com

SPDX-License-Identifier: Apache-2.0
"""

import os
import sys
import struct

# Add CLI tool to import path
sys.path.insert(0, os.path.join(os.path.dirname(__file__),
                                '..', '..', 'tools', 'embediq_obs'))

from embediq_obs import (parse_file_header, parse_session, parse_events,
                          stats, filter_events, export_json,
                          FILE_HDR_SIZE, SESSION_SIZE, EVENT_SIZE,
                          TLV_HDR_SIZE, TLV_SESSION, TLV_EVENT,
                          TLV_EVENT_BATCH, TLV_STREAM_END)

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

_FILE_HDR = struct.Struct('<4sHH')
_TLV_HDR = struct.Struct('<HH')
_SESSION = struct.Struct('<IIQIBBxx16s')
_EVENT = struct.Struct('<BBBBHII')


def _pack_session():
    """Build a 40-byte session payload with known values."""
    fw_version = (1 << 16) | (2 << 8) | 3  # 1.2.3
    build_id = b'testbld0' + b'\x00' * 8  # pad to 16 bytes
    return _SESSION.pack(
        0xDEADBEEF,       # device_id
        fw_version,       # fw_version
        1234567890000000, # timestamp_base_us
        7,                # session_id
        0,                # platform_id = POSIX
        1,                # trace_level
        build_id,         # build_id (16 bytes)
    )


def _pack_event(event_type, source, target, state, msg_id, timestamp_us, sequence):
    """Build a 14-byte event payload."""
    return _EVENT.pack(event_type, source, target, state,
                       msg_id, timestamp_us, sequence)


def _pack_tlv(tlv_type, payload):
    """Build a complete TLV record (header + payload)."""
    return _TLV_HDR.pack(tlv_type, len(payload)) + payload


def build_test_iqtrace(events_list):
    """
    Build a valid .iqtrace binary in memory.
    events_list: list of tuples (event_type, source, target, state,
                                  msg_id, timestamp_us, sequence)
    Returns bytes.
    """
    # File header
    data = _FILE_HDR.pack(b'IQTR', 1, 0)

    # SESSION TLV
    session_payload = _pack_session()
    data += _pack_tlv(TLV_SESSION, session_payload)

    # EVENT TLVs
    for ev in events_list:
        event_payload = _pack_event(*ev)
        data += _pack_tlv(TLV_EVENT, event_payload)

    # STREAM_END TLV
    data += _pack_tlv(TLV_STREAM_END, b'')

    return data


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

_tests_run = 0
_tests_failed = 0


def _assert(cond, msg):
    global _tests_run, _tests_failed
    _tests_run += 1
    if not cond:
        _tests_failed += 1
        frame = sys._getframe(1)
        print(f'FAIL  {frame.f_code.co_name}  {msg}', file=sys.stderr)
    else:
        frame = sys._getframe(1)
        print(f'PASS  {frame.f_code.co_name}')


def test_parse_file_header():
    data = build_test_iqtrace([])
    hdr = parse_file_header(data)
    _assert(hdr['magic'] == b'IQTR', 'magic must be IQTR')
    _assert(hdr['version'] == 1, 'version must be 1')


def test_parse_session_tlv():
    data = build_test_iqtrace([])
    s = parse_session(data)
    _assert(s['device_id'] == 0xDEADBEEF, 'device_id must match')
    _assert(s['fw_version'] == (1, 2, 3), 'fw_version must be (1,2,3)')
    _assert(s['platform'] == 'POSIX', 'platform must be POSIX')
    _assert(s['trace_level'] == 1, 'trace_level must be 1')
    _assert(s['build_id'] == 'testbld0', 'build_id must be testbld0')


def test_parse_event_tlv():
    # One LIFECYCLE event (0x30), seq=0
    events_list = [(0x30, 0, 0xFF, 2, 0, 1000, 0)]
    data = build_test_iqtrace(events_list)
    events = parse_events(data)
    _assert(len(events) == 1, 'must parse 1 event')
    _assert(events[0]['family'] == 'STATE', 'family must be STATE')
    _assert(events[0]['type_name'] == 'LIFECYCLE', 'type_name must be LIFECYCLE')
    _assert(events[0]['sequence'] == 0, 'sequence must be 0')


def test_parse_event_batch():
    # Build file with one EVENT_BATCH TLV containing 3 events
    data = _FILE_HDR.pack(b'IQTR', 1, 0)
    data += _pack_tlv(TLV_SESSION, _pack_session())

    # EVENT_BATCH: uint16 count + count * 14-byte events
    batch_payload = struct.pack('<H', 3)
    for i in range(3):
        batch_payload += _pack_event(0x30, 0, 0xFF, 2, 0, 1000 + i, i)
    data += _pack_tlv(TLV_EVENT_BATCH, batch_payload)
    data += _pack_tlv(TLV_STREAM_END, b'')

    events = parse_events(data)
    _assert(len(events) == 3, 'must parse 3 events from batch')
    _assert(events[0]['sequence'] == 0, 'first event sequence must be 0')
    _assert(events[2]['sequence'] == 2, 'third event sequence must be 2')


def test_stats_output():
    events_list = [
        (0x30, 0, 0xFF, 2, 0, 1000, 0),  # LIFECYCLE
        (0x30, 0, 0xFF, 2, 0, 2000, 1),  # LIFECYCLE
        (0x30, 0, 0xFF, 2, 0, 3000, 2),  # LIFECYCLE
        (0x20, 0, 1, 0, 0x100, 4000, 3), # MSG_TX
        (0x20, 0, 1, 0, 0x100, 5000, 4), # MSG_TX
        (0x60, 0, 0xFF, 3, 0, 6000, 5),  # FAULT
    ]
    data = build_test_iqtrace(events_list)
    s = stats(data)
    _assert(s['total_events'] == 6, 'total_events must be 6')
    _assert(s['by_family'].get('STATE', 0) == 3, 'STATE count must be 3')
    _assert(s['by_family'].get('MESSAGE', 0) == 2, 'MESSAGE count must be 2')
    _assert(s['by_family'].get('FAULT', 0) == 1, 'FAULT count must be 1')


def test_export_json():
    events_list = [(0x30, 0, 0xFF, 2, 0, 1000, 0)]
    data = build_test_iqtrace(events_list)
    result = export_json(data)
    _assert(len(result) == 1, 'export_json must return 1 event')
    _assert(result[0]['family'] == 'STATE', 'family must be STATE')
    _assert(result[0]['type_name'] == 'LIFECYCLE', 'type_name must be LIFECYCLE')
    _assert('session' in result[0], 'event must include session metadata')
    # Verify JSON-serialisable
    import json
    try:
        json.dumps(result)
        _assert(True, 'result is JSON serialisable')
    except (TypeError, ValueError):
        _assert(False, 'result must be JSON serialisable')


def test_filter_by_family():
    events_list = [
        (0x30, 0, 0xFF, 2, 0, 1000, 0),  # LIFECYCLE (STATE)
        (0x30, 0, 0xFF, 2, 0, 2000, 1),  # LIFECYCLE (STATE)
        (0x20, 0, 1, 0, 0x100, 3000, 2), # MSG_TX (MESSAGE)
        (0x60, 0, 0xFF, 3, 0, 4000, 3),  # FAULT
    ]
    data = build_test_iqtrace(events_list)
    filtered = filter_events(data, family='STATE')
    _assert(len(filtered) == 2, 'must return 2 STATE events')
    _assert(all(e['family'] == 'STATE' for e in filtered),
            'all events must be STATE family')


def test_decode_stream_end_accepted():
    # SESSION + STREAM_END, no events
    data = build_test_iqtrace([])
    events = parse_events(data)
    _assert(len(events) == 0, 'must return 0 events for empty trace')


def test_unknown_tlv_skipped():
    # Build file with an unknown TLV between SESSION and EVENT
    data = _FILE_HDR.pack(b'IQTR', 1, 0)
    data += _pack_tlv(TLV_SESSION, _pack_session())

    # Unknown TLV type 0x00AA with 6 bytes payload
    data += _pack_tlv(0x00AA, b'\xDE\xAD\xBE\xEF\xCA\xFE')

    # One EVENT
    data += _pack_tlv(TLV_EVENT, _pack_event(0x30, 0, 0xFF, 2, 0, 1000, 0))
    data += _pack_tlv(TLV_STREAM_END, b'')

    events = parse_events(data)
    _assert(len(events) == 1, 'must parse 1 event after skipping unknown TLV')
    _assert(events[0]['type_name'] == 'LIFECYCLE',
            'event must be LIFECYCLE after skipping unknown')


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

if __name__ == '__main__':
    test_parse_file_header()
    test_parse_session_tlv()
    test_parse_event_tlv()
    test_parse_event_batch()
    test_stats_output()
    test_export_json()
    test_filter_by_family()
    test_decode_stream_end_accepted()
    test_unknown_tlv_skipped()

    print()
    if _tests_failed == 0:
        print(f'All {_tests_run} tests passed. (0 failed)')
    else:
        print(f'{_tests_failed} of {_tests_run} tests FAILED.')
    sys.exit(1 if _tests_failed else 0)
