# _transport.py -- TLV framing, recv thread, heartbeat response, auto-reconnect (private)
#
# Internal transport layer for the embediq-python SDK. Handles Unix socket
# connection lifecycle, TLV frame encode/decode, background recv thread,
# heartbeat (HEALTH_REQ/HEALTH_RSP), and stateless auto-reconnect. Not part
# of the public API.
#
# @author  Ritesh Anand
# @company embediq.com | ritzylab.com
#
# SPDX-License-Identifier: Apache-2.0

import socket
import struct
import threading
import time
from typing import Callable, List, Optional

# TLV frame types — must match fb_bridge.c BRIDGE_FRAME_* constants.
FRAME_MSG             = 0x0001
FRAME_SUBSCRIBE       = 0x0002
FRAME_UNSUBSCRIBE     = 0x0003
FRAME_IDENTIFY        = 0x0004
FRAME_ENDPOINT_ASSIGN = 0x0005
FRAME_HEALTH_REQ      = 0x0006
FRAME_HEALTH_RSP      = 0x0007
FRAME_ERROR           = 0x0008

# 4-byte TLV header: frame_type (uint16 LE) + payload_len (uint16 LE).
_HDR = struct.Struct('<HH')


def encode_tlv(frame_type: int, payload: bytes) -> bytes:
    """Encode a TLV frame: 4-byte header + payload."""
    return _HDR.pack(frame_type, len(payload)) + payload


def decode_tlv_frame(data: bytes):
    """Decode a TLV frame. Returns (frame_type, payload) tuple."""
    if len(data) < 4:
        raise ValueError(f'decode_tlv_frame: need at least 4 bytes, got {len(data)}')
    frame_type, payload_len = _HDR.unpack_from(data)
    payload = data[4:4 + payload_len]
    return frame_type, payload


class _Transport:
    """
    Manages one Unix socket connection to fb_bridge. Used by ExternalFB —
    not intended for direct application use.
    """

    def __init__(self, sock_path: str, name: str,
                 on_msg: Callable, on_connect: Callable, on_disconnect: Callable):
        self._sock_path = sock_path
        self._name = name
        self._on_msg = on_msg
        self._on_connect = on_connect
        self._on_disconnect = on_disconnect
        self._sock: Optional[socket.socket] = None
        self._endpoint_id: Optional[int] = None
        self._subscriptions: List[int] = []
        self._recv_thread: Optional[threading.Thread] = None
        self._stop_event = threading.Event()

    # ----- public, called by ExternalFB --------------------------------------

    def connect(self) -> None:
        """Connect, send IDENTIFY, await ENDPOINT_ASSIGN, start recv thread, on_connect()."""
        self._sock = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        self._sock.connect(self._sock_path)
        # Send IDENTIFY.
        self._send(encode_tlv(FRAME_IDENTIFY, self._name.encode() + b'\x00'))
        # Receive ENDPOINT_ASSIGN: 4-byte header + 1-byte endpoint id.
        raw = self._sock.recv(5)
        ft, payload = decode_tlv_frame(raw)
        if ft != FRAME_ENDPOINT_ASSIGN:
            raise RuntimeError(f'Expected ENDPOINT_ASSIGN, got frame_type={ft:#06x}')
        self._endpoint_id = payload[0]
        # Start background recv thread.
        self._stop_event.clear()
        self._recv_thread = threading.Thread(target=self._recv_loop, daemon=True)
        self._recv_thread.start()
        self._on_connect()

    def disconnect(self) -> None:
        """Close the socket and stop the recv thread."""
        self._stop_event.set()
        if self._sock is not None:
            try:
                self._sock.close()
            except OSError:
                pass
            self._sock = None

    def subscribe(self, *msg_ids: int) -> None:
        """Send a SUBSCRIBE TLV. Thread-safe."""
        for mid in msg_ids:
            if mid not in self._subscriptions:
                self._subscriptions.append(mid)
        if not msg_ids:
            return
        payload = struct.pack(f'<{len(msg_ids)}H', *msg_ids)
        self._send(encode_tlv(FRAME_SUBSCRIBE, payload))

    def send_msg(self, msg_bytes: bytes) -> None:
        """Send a MSG_FRAME TLV with msg_bytes as payload. Thread-safe."""
        self._send(encode_tlv(FRAME_MSG, msg_bytes))

    def reconnect(self) -> None:
        """Reconnect: close, sleep, connect fresh, re-send IDENTIFY + SUBSCRIBE."""
        self.disconnect()
        time.sleep(0.5)
        self.connect()
        if self._subscriptions:
            payload = struct.pack(
                f'<{len(self._subscriptions)}H', *self._subscriptions)
            self._send(encode_tlv(FRAME_SUBSCRIBE, payload))

    # ----- internal ----------------------------------------------------------

    def _send(self, data: bytes) -> None:
        if self._sock is not None:
            self._sock.sendall(data)

    def _recv_loop(self) -> None:
        """Background thread: drain frames and dispatch."""
        while not self._stop_event.is_set():
            try:
                data = self._sock.recv(4096) if self._sock is not None else b''
                if not data:
                    break
                ft, payload = decode_tlv_frame(data)
                if ft == FRAME_MSG:
                    self._on_msg(payload)
                elif ft == FRAME_HEALTH_REQ:
                    # status=1 (ok), uptime_ms=0 — uptime tracking is deferred.
                    rsp = struct.pack('<BI', 1, 0)
                    self._send(encode_tlv(FRAME_HEALTH_RSP, rsp))
                elif ft == FRAME_ERROR:
                    break
            except (OSError, ValueError):
                break
        # Connection dropped — notify the SDK.
        self._on_disconnect()
