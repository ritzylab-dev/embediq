# ext_fb.py -- ExternalFB base class — connect, subscribe, publish, run
#
# Base class for any Python process acting as an External FB. Subclass
# ExternalFB, set name, and override on_connect/on_message/on_disconnect.
# Handles socket lifecycle, TLV framing, IDENTIFY handshake, heartbeat, and
# auto-reconnect via _Transport. Zero external dependencies — stdlib only.
#
# @author  Ritesh Anand
# @company embediq.com | ritzylab.com
#
# SPDX-License-Identifier: Apache-2.0

import os
import struct

from embediq._transport import _Transport
from embediq._msg import EmbedIQMsg


class ExternalFB:
    """
    Base class for Python External FBs.

    Usage:
        class MyAgent(ExternalFB):
            name = 'my_agent'
            def on_connect(self):
                self.subscribe(MSG_TELEMETRY_GAUGE)
            def on_message(self, msg):
                print(f'received {msg.msg_id:#06x}')

        agent = MyAgent()
        agent.connect()
        agent.run()   # blocks until disconnect
    """

    name: str = 'unnamed_external_fb'

    # -----------------------------------------------------------------------
    # Public API
    # -----------------------------------------------------------------------

    def connect(self, transport: str = 'unix',
                host: str = None, port: int = None) -> None:
        """
        Open the bridge connection.
        Reads EMBEDIQ_BRIDGE_SOCK for the Unix socket path (default:
        /tmp/embediq_bridge.sock). TCP transport is deferred — Item 6.
        Blocks until ENDPOINT_ASSIGN is received.
        """
        if transport == 'unix':
            sock_path = os.environ.get(
                'EMBEDIQ_BRIDGE_SOCK', '/tmp/embediq_bridge.sock')
        else:
            raise NotImplementedError(
                'TCP transport: deferred to Item 6 per ITEM5_BRIDGE_DESIGN_CONTEXT')

        self._transport = _Transport(
            sock_path=sock_path,
            name=self.name,
            on_msg=self._handle_msg,
            on_connect=self.on_connect,
            on_disconnect=self._handle_disconnect,
        )
        self._transport.connect()

    def disconnect(self) -> None:
        """Close the connection. Idempotent."""
        if hasattr(self, '_transport'):
            self._transport.disconnect()

    def subscribe(self, *msg_ids: int) -> None:
        """Subscribe to one or more msg_ids. Thread-safe."""
        self._transport.subscribe(*msg_ids)

    def publish(self, msg_id: int, payload: bytes = b'',
                target: int = 0xFF, priority: int = 1,
                correlation_id: int = 0) -> None:
        """
        Publish a message onto the bus.
        Packs an EmbedIQ_Msg_t with source_endpoint_id=0 (bridge stamps the
        slot's virtual endpoint id), target=0xFF (broadcast) by default.
        """
        payload64 = payload[:64].ljust(64, b'\x00')
        raw = struct.pack(
            '<HBBHHIIBxxx64s',
            msg_id,
            0,                 # source — fb_bridge overwrites with virtual ep id
            target,
            0,                 # schema_id — caller-set fields go in the payload
            len(payload),
            correlation_id,
            0,                 # timestamp_us — informational only
            priority,
            payload64,
        )
        self._transport.send_msg(raw)

    def run(self) -> None:
        """Block until the recv thread exits (i.e. until disconnect)."""
        if hasattr(self, '_transport') and self._transport._recv_thread:
            self._transport._recv_thread.join()

    def recv(self, timeout_ms: int = 1000):
        """
        Poll-style receive. Returns EmbedIQMsg or None on timeout.
        Alternative to callback-based on_message(). Deferred to a follow-up
        prompt — callback API ships in Item 5b.
        """
        raise NotImplementedError(
            'recv() not yet implemented in Item 5b — use on_message() callback')

    # -----------------------------------------------------------------------
    # Overridable callbacks
    # -----------------------------------------------------------------------

    def on_connect(self) -> None:
        """Called once the IDENTIFY + ENDPOINT_ASSIGN handshake completes."""
        pass

    def on_message(self, msg: EmbedIQMsg) -> None:
        """Called for each subscribed message received from the bus."""
        pass

    def on_disconnect(self) -> None:
        """Called when the connection drops; the SDK will auto-reconnect."""
        pass

    # -----------------------------------------------------------------------
    # Internal — invoked by _Transport
    # -----------------------------------------------------------------------

    def _handle_msg(self, payload: bytes) -> None:
        try:
            msg = EmbedIQMsg.unpack(payload)
        except Exception:
            return   # malformed frame — drop silently
        self.on_message(msg)

    def _handle_disconnect(self) -> None:
        self.on_disconnect()
        # Auto-reconnect; if it fails we let the next disconnect cycle retry.
        try:
            self._transport.reconnect()
        except OSError:
            pass
