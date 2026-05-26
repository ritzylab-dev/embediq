# telemetry_msgs.py -- Generated Python message stubs
#
# Source:    telemetry.iq
# Namespace: com.embediq.telemetry
# Schema:    version 1
#
# DO NOT EDIT -- regenerate with:
#   python3 tools/messages_iq/generate.py messages/telemetry.iq --lang python --out <output_dir>/
#
# @author  Ritesh Anand
# @company embediq.com | ritzylab.com
#
# SPDX-License-Identifier: Apache-2.0

import struct
from dataclasses import dataclass
from typing import Any

# --- MSG_TELEMETRY_GAUGE (0x0640) ---
MSG_TELEMETRY_GAUGE = 0x0640


@dataclass
class MSG_TELEMETRY_GAUGE_Payload:
    metric_id: int
    value: float
    unit_id: int

    _STRUCT = struct.Struct('<HfB')

    @classmethod
    def unpack(cls, data: bytes) -> 'MSG_TELEMETRY_GAUGE_Payload':
        (metric_id, value, unit_id,) = cls._STRUCT.unpack_from(data)
        return cls(metric_id, value, unit_id)

    def pack(self) -> bytes:
        return self._STRUCT.pack(self.metric_id, self.value, self.unit_id)


# --- MSG_TELEMETRY_COUNTER (0x0641) ---
MSG_TELEMETRY_COUNTER = 0x0641


@dataclass
class MSG_TELEMETRY_COUNTER_Payload:
    metric_id: int
    delta: int
    unit_id: int

    _STRUCT = struct.Struct('<HIB')

    @classmethod
    def unpack(cls, data: bytes) -> 'MSG_TELEMETRY_COUNTER_Payload':
        (metric_id, delta, unit_id,) = cls._STRUCT.unpack_from(data)
        return cls(metric_id, delta, unit_id)

    def pack(self) -> bytes:
        return self._STRUCT.pack(self.metric_id, self.delta, self.unit_id)


# --- MSG_TELEMETRY_HISTOGRAM (0x0642) ---
MSG_TELEMETRY_HISTOGRAM = 0x0642


@dataclass
class MSG_TELEMETRY_HISTOGRAM_Payload:
    metric_id: int
    observation: float
    unit_id: int

    _STRUCT = struct.Struct('<HfB')

    @classmethod
    def unpack(cls, data: bytes) -> 'MSG_TELEMETRY_HISTOGRAM_Payload':
        (metric_id, observation, unit_id,) = cls._STRUCT.unpack_from(data)
        return cls(metric_id, observation, unit_id)

    def pack(self) -> bytes:
        return self._STRUCT.pack(self.metric_id, self.observation, self.unit_id)


# --- MSG_TELEMETRY_BATCH (0x0643) ---
MSG_TELEMETRY_BATCH = 0x0643


@dataclass
class MSG_TELEMETRY_BATCH_Payload:
    window_start_s: int
    window_dur_s: int
    entry_count: int
    flags: int

    _STRUCT = struct.Struct('<IHBB')

    @classmethod
    def unpack(cls, data: bytes) -> 'MSG_TELEMETRY_BATCH_Payload':
        (window_start_s, window_dur_s, entry_count, flags,) = cls._STRUCT.unpack_from(data)
        return cls(window_start_s, window_dur_s, entry_count, flags)

    def pack(self) -> bytes:
        return self._STRUCT.pack(self.window_start_s, self.window_dur_s, self.entry_count, self.flags)


# Registry: maps msg_id -> payload dataclass
# Used by EmbedIQMsg.decode() to return typed objects.
_MSG_REGISTRY = {
    MSG_TELEMETRY_GAUGE: MSG_TELEMETRY_GAUGE_Payload,
    MSG_TELEMETRY_COUNTER: MSG_TELEMETRY_COUNTER_Payload,
    MSG_TELEMETRY_HISTOGRAM: MSG_TELEMETRY_HISTOGRAM_Payload,
    MSG_TELEMETRY_BATCH: MSG_TELEMETRY_BATCH_Payload,
}
