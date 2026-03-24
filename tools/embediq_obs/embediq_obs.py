#!/usr/bin/env python3
"""
tools/embediq_obs/embediq_obs.py — EmbedIQ Observatory CLI tool

Open-source CLI for decoding, filtering, and exporting .iqtrace files.
Conforms to docs/observability/iqtrace_format.md v1.0.

Usage:
    python3 tools/embediq_obs/embediq_obs.py obs <command> [args]

Commands:
    decode   — Decode and print all events from a .iqtrace file
    stats    — Print trace statistics
    filter   — Filter events by family / type / source
    export   — Export all events as JSON
    tail     — Print all events (alias for decode)
    capture  — (stub) Capture live stream to file

@author  Ritesh Anand
@company embediq.com | ritzylab.com

SPDX-License-Identifier: Apache-2.0
"""

import argparse
import struct
import sys

# ---------------------------------------------------------------------------
# Constants (from iqtrace_format.md)
# ---------------------------------------------------------------------------

IQTRACE_MAGIC   = b'\x49\x51\x54\x52'
IQTRACE_VERSION = 1
TLV_SESSION     = 0x0001
TLV_EVENT       = 0x0002
TLV_EVENT_BATCH = 0x0003
TLV_STREAM_END  = 0x0004
TLV_PADDING     = 0x00FF
FILE_HDR_SIZE   = 8
SESSION_SIZE    = 40
EVENT_SIZE      = 14
TLV_HDR_SIZE    = 4

# ---------------------------------------------------------------------------
# Struct formats (little-endian)
# ---------------------------------------------------------------------------

# 8-byte file header: 4s magic, H version, H reserved
_FILE_HDR = struct.Struct('<4sHH')

# 4-byte TLV header: H type, H length
_TLV_HDR = struct.Struct('<HH')

# 40-byte session: I device_id, I fw_version, Q timestamp_base_us,
#                  I session_id, B platform_id, B trace_level, 2x pad, 16s build_id
_SESSION = struct.Struct('<IIQIBBxx16s')

# 14-byte event: B event_type, B source, B target, B state,
#               H msg_id, I timestamp_us, I sequence
_EVENT = struct.Struct('<BBBBHII')

# ---------------------------------------------------------------------------
# Lookup tables
# ---------------------------------------------------------------------------

EVENT_NAMES = {
    0x10: 'OVERFLOW',
    0x20: 'MSG_TX',
    0x21: 'MSG_RX',
    0x22: 'QUEUE_DROP',
    0x30: 'LIFECYCLE',
    0x31: 'FSM_TRANS',
    0x60: 'FAULT',
}

PLATFORM_NAMES = {
    0: 'POSIX',
    1: 'FreeRTOS',
    2: 'Baremetal',
    3: 'Zephyr',
    0xFF: 'Unknown',
}

# ---------------------------------------------------------------------------
# Library functions
# ---------------------------------------------------------------------------


def event_family(event_type: int) -> str:
    """Derive family from event_type band per iqtrace_format.md section 3."""
    if event_type < 0x20:
        return 'SYSTEM'
    if event_type < 0x30:
        return 'MESSAGE'
    if event_type < 0x40:
        return 'STATE'
    if event_type < 0x50:
        return 'RESOURCE'
    if event_type < 0x60:
        return 'TIMING'
    if event_type < 0x70:
        return 'FAULT'
    return 'FUNCTION'


def event_type_name(event_type: int) -> str:
    return EVENT_NAMES.get(event_type, f'0x{event_type:02X}')


def _read_file(path: str) -> bytes:
    with open(path, 'rb') as f:
        return f.read()


def parse_file_header(data: bytes) -> dict:
    """
    Parse and validate the 8-byte .iqtrace file header.
    Returns dict: {magic, version}.
    Raises ValueError on bad magic or unsupported version.
    """
    if len(data) < FILE_HDR_SIZE:
        raise ValueError('File too short for header')
    magic, version, _ = _FILE_HDR.unpack_from(data, 0)
    if magic != IQTRACE_MAGIC:
        raise ValueError(f'Bad magic: {magic!r}')
    if version != IQTRACE_VERSION:
        raise ValueError(f'Unsupported version: {version}')
    return {'magic': magic, 'version': version}


def parse_session(data: bytes) -> dict:
    """
    Find and parse the SESSION TLV (must be first TLV after file header).
    Returns dict with all session fields.
    Raises ValueError if not found or malformed.
    """
    parse_file_header(data)
    offset = FILE_HDR_SIZE
    if len(data) < offset + TLV_HDR_SIZE:
        raise ValueError('No TLV after file header')
    tlv_type, tlv_len = _TLV_HDR.unpack_from(data, offset)
    if tlv_type != TLV_SESSION:
        raise ValueError(f'First TLV is not SESSION (got 0x{tlv_type:04X})')
    if tlv_len != SESSION_SIZE:
        raise ValueError(f'SESSION TLV length {tlv_len} != {SESSION_SIZE}')
    offset += TLV_HDR_SIZE
    (device_id, fw_version, timestamp_base_us, session_id,
     platform_id, trace_level, build_id_raw) = _SESSION.unpack_from(data, offset)
    major = (fw_version >> 16) & 0xFFFF
    minor = (fw_version >> 8) & 0x00FF
    patch = fw_version & 0x00FF
    build_id = build_id_raw.rstrip(b'\x00').decode('ascii', errors='replace')
    return {
        'device_id':         device_id,
        'fw_version':        (major, minor, patch),
        'fw_version_str':    f'{major}.{minor}.{patch}',
        'timestamp_base_us': timestamp_base_us,
        'session_id':        session_id,
        'platform_id':       platform_id,
        'platform':          PLATFORM_NAMES.get(platform_id, 'Unknown'),
        'trace_level':       trace_level,
        'build_id':          build_id,
    }


def _iter_tlv_records(data: bytes):
    """
    Yield (tlv_type, payload_bytes) for every TLV record after the file
    header + SESSION. Skips unknown types per section 8 rule 5.
    Stops at STREAM_END or end of data.
    """
    offset = FILE_HDR_SIZE
    tlv_type, tlv_len = _TLV_HDR.unpack_from(data, offset)
    offset += TLV_HDR_SIZE + tlv_len  # skip SESSION

    while offset + TLV_HDR_SIZE <= len(data):
        tlv_type, tlv_len = _TLV_HDR.unpack_from(data, offset)
        payload_start = offset + TLV_HDR_SIZE
        payload = data[payload_start: payload_start + tlv_len]
        offset = payload_start + tlv_len
        if tlv_type == TLV_STREAM_END:
            break
        yield tlv_type, payload


def _parse_event_bytes(raw: bytes) -> dict:
    """Parse a 14-byte event payload into a dict."""
    (evt_type, source, target, state,
     msg_id, timestamp_us, sequence) = _EVENT.unpack(raw)
    return {
        'event_type':   evt_type,
        'type_name':    event_type_name(evt_type),
        'family':       event_family(evt_type),
        'source':       source,
        'target':       target,
        'state':        state,
        'msg_id':       msg_id,
        'timestamp_us': timestamp_us,
        'sequence':     sequence,
    }


def parse_events(data: bytes) -> list:
    """
    Parse all EVENT and EVENT_BATCH records from a .iqtrace binary.
    Returns list of event dicts in file order.
    Unknown TLV types are skipped (forward-compat, per section 8 rule 5).
    """
    events = []
    for tlv_type, payload in _iter_tlv_records(data):
        if tlv_type == TLV_EVENT:
            if len(payload) == EVENT_SIZE:
                events.append(_parse_event_bytes(payload))
        elif tlv_type == TLV_EVENT_BATCH:
            if len(payload) >= 2:
                count = struct.unpack_from('<H', payload, 0)[0]
                for i in range(count):
                    start = 2 + i * EVENT_SIZE
                    if start + EVENT_SIZE <= len(payload):
                        events.append(_parse_event_bytes(
                            payload[start: start + EVENT_SIZE]))
        # else: unknown type, skip (forward-compat)
    return events


def stats(data: bytes) -> dict:
    """
    Compute statistics for a .iqtrace file.
    Returns dict with total_events, by_family, by_type, seq_min, seq_max,
    duration_us, session.
    """
    session = parse_session(data)
    events = parse_events(data)
    by_family = {}
    by_type = {}
    seqs = []
    for e in events:
        by_family[e['family']] = by_family.get(e['family'], 0) + 1
        by_type[e['type_name']] = by_type.get(e['type_name'], 0) + 1
        seqs.append(e['sequence'])
    seq_min = min(seqs) if seqs else 0
    seq_max = max(seqs) if seqs else 0
    ts_list = [e['timestamp_us'] for e in events]
    duration_us = (max(ts_list) - min(ts_list)) if len(ts_list) > 1 else 0
    return {
        'total_events': len(events),
        'by_family':    by_family,
        'by_type':      by_type,
        'seq_min':      seq_min,
        'seq_max':      seq_max,
        'duration_us':  duration_us,
        'session':      session,
    }


def filter_events(data: bytes, family: str = None,
                  event_type: int = None, source: int = None) -> list:
    """
    Return events matching ALL supplied filter criteria.
    family: 'STATE', 'MESSAGE', etc. (case-insensitive)
    event_type: integer event_type value
    source: integer source endpoint id
    """
    events = parse_events(data)
    if family is not None:
        events = [e for e in events
                  if e['family'].upper() == family.upper()]
    if event_type is not None:
        events = [e for e in events if e['event_type'] == event_type]
    if source is not None:
        events = [e for e in events if e['source'] == source]
    return events


def export_json(data: bytes) -> list:
    """
    Return all events as a JSON-serialisable list of dicts.
    Each dict contains all event fields plus session metadata under 'session'.
    """
    session = parse_session(data)
    events = parse_events(data)
    for e in events:
        e['session'] = session
    return events


def format_event(e: dict) -> str:
    """Format a single event dict as a human-readable line."""
    target = f'0x{e["target"]:02X}' if e['target'] == 0xFF else str(e['target'])
    return (f'[seq={e["sequence"]:>6} ts={e["timestamp_us"]:>10}] '
            f'{e["family"]}/{e["type_name"]:<12} '
            f'src={e["source"]} tgt={target} '
            f'state={e["state"]} msg=0x{e["msg_id"]:04X}')


# ---------------------------------------------------------------------------
# CLI commands
# ---------------------------------------------------------------------------


def cmd_decode(args):
    data = _read_file(args.file)
    session = parse_session(data)
    print(f'Session:  device=0x{session["device_id"]:08X}  '
          f'fw={session["fw_version_str"]}  '
          f'build={session["build_id"]}  '
          f'platform={session["platform"]}  '
          f'level={session["trace_level"]}')
    print()
    events = parse_events(data)
    if not events:
        print('(no events)')
        return
    for e in events:
        print(format_event(e))


def cmd_stats(args):
    data = _read_file(args.file)
    s = stats(data)
    sess = s['session']
    print(f'File:         {args.file}')
    print(f'Device:       0x{sess["device_id"]:08X}')
    print(f'Firmware:     {sess["fw_version_str"]}  build={sess["build_id"]}')
    print(f'Platform:     {sess["platform"]}')
    print(f'Total events: {s["total_events"]}')
    print(f'Seq range:    {s["seq_min"]} – {s["seq_max"]}')
    if s['duration_us'] > 0:
        print(f'Duration:     {s["duration_us"]} \u00b5s')
    print()
    print('Events by family:')
    for fam, count in sorted(s['by_family'].items()):
        print(f'  {fam:<12} {count}')
    print()
    print('Events by type:')
    for name, count in sorted(s['by_type'].items()):
        print(f'  {name:<16} {count}')


def cmd_filter(args):
    data = _read_file(args.file)
    et = int(args.type, 0) if args.type else None
    src = int(args.source, 0) if args.source else None
    evts = filter_events(data, family=args.family, event_type=et, source=src)
    if not evts:
        print('(no matching events)')
        return
    for e in evts:
        print(format_event(e))


def cmd_export(args):
    data = _read_file(args.file)
    evts = export_json(data)
    import json
    print(json.dumps(evts, indent=2))


def cmd_tail(args):
    """
    Read and print all events from a .iqtrace file.
    Note: live stream tail requires a hardware transport (UART/TCP/RTT).
    File-based tail reads the complete capture from the beginning.
    """
    data = _read_file(args.file)
    events = parse_events(data)
    for e in events:
        print(format_event(e))


def cmd_capture(args):
    """
    Capture a live stream to a .iqtrace file.
    Not yet implemented: requires hardware transport (UART/TCP/RTT).
    """
    print('capture: not yet implemented — requires hardware transport.',
          file=sys.stderr)
    print('Use embediq_obs_capture_begin() / embediq_obs_capture_end() '
          'in firmware to write .iqtrace files directly.', file=sys.stderr)
    sys.exit(1)


# ---------------------------------------------------------------------------
# Argparse entry point
# ---------------------------------------------------------------------------


def main():
    parser = argparse.ArgumentParser(
        prog='embediq',
        description='EmbedIQ CLI — Observatory tools')
    sub = parser.add_subparsers(dest='top', required=True)

    obs = sub.add_parser('obs', help='Observatory commands')
    obs_sub = obs.add_subparsers(dest='command', required=True)

    # decode
    p = obs_sub.add_parser('decode', help='Decode a .iqtrace file')
    p.add_argument('file')
    p.set_defaults(func=cmd_decode)

    # stats
    p = obs_sub.add_parser('stats', help='Print trace statistics')
    p.add_argument('file')
    p.set_defaults(func=cmd_stats)

    # filter
    p = obs_sub.add_parser('filter', help='Filter events from a trace')
    p.add_argument('file')
    p.add_argument('--family', help='Filter by family (STATE, MESSAGE, etc.)')
    p.add_argument('--type', help='Filter by event type (hex or decimal)')
    p.add_argument('--source', help='Filter by source endpoint id')
    p.set_defaults(func=cmd_filter)

    # export
    p = obs_sub.add_parser('export', help='Export trace as JSON')
    p.add_argument('file')
    p.add_argument('--json', action='store_true', default=True)
    p.set_defaults(func=cmd_export)

    # tail
    p = obs_sub.add_parser('tail', help='Print all events from a .iqtrace file')
    p.add_argument('file')
    p.set_defaults(func=cmd_tail)

    # capture
    p = obs_sub.add_parser('capture', help='Capture live stream to file')
    p.add_argument('--out', required=True, help='Output .iqtrace file')
    p.set_defaults(func=cmd_capture)

    args = parser.parse_args()
    args.func(args)


if __name__ == '__main__':
    main()
