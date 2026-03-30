# EmbedIQ Industrial Edge Gateway Demo

**What an observable, resilient edge gateway looks like from the inside — structured, deterministic, and traceable without a single printf in application code.**

---

## What it simulates

A water treatment plant edge gateway monitoring three field sensors via a simulated RS-485/Modbus poll cycle:

| Sensor           | Unit      | Alert threshold        |
| ---------------- | --------- | ---------------------- |
| Tank_Temp        | deg C     | > 70.0 (high temp)     |
| Tank_Pressure    | bar       | > 8.5  (overpressure)  |
| Pump_Flow        | L/min     | < 5.0  (dry-run)       |

Three application FBs form a complete edge-to-cloud pipeline:

- **fb_field_receiver** — polls field sensors every second, publishes `MSG_FIELD_READING`
- **fb_policy_engine** — applies threshold rules, publishes `MSG_ALERT` (on state change only, no duplicates) + `MSG_TELEMETRY` (throttled 2:1)
- **fb_north_bridge** — sends telemetry north to cloud (stdout) or buffers locally when offline. First FB in the repo with two sub-functions: `north_timer` (connectivity FSM) and `north_rx` (data handler)

Supporting FBs: `fb_timer`, `fb_nvm`, `fb_watchdog` (from `fbs/drivers/`).

---

## Why it matters

- **Protocol-independent architecture** — field protocol (RS-485/Modbus) and cloud protocol (MQTT/TCP) are completely decoupled through the message bus. Change either without touching the other.
- **Offline resilience** — fb_north_bridge buffers telemetry locally during a 10-second connectivity outage and automatically flushes on reconnect. Alerts always go through (never buffered).
- **Observable by default** — every message, every FSM transition, every lifecycle event is captured by the Observatory. Zero instrumentation code in any application FB.
- **Runs on Linux today** — no hardware required. Same `cmake -B build` command as the thermostat.

---

## Build and run

```bash
cmake -B build -DEMBEDIQ_PLATFORM=host
cmake --build build
./build/examples/gateway/embediq_gateway
```

With Observatory capture:

```bash
./build/examples/gateway/embediq_gateway --capture /tmp/gateway.iqtrace
python3 tools/embediq_obs/embediq_obs.py obs stats /tmp/gateway.iqtrace
```

---

## Scripted events (30-second demo)

| Time    | Event                                           |
| ------- | ----------------------------------------------- |
| t=1-4s  | Normal telemetry flowing north                  |
| t=5s    | Tank_Pressure spikes to 9.2 bar — ALERT fires   |
| t=6-7s  | Pressure sustained high — no duplicate alert     |
| t=8s    | Pressure normalises to 7.5 bar — alert clears   |
| t=12s   | North bridge goes OFFLINE — buffering begins    |
| t=12-21s| Telemetry buffered locally (up to 10 slots)     |
| t=22s   | Link reconnecting                               |
| t=23s   | Link restored — buffer flushed to cloud          |
| t=23s   | Pump_Flow drops to 3.2 L/min — dry-run ALERT    |
| t=24s   | Pump flow still low (3.1 L/min)                 |
| t=25s   | Pump flow recovered (22.0 L/min) — alert clears |
| t=26-29s| Normal operation                                |

---

## How to read the output

```
[-> CLOUD] TELEMETRY  Tank_Temp         value=23.10  seq=0     # Normal: telemetry sent north
[-> CLOUD] ALERT ***  Tank_Pressure     value=9.20  threshold=8.50  ACTIVE   # Alert fires
[-> CLOUD] ALERT clr  Tank_Pressure     value=7.50  threshold=8.50  cleared  # Alert clears

[NORTH] *** Link down at t=12s — buffering telemetry locally ***

[BUFFER] Tank_Temp         seq=5  buffered=1/10     # Buffered during outage
[BUFFER] Tank_Pressure     seq=5  buffered=2/10

[NORTH] Link restored at t=23s — flushing 10 buffered records

  [-> CLOUD buffered] TELEMETRY  Tank_Temp         value=23.50  seq=5   # Flushed on reconnect
```

- `[-> CLOUD]` — sent to cloud (stdout simulation)
- `[BUFFER]` — buffered locally during offline window
- `[-> CLOUD buffered]` — flushed from local buffer after reconnect
- `[NORTH]` — connectivity FSM state changes

---

## Integration test

```bash
ctest --test-dir build -R integration_gateway_full -V --timeout 60
```

Boots all 6 FBs on real POSIX threads, runs the full 30-second scenario, exits 0 on success.

---

*Apache 2.0 · [embediq.com](https://embediq.com) · Built by [Ritesh Anand](https://www.linkedin.com/in/riteshanand101) · [Ritzy Lab](https://ritzylab.com)*
