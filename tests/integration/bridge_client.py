#!/usr/bin/env python3
# tests/integration/bridge_client.py — Raw TLV client for the bridge socket integration test
#
# Connects to fb_bridge over the Unix socket path passed as argv[1], performs
# the IDENTIFY/ENDPOINT_ASSIGN handshake using raw struct.pack TLV frames,
# publishes one MSG_HARNESS_A (0x0430), and exits 0. Deliberately does NOT
# import the embediq-python SDK — this exercises the Layer 1 wire protocol
# end-to-end, independent of the SDK.
#
# @author  Ritesh Anand
# @company embediq.com | ritzylab.com
#
# SPDX-License-Identifier: Apache-2.0

import socket
import struct
import sys

# TLV frame types — must match fb_bridge.c constants.
FRAME_MSG             = 0x0001
FRAME_IDENTIFY        = 0x0004
FRAME_ENDPOINT_ASSIGN = 0x0005
FRAME_HEALTH_REQ      = 0x0006
FRAME_HEALTH_RSP      = 0x0007

MSG_HARNESS_A = 0x0430                       # test harness msg_id
_HDR  = struct.Struct('<HH')                 # frame_type + payload_len
_MSG  = struct.Struct('<HBBHHIIBxxx64s')     # EmbedIQ_Msg_t wire format (84 bytes)


def _send_frame(sock, frame_type, payload):
    sock.sendall(_HDR.pack(frame_type, len(payload)) + payload)


def _recv_frame(sock):
    """Read one TLV frame. SOCK_SEQPACKET preserves message boundaries."""
    data = sock.recv(4096)
    if not data or len(data) < 4:
        return None, b''
    frame_type, payload_len = _HDR.unpack_from(data)
    return frame_type, data[4:4 + payload_len]


def main():
    if len(sys.argv) < 2:
        print('usage: bridge_client.py <socket_path>', file=sys.stderr)
        return 1
    sock_path = sys.argv[1]

    sock = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
    sock.connect(sock_path)

    # 1. IDENTIFY — send name (NUL-terminated).
    _send_frame(sock, FRAME_IDENTIFY, b'test_bridge_client\x00')

    # 2. Wait for ENDPOINT_ASSIGN; respond to any HEALTH_REQ that arrives first.
    endpoint_id = None
    while endpoint_id is None:
        frame_type, payload = _recv_frame(sock)
        if frame_type is None:
            print('bridge_client: connection closed before ENDPOINT_ASSIGN', file=sys.stderr)
            return 1
        if frame_type == FRAME_ENDPOINT_ASSIGN:
            if len(payload) < 1:
                print('bridge_client: malformed ENDPOINT_ASSIGN payload', file=sys.stderr)
                return 1
            endpoint_id = payload[0]
            break
        if frame_type == FRAME_HEALTH_REQ:
            # status=1 (ok), uptime_ms=0 — 5 bytes.
            _send_frame(sock, FRAME_HEALTH_RSP, struct.pack('<BI', 1, 0))
            continue
        # Any other frame: ignore and read the next one.

    # 3. Publish one MSG_HARNESS_A — broadcast, priority NORMAL.
    msg_bytes = _MSG.pack(
        MSG_HARNESS_A,   # msg_id
        0xFF,            # source_endpoint_id (fb_bridge overwrites with virtual ep)
        0xFF,            # target_endpoint_id (broadcast)
        0,               # schema_id
        0,               # payload_len
        0,               # correlation_id
        0,               # timestamp_us
        1,               # priority — NORMAL
        b'\x00' * 64,    # payload
    )
    _send_frame(sock, FRAME_MSG, msg_bytes)

    sock.close()
    return 0


if __name__ == '__main__':
    sys.exit(main())
