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
