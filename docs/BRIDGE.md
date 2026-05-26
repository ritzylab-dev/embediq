<!-- docs/BRIDGE.md — Bridge daemon user guide -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# EmbedIQ Bridge Daemon

The Bridge Daemon (`fb_bridge`) lets processes and threads outside the EmbedIQ
C framework join the message bus as first-class participants — **External FBs**.
A Python or Node.js process on Linux, an RTOS brownfield task, or an AI
inference worker all use the same wire protocol and appear identically to
internal FBs.

## 1. Overview

`fb_bridge` is a Service FB (`boot_phase = EMBEDIQ_BOOT_PHASE_BRIDGE = 4`). It
sits in front of a small set of External-FB slots and routes messages between
them and the internal bus. Architecturally, the bridge stack is three layers:

| Layer | What lives here | Where it's implemented |
| ----- | --------------- | ---------------------- |
| **3 — Language SDKs** | `embediq-python`, `embediq-c-api`, future `embediq-node` | `tools/embediq_python/`, `core/include/embediq_ext_fb.h` |
| **2 — External FB Protocol** | `subscribe` · `publish` · `recv` · `identify` · `health` | Stable contract — same five operations on every transport |
| **1 — Transport** | Socket (POSIX) and Queue (OSAL) — compile-time selected | `fbs/bridge/fb_bridge.c`, `core/src/bridge/embediq_ext_fb.c` |

Use the bridge when the code you're integrating lives in a different process
or runtime: an analytics agent in Python, a UI in Node, a brownfield FreeRTOS
task that won't be refactored into a full FB. Internal application FBs continue
to use the bus directly — the bridge is for the boundary, not for general
intra-application messaging.

## 2. Transport configuration

The Unix socket transport is the default on Linux and needs no setup. Override
via environment variables:

| Env var | Default | Meaning |
| ------- | ------- | ------- |
| `EMBEDIQ_BRIDGE_SOCK` | `/tmp/embediq_bridge.sock` | Unix socket path. Empty string disables the listener. |
| `EMBEDIQ_BRIDGE_PORT` | _(unset = TCP off)_ | TCP port. **Deferred to Item 6 — read but no TCP socket is opened in this release.** |
| `EMBEDIQ_BRIDGE_HOST` | `127.0.0.1` | TCP bind address. `0.0.0.0` for remote access. _(Reserved; effective once TCP transport ships.)_ |
| `EMBEDIQ_BRIDGE_HEARTBEAT_MS` | `5000` | Heartbeat interval; three missed `HEALTH_RSP` → disconnect. |

The queue transport is selected at compile time via
`-DEMBEDIQ_BRIDGE_TRANSPORT_QUEUE` on the `embediq_bridge` library. It has no
environment variables — RTOS targets have no environment. Per-direction depth
is set by `EMBEDIQ_BRIDGE_QUEUE_DEPTH` in `core/include/embediq_config.h`
(default: 32 messages).

## 3. Python SDK quickstart

The `embediq-python` SDK ships in `tools/embediq_python/`. Zero external
dependencies — `struct`, `socket`, `threading`, `os`, `time`, `dataclasses`
only. A minimal External FB:

```python
from embediq import ExternalFB
from embediq.msgs.telemetry_msgs import MSG_TELEMETRY_GAUGE

class MyAgent(ExternalFB):
    name = "my_agent"

    def on_connect(self):
        self.subscribe(MSG_TELEMETRY_GAUGE)

    def on_message(self, msg):
        if msg.msg_id == MSG_TELEMETRY_GAUGE:
            gauge = msg.decode()        # typed dataclass from generated stub
            print(f"gauge: metric={gauge.metric_id} value={gauge.value}")

agent = MyAgent()
agent.connect()        # reads EMBEDIQ_BRIDGE_SOCK
agent.run()            # blocks until disconnect (Ctrl+C to exit)
```

Override `on_connect`, `on_message`, `on_disconnect` as needed. `subscribe()`
is callable from `on_connect` or anytime after, and may be called multiple
times. The SDK responds to `HEALTH_REQ` frames automatically and reconnects
statelessly on socket drop — `on_disconnect` is called, the SDK reconnects,
re-sends `IDENTIFY` and all previously-registered subscriptions, then calls
`on_connect` again.

To publish from an External FB:

```python
agent.publish(msg_id=MSG_LED_CMD, payload=bytes([1, 0xFF]))
```

`fb_bridge` stamps the source endpoint (`0x40-0x7F` virtual range) before the
message hits the bus. Internal FBs receive it identically to one published by
an internal FB — no per-source code paths required.

## 4. Health mechanisms

There is no dedicated `/health` HTTP port — each consumer talks to the bridge
through the interface it already speaks:

- **Bridge protocol `HEALTH_REQ` / `HEALTH_RSP`** — `fb_bridge` sends
  `HEALTH_REQ` on every `EMBEDIQ_BRIDGE_HEARTBEAT_MS` interval. The Python SDK
  responds automatically; a missed response (three times in a row) closes the
  connection and emits `EMBEDIQ_OBS_EVT_EXTFB_DISCONNECTED` with reason 1
  (timeout). Use this layer to detect a hung External FB.
- **systemd watchdog** — set `WatchdogSec=` in your `.service` file. If the
  unit also defines `NOTIFY_SOCKET`, `fb_bridge` sends `WATCHDOG=1` every
  `WATCHDOG_USEC/2` microseconds using the raw sd_notify protocol. No
  `libsystemd` dependency.
- **CLI health command (Item 7)** — `embediq bridge health` will connect to
  the Unix socket, send `HEALTH_REQ`, print JSON to stdout, and exit 0 (ok)
  or 1 (unreachable). Useful for `HEALTHCHECK CMD embediq bridge health`
  in a Dockerfile. Ships in a later item.

## 5. C External FB API

For in-process or RTOS clients, `core/include/embediq_ext_fb.h` declares five
functions:

```c
embediq_ext_fb_t *embediq_ext_fb_init(const char *name);
embediq_err_t     embediq_ext_fb_subscribe(embediq_ext_fb_t *h,
                                            const uint16_t *ids, uint8_t n);
embediq_err_t     embediq_ext_fb_publish(embediq_ext_fb_t *h,
                                          const EmbedIQ_Msg_t *msg);
embediq_err_t     embediq_ext_fb_recv(embediq_ext_fb_t *h,
                                       EmbedIQ_Msg_t *out, uint32_t timeout_ms);
void              embediq_ext_fb_deinit(embediq_ext_fb_t *h);
```

Build with `-DEMBEDIQ_BRIDGE_TRANSPORT_SOCKET` on Linux or
`-DEMBEDIQ_BRIDGE_TRANSPORT_QUEUE` on RTOS targets. Pool capacity
(`EMBEDIQ_BRIDGE_MAX_EXT_FBS`, default 8) and queue depth
(`EMBEDIQ_BRIDGE_QUEUE_DEPTH`, default 32) are compile-time constants from
`embediq_config.h`. R-02 holds: the pool is static; no `malloc` is added by
the bridge layer itself.

## 6. Deferred / future

- **TCP transport.** Item 5 reads `EMBEDIQ_BRIDGE_PORT` and
  `EMBEDIQ_BRIDGE_HOST` for forward compatibility but does not open the TCP
  socket. Remote access without authentication would be unsafe; both TCP and
  authentication ship together in **Item 6**.
- **Async Python wrapper.** The synchronous (threading-based) API ships in
  Item 5. An `asyncio` wrapper is a thin layer above the same transport and
  protocol — deferred to **Item 6**.
- **Direct Observatory emission from External FBs.** A future bridge frame
  type will let External FBs submit Observatory events through `fb_bridge`,
  using the AI band (`0x80–0x8F`) and vendor band (`0x90–0xFF`). Requires
  the **Phase 3** AI event taxonomy.
- **Authentication.** Anything that can reach the Unix socket path can
  currently connect. mTLS, token, or challenge-response auth lands with the
  TCP transport in **Phase 3**.
