<!-- SPDX-License-Identifier: Apache-2.0 -->

# Bridge daemon examples — `telemetry_observer.py`

## Overview

A Python External FB demonstrating the **`embediq-python`** SDK end-to-end. It
connects to the EmbedIQ bus through `fb_bridge` over a Unix socket, subscribes
to telemetry messages, and renders a live in-place terminal table that updates
as values arrive. ~80 lines, stdlib-only beyond the SDK.

## Prerequisites

- A running EmbedIQ application that boots `fb_bridge` and a telemetry source.
  The thermostat example (`examples/thermostat`) publishes
  `MSG_TELEMETRY_GAUGE` periodically and is the simplest way to generate live
  data.
- Python ≥ 3.9.
- `fb_bridge`'s Unix socket reachable at the path in `EMBEDIQ_BRIDGE_SOCK`
  (default `/tmp/embediq_bridge.sock`).

## Install the SDK

One-time, editable install — uses your repo checkout directly:

```sh
pip install -e tools/embediq_python
```

This makes `from embediq import ExternalFB` work from any directory.

## Run

Open two terminals.

**Terminal 1 — telemetry source:**

```sh
# Build once if you haven't already
cmake -B build -DEMBEDIQ_PLATFORM=host
cmake --build build

# Boot the thermostat (publishes MSG_TELEMETRY_GAUGE while running)
./build/examples/thermostat/thermostat_main
```

**Terminal 2 — the observer:**

```sh
EMBEDIQ_BRIDGE_SOCK=/tmp/embediq_bridge.sock \
  python3 examples/bridge/telemetry_observer.py
```

Press `Ctrl+C` to exit cleanly.

## Terminal output

The table redraws in place every time a telemetry message arrives:

```
TYPE     METRIC                 VALUE   UNIT    COUNT
─────────────────────────────────────────────────────
G        0x0001                20.500   °C         12
G        0x0002                55.000   %           8
C        0x0010               144        ms         4
  updated 14:32:07
```

| Column   | Meaning                                                          |
| -------- | ---------------------------------------------------------------- |
| `TYPE`   | `G` = gauge (instantaneous), `C` = counter (running total)       |
| `METRIC` | Application-assigned `metric_id` (uint16) as 4-digit hex         |
| `VALUE`  | Latest gauge value (3 decimals) or accumulated counter (integer) |
| `UNIT`   | Decoded from `unit_id` — see `UNIT_LABELS` in the script         |
| `COUNT`  | Number of telemetry messages received for this metric            |

Until the first message arrives, the table shows
`(no data yet — waiting for fb_telemetry...)`.

## Environment variables

| Var                          | Default                       | Meaning                                                  |
| ---------------------------- | ----------------------------- | -------------------------------------------------------- |
| `EMBEDIQ_BRIDGE_SOCK`        | `/tmp/embediq_bridge.sock`    | Unix socket path served by `fb_bridge`                   |
| `EMBEDIQ_BRIDGE_HEARTBEAT_MS`| `5000`                        | Bridge heartbeat interval (server-side)                  |

The SDK reads `EMBEDIQ_BRIDGE_SOCK` automatically inside `connect()`.
See `docs/BRIDGE.md` for the full transport configuration reference.

## How it works

`TelemetryObserver` subclasses `ExternalFB`, calls `subscribe(...)` from
`on_connect()`, and updates a `threading.Lock`-guarded row dict from
`on_message()` — that callback fires on the SDK's recv thread, so the lock
matters. `msg.decode()` returns a typed payload dataclass (`metric_id`,
`value`/`delta`, `unit_id`) because the script calls
`register_msg_registry(TEL_REGISTRY)` at import time. Auto-reconnect is the
SDK base class's job; the observer's `on_disconnect()` just prints a notice.
