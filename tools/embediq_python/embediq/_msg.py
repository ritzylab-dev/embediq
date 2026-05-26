# _msg.py -- EmbedIQMsg dataclass and struct pack/unpack
#
# Wraps the EmbedIQ_Msg_t wire format (84 bytes, '<HBBHHIIBxxx64s') in a
# Python dataclass. decode() returns a typed payload dataclass if a generated
# stub is registered for the msg_id; otherwise it returns the raw payload
# bytes truncated to payload_len.
#
# @author  Ritesh Anand
# @company embediq.com | ritzylab.com
#
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import struct
from dataclasses import dataclass
from typing import Any

# Struct format matches core/include/embediq_msg.h exactly:
#   msg_id              uint16  H
#   source_endpoint_id  uint8   B
#   target_endpoint_id  uint8   B
#   schema_id           uint16  H
#   payload_len         uint16  H
#   correlation_id      uint32  I
#   timestamp_us        uint32  I
#   priority            uint8   B
#   _reserved[3]                xxx   (3 padding bytes)
#   payload[64]                 64s
# Total: 2+1+1+2+2+4+4+1+3+64 = 84 bytes (offsetof(payload) == 20).
_MSG_STRUCT = struct.Struct('<HBBHHIIBxxx64s')
assert _MSG_STRUCT.size == 84, f'EmbedIQ_Msg_t must be 84 bytes, got {_MSG_STRUCT.size}'

# Registry populated by importing generated stubs and calling
# register_msg_registry(stub._MSG_REGISTRY). Maps msg_id -> payload dataclass.
_PAYLOAD_REGISTRY: dict = {}


def register_msg_registry(registry: dict) -> None:
    """Merge a generated stub's _MSG_REGISTRY into the global decode() map."""
    _PAYLOAD_REGISTRY.update(registry)


@dataclass
class EmbedIQMsg:
    msg_id:              int
    source_endpoint_id:  int
    target_endpoint_id:  int
    schema_id:           int
    payload_len:         int
    correlation_id:      int
    timestamp_us:        int
    priority:            int
    raw_payload:         bytes  # always exactly 64 bytes after unpack()

    @classmethod
    def unpack(cls, data: bytes) -> 'EmbedIQMsg':
        """Unpack 84 raw bytes into an EmbedIQMsg instance."""
        if len(data) < 84:
            raise ValueError(f'EmbedIQMsg.unpack: need 84 bytes, got {len(data)}')
        (msg_id, src, tgt, schema_id, payload_len,
         corr_id, ts_us, priority, raw_payload) = _MSG_STRUCT.unpack_from(data)
        return cls(
            msg_id=msg_id,
            source_endpoint_id=src,
            target_endpoint_id=tgt,
            schema_id=schema_id,
            payload_len=payload_len,
            correlation_id=corr_id,
            timestamp_us=ts_us,
            priority=priority,
            raw_payload=raw_payload,
        )

    def pack(self) -> bytes:
        """Pack into 84 bytes (EmbedIQ_Msg_t wire format). Pads payload to 64 bytes."""
        payload = self.raw_payload[:64].ljust(64, b'\x00')
        return _MSG_STRUCT.pack(
            self.msg_id,
            self.source_endpoint_id,
            self.target_endpoint_id,
            self.schema_id,
            self.payload_len,
            self.correlation_id,
            self.timestamp_us,
            self.priority,
            payload,
        )

    def decode(self) -> Any:
        """
        Decode the payload into a typed dataclass when a generated stub is
        registered for this msg_id; otherwise return the raw payload bytes
        truncated to payload_len.
        """
        cls = _PAYLOAD_REGISTRY.get(self.msg_id)
        if cls is None:
            return self.raw_payload[:self.payload_len]
        return cls.unpack(self.raw_payload)
