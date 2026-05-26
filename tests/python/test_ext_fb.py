#!/usr/bin/env python3
# test_ext_fb.py — Python SDK unit tests for the embediq-python package (Item 5b)
#
# 5 tests covering EmbedIQMsg struct pack/unpack, TLV frame encode/decode,
# EmbedIQMsg field assignment and decode(), subscribe frame generation, and
# auto-reconnect behaviour. Uses unittest.mock for socket injection -- no
# running process or socket needed.
#
# @author  Ritesh Anand
# @company embediq.com | ritzylab.com
#
# SPDX-License-Identifier: Apache-2.0

import os
import sys
import struct
import unittest.mock

# Add the embediq-python package to import path (sibling-of-sibling pattern).
sys.path.insert(
    0,
    os.path.join(os.path.dirname(__file__), '..', '..', 'tools', 'embediq_python')
)

from embediq._msg import EmbedIQMsg
from embediq._transport import (
    encode_tlv,
    decode_tlv_frame,
    _Transport,
    FRAME_MSG,
    FRAME_SUBSCRIBE,
    FRAME_IDENTIFY,
    FRAME_ENDPOINT_ASSIGN,
)
from embediq.ext_fb import ExternalFB

# ---------------------------------------------------------------------------
# Test harness — matches tests/cli/test_obs_cli.py pattern exactly.
# ---------------------------------------------------------------------------

_tests_run = 0
_tests_failed = 0


def _assert(cond, msg):
    global _tests_run, _tests_failed
    _tests_run += 1
    frame = sys._getframe(1)
    if not cond:
        _tests_failed += 1
        print(f'FAIL  {frame.f_code.co_name}  {msg}', file=sys.stderr)
    else:
        print(f'PASS  {frame.f_code.co_name}  {msg}')


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

def test_msg_pack_unpack():
    """EmbedIQMsg.pack() / .unpack() round-trip preserves every field."""
    payload = bytes(range(7)) + b'\x00' * 57   # 7 meaningful bytes, pad to 64
    msg = EmbedIQMsg(
        msg_id=0x0640,
        source_endpoint_id=0x42,
        target_endpoint_id=0xFF,
        schema_id=1,
        payload_len=7,
        correlation_id=0xCAFEBABE,
        timestamp_us=1234567,
        priority=1,
        raw_payload=payload,
    )
    raw = msg.pack()
    _assert(len(raw) == 84, 'packed size must be 84 bytes')

    msg2 = EmbedIQMsg.unpack(raw)
    _assert(msg2.msg_id == msg.msg_id, 'msg_id round-trip')
    _assert(msg2.source_endpoint_id == msg.source_endpoint_id, 'source round-trip')
    _assert(msg2.target_endpoint_id == msg.target_endpoint_id, 'target round-trip')
    _assert(msg2.schema_id == msg.schema_id, 'schema_id round-trip')
    _assert(msg2.payload_len == msg.payload_len, 'payload_len round-trip')
    _assert(msg2.correlation_id == msg.correlation_id, 'correlation_id round-trip')
    _assert(msg2.timestamp_us == msg.timestamp_us, 'timestamp_us round-trip')
    _assert(msg2.priority == msg.priority, 'priority round-trip')
    _assert(
        msg2.raw_payload[:msg.payload_len] == msg.raw_payload[:msg.payload_len],
        'payload round-trip',
    )


def test_tlv_encode_decode():
    """encode_tlv() / decode_tlv_frame() round-trip for three frame types."""
    cases = [
        (FRAME_MSG,       b'\x00' * 84),
        (FRAME_SUBSCRIBE, struct.pack('<HH', 0x0640, 0x0641)),
        (FRAME_IDENTIFY,  b'my_agent\x00'),
    ]
    for frame_type, payload in cases:
        encoded = encode_tlv(frame_type, payload)
        _assert(
            len(encoded) == 4 + len(payload),
            f'TLV total length for frame {frame_type:#06x}',
        )
        ft_out, payload_out = decode_tlv_frame(encoded)
        _assert(ft_out == frame_type, f'frame_type round-trip {frame_type:#06x}')
        _assert(payload_out == payload, f'payload round-trip {frame_type:#06x}')


def test_embediq_msg_fields():
    """EmbedIQMsg field assignment + decode() returns typed payload when stub registered."""
    msg = EmbedIQMsg(
        msg_id=0x0640,
        source_endpoint_id=0x42,
        target_endpoint_id=0xFF,
        schema_id=1,
        payload_len=7,
        correlation_id=0,
        timestamp_us=0,
        priority=1,
        raw_payload=b'\x01\x02\x03\x04\x05\x06\x07' + b'\x00' * 57,
    )
    _assert(msg.msg_id == 0x0640, 'msg_id field')
    _assert(msg.source_endpoint_id == 0x42, 'source_endpoint_id field')
    _assert(msg.raw_payload[0] == 0x01, 'payload byte 0')

    # decode() must not raise. If the generated telemetry stub is importable
    # and registered, decode() should return a typed dataclass instance for
    # MSG_TELEMETRY_GAUGE (0x0640).
    try:
        from embediq.msgs import telemetry_msgs   # noqa: F401
        from embediq._msg import register_msg_registry
        register_msg_registry(telemetry_msgs._MSG_REGISTRY)
        decoded = msg.decode()
        _assert(decoded is not None, 'decode() returned a value')
        _assert(
            type(decoded).__name__.startswith('MSG_TELEMETRY_GAUGE'),
            f'decode() returned a MSG_TELEMETRY_GAUGE type (got {type(decoded).__name__})',
        )
    except ImportError:
        # No generated stub yet — decode() must still work, returning raw bytes.
        decoded = msg.decode()
        _assert(
            decoded == msg.raw_payload[:msg.payload_len],
            'decode() returns raw_payload when no stub registered',
        )


def test_subscribe_frame():
    """_Transport.subscribe() must send a SUBSCRIBE TLV containing both msg_ids."""
    captured = []

    class _FakeSock:
        def sendall(self, data):
            captured.append(data)

    transport = _Transport(
        sock_path='/tmp/unused',
        name='test_agent',
        on_msg=lambda payload: None,
        on_connect=lambda: None,
        on_disconnect=lambda: None,
    )
    # Inject a fake socket so we capture writes without a real connection.
    transport._sock = _FakeSock()

    transport.subscribe(0x0640, 0x0641)

    _assert(len(captured) == 1, 'subscribe() wrote exactly one frame')
    frame_type, payload = decode_tlv_frame(captured[0])
    _assert(frame_type == FRAME_SUBSCRIBE, 'subscribe frame type is 0x0002')
    ids = struct.unpack(f'<{len(payload) // 2}H', payload)
    _assert(0x0640 in ids, 'subscribe payload contains msg_id 0x0640')
    _assert(0x0641 in ids, 'subscribe payload contains msg_id 0x0641')


def test_auto_reconnect():
    """ExternalFB._handle_disconnect() must call on_disconnect and attempt reconnect."""
    on_disconnect_called = []
    reconnect_called = []

    class _FakeTransport:
        def reconnect(self):
            reconnect_called.append(True)

    class _Agent(ExternalFB):
        name = 'reconnect_test_agent'
        def on_disconnect(self):
            on_disconnect_called.append(True)

    agent = _Agent()
    agent._transport = _FakeTransport()

    # Simulate the transport layer detecting a dropped connection.
    agent._handle_disconnect()

    _assert(
        len(on_disconnect_called) == 1,
        'on_disconnect() was invoked exactly once',
    )
    _assert(
        len(reconnect_called) == 1,
        'transport.reconnect() was invoked after on_disconnect',
    )


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

if __name__ == '__main__':
    test_msg_pack_unpack()
    test_tlv_encode_decode()
    test_embediq_msg_fields()
    test_subscribe_frame()
    test_auto_reconnect()

    print()
    if _tests_failed == 0:
        print(f'All {_tests_run} tests passed. (0 failed)')
    else:
        print(f'{_tests_failed} of {_tests_run} tests FAILED.')
    sys.exit(1 if _tests_failed else 0)
