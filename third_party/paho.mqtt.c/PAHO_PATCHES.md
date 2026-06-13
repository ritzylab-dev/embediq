# Paho MQTT C 1.3.13 — Applied Patches

This file documents patches applied to the vendored Paho MQTT C source under the
EmbedIQ project's D-LIB-3 exception policy.

D-LIB-3 (CODING_RULES.md): "Third-party source in `third_party/` is never modified."
An exception is granted only when: (a) the issue is a genuine platform bug with no
external workaround, and (b) the patch is minimal, documented, and approved by the PM.

---

## Patch 1 — TCPSOCKET_INTERRUPTED in synchronous-connect path (macOS)

**File:** `src/MQTTProtocolOut.c`
**Paho version:** 1.3.13
**Date applied:** 2026-06-12
**PR:** fix/pr160-mqtt-connect-eagain

### Problem

On macOS, `connect()` to 127.0.0.1 on a non-blocking socket can return 0 synchronously
(the kernel completes the loopback connection inline). `Socket.c` documents this:
`/* this could complete immediately, even though we are non-blocking */`.

When this happens, `MQTTProtocol_connect()` immediately calls `MQTTPacket_send_connect()`.
The first `writev()` on the freshly-connected socket returns `EAGAIN` — macOS sockets are
not immediately write-ready even after a synchronous loopback `connect()`. `Socket_writev()`
maps EAGAIN to `TCPSOCKET_INTERRUPTED = -22`. `Socket_putdatas()` stores the pending write
bytes in `SocketBuffer` (to be flushed by the dispatch thread) and returns -22.

The unpatched code at lines 347–350 treats any non-zero return from `MQTTPacket_send_connect()`
as a fatal error:

```c
if ((rc = MQTTPacket_send_connect(...)) == 0)
    aClient->connect_state = WAIT_FOR_CONNACK;
else
    aClient->connect_state = NOT_IN_PROGRESS;  /* bug: -22 is not fatal */
```

`connect_state = NOT_IN_PROGRESS` causes `connectURIVersion()` to immediately exit with
`SOCKET_ERROR = -1`. `MQTTClient_disconnect1()` closes the socket, discarding the pending
CONNECT write. Mosquitto sees TCP connect + immediate FIN. No MQTT CONNECT ever arrives.

### Fix

Added `else if (rc == TCPSOCKET_INTERRUPTED)` branch that sets `connect_state = WAIT_FOR_CONNACK`
and returns `rc = 0`. The dispatch thread flushes the pending write via `Socket_continueWrites()`
at its next poll cycle. `connectURIVersion()` proceeds to wait for CONNACK normally.

### Why no external workaround

The `SOCKET` fd is not accessible from outside `MQTTClient_connect()` — Paho does not
expose it until after `MQTTClient_connect()` returns. There is no way to pre-warm the
send buffer or intercept the write failure without modifying the library.

### Upstream status

Not yet reported. The fix is correct for Paho 1.3.13 and is likely applicable to later
versions. If the embedded project upgrades Paho, verify this patch is no longer needed
or re-apply it.

---

## Patch 2 — EPROTOTYPE mapping in Socket_writev (macOS)

**File:** `src/Socket.c`
**Paho version:** 1.3.13
**Date applied:** 2026-06-12
**PR:** fix/pr161-mqtt-eprototype

### Problem

PR #160 (Patch 1 in MQTTProtocolOut.c) fixed the TCPSOCKET_INTERRUPTED path so that
a pending-write result from `MQTTPacket_send_connect()` sets WAIT_FOR_CONNACK instead
of NOT_IN_PROGRESS. That patch handles errno EAGAIN (= 35 on macOS).

However, on macOS the first `writev()` after a synchronous non-blocking loopback
`connect()` can return errno EPROTOTYPE (= 41, "Protocol wrong type for socket")
instead of EAGAIN. This is a documented macOS kernel quirk — the kernel has not
fully transitioned the socket from SYN_SENT to ESTABLISHED state at the time of
the first write attempt.

`Socket_writev()` (lines 760–766) mapped only `EWOULDBLOCK` (= 35) and `EAGAIN`
(= 35 on macOS, same value) to `TCPSOCKET_INTERRUPTED`. EPROTOTYPE = 41 was not
in the list, so `writev()` failure with errno 41 propagated as `SOCKET_ERROR`.
This caused `MQTTPacket_send_connect()` to return `SOCKET_ERROR`, bypassing the
`else if (rc == TCPSOCKET_INTERRUPTED)` branch added by Patch 1, and landing on
the `else` branch → `connect_state = NOT_IN_PROGRESS` → immediate fatal exit.
Mosquitto saw TCP connect + FIN with no MQTT CONNECT packet.

### Fix

Added `else if (err == EPROTOTYPE)` under `#if defined(__APPLE__)` guard in
`Socket_writev()`. Maps EPROTOTYPE → TCPSOCKET_INTERRUPTED on macOS. Linux and
Windows compilation are unaffected (EPROTOTYPE is not defined on Linux; the Windows
path is in a separate `#if defined(_WIN32) || defined(_WIN64)` block).

After this fix, the EPROTOTYPE path flows through:
1. `Socket_writev()` → TCPSOCKET_INTERRUPTED
2. `Socket_putdatas()` → stores pending write in SocketBuffer → returns TCPSOCKET_INTERRUPTED
3. `MQTTPacket_send_connect()` → returns TCPSOCKET_INTERRUPTED
4. `MQTTProtocolOut.c` (Patch 1) → `else if (rc == TCPSOCKET_INTERRUPTED)` →
   `connect_state = WAIT_FOR_CONNACK`, `rc = 0`
5. Dispatch thread flushes pending write via `Socket_continueWrites()`
6. Mosquitto receives CONNECT, sends CONNACK → `MQTTClient_connect()` succeeds

### Note on Socket_error() logging

`Socket_error()` (Socket.c line 120) logs EPROTOTYPE at `TRACE_MINIMUM` level:
`"Socket error Protocol wrong type for socket(41) in writev - putdatas for socket N"`.
This log message will appear in Debug builds (if Paho trace callback is active) but
does NOT indicate a fatal error — the error is now handled by the EPROTOTYPE branch.

### Why no external workaround

The `SOCKET` fd is not accessible from outside `MQTTClient_connect()`. There is no
way to intercept or suppress the EPROTOTYPE failure without modifying the library.

### Upstream status

Not yet reported. Fix is correct for Paho 1.3.13. If the project upgrades Paho,
verify this patch is still needed or re-apply.
