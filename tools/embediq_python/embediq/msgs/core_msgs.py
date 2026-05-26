# core_msgs.py -- Generated Python message stubs
#
# Source:    core.iq
# Namespace: com.embediq.core
# Schema:    version 1
#
# DO NOT EDIT -- regenerate with:
#   python3 tools/messages_iq/generate.py messages/core.iq --lang python --out <output_dir>/
#
# @author  Ritesh Anand
# @company embediq.com | ritzylab.com
#
# SPDX-License-Identifier: Apache-2.0

import struct
from dataclasses import dataclass
from typing import Any

# --- MSG_FB_STATE_CHANGE (0x0001) ---
MSG_FB_STATE_CHANGE = 0x0001


@dataclass
class MSG_FB_STATE_CHANGE_Payload:
    fb_id: int
    new_state: int
    prev_state: int
    reason: int

    _STRUCT = struct.Struct('<BBBB')

    @classmethod
    def unpack(cls, data: bytes) -> 'MSG_FB_STATE_CHANGE_Payload':
        (fb_id, new_state, prev_state, reason,) = cls._STRUCT.unpack_from(data)
        return cls(fb_id, new_state, prev_state, reason)

    def pack(self) -> bytes:
        return self._STRUCT.pack(self.fb_id, self.new_state, self.prev_state, self.reason)


# --- MSG_FB_FAULT (0x0002) ---
MSG_FB_FAULT = 0x0002


@dataclass
class MSG_FB_FAULT_Payload:
    fb_id: int
    fault_code: int
    _reserved: int

    _STRUCT = struct.Struct('<BIB')

    @classmethod
    def unpack(cls, data: bytes) -> 'MSG_FB_FAULT_Payload':
        (fb_id, fault_code, _reserved,) = cls._STRUCT.unpack_from(data)
        return cls(fb_id, fault_code, _reserved)

    def pack(self) -> bytes:
        return self._STRUCT.pack(self.fb_id, self.fault_code, self._reserved)


# --- MSG_SYS_OTA_REQUEST (0x0003) ---
MSG_SYS_OTA_REQUEST = 0x0003


@dataclass
class MSG_SYS_OTA_REQUEST_Payload:
    reason: int

    _STRUCT = struct.Struct('<B')

    @classmethod
    def unpack(cls, data: bytes) -> 'MSG_SYS_OTA_REQUEST_Payload':
        (reason,) = cls._STRUCT.unpack_from(data)
        return cls(reason)

    def pack(self) -> bytes:
        return self._STRUCT.pack(self.reason)


# --- MSG_SYS_OTA_READY (0x0004) ---
MSG_SYS_OTA_READY = 0x0004


@dataclass
class MSG_SYS_OTA_READY_Payload:
    fb_id: int

    _STRUCT = struct.Struct('<B')

    @classmethod
    def unpack(cls, data: bytes) -> 'MSG_SYS_OTA_READY_Payload':
        (fb_id,) = cls._STRUCT.unpack_from(data)
        return cls(fb_id)

    def pack(self) -> bytes:
        return self._STRUCT.pack(self.fb_id)


# --- MSG_SYS_SHUTDOWN (0x0005) ---
MSG_SYS_SHUTDOWN = 0x0005


@dataclass
class MSG_SYS_SHUTDOWN_Payload:
    reason: int

    _STRUCT = struct.Struct('<B')

    @classmethod
    def unpack(cls, data: bytes) -> 'MSG_SYS_SHUTDOWN_Payload':
        (reason,) = cls._STRUCT.unpack_from(data)
        return cls(reason)

    def pack(self) -> bytes:
        return self._STRUCT.pack(self.reason)


# --- MSG_SYSTEM_READY (0x0020) ---
MSG_SYSTEM_READY = 0x0020


@dataclass
class MSG_SYSTEM_READY_Payload:
    boot_phase: int

    _STRUCT = struct.Struct('<B')

    @classmethod
    def unpack(cls, data: bytes) -> 'MSG_SYSTEM_READY_Payload':
        (boot_phase,) = cls._STRUCT.unpack_from(data)
        return cls(boot_phase)

    def pack(self) -> bytes:
        return self._STRUCT.pack(self.boot_phase)


# Registry: maps msg_id -> payload dataclass
# Used by EmbedIQMsg.decode() to return typed objects.
_MSG_REGISTRY = {
    MSG_FB_STATE_CHANGE: MSG_FB_STATE_CHANGE_Payload,
    MSG_FB_FAULT: MSG_FB_FAULT_Payload,
    MSG_SYS_OTA_REQUEST: MSG_SYS_OTA_REQUEST_Payload,
    MSG_SYS_OTA_READY: MSG_SYS_OTA_READY_Payload,
    MSG_SYS_SHUTDOWN: MSG_SYS_SHUTDOWN_Payload,
    MSG_SYSTEM_READY: MSG_SYSTEM_READY_Payload,
}
