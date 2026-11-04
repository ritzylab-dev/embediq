# EmbedIQ Industrial Edge Gateway Demo

**What an observable, resilient edge gateway looks like from the inside — structured, deterministic, and traceable through the Observatory. No Functional Block prints; only the host program (`main.c`) writes to the console.**

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
- **fb_north_bridge** — sends telemetry north to cloud (simulated: each send is an Observatory event) or buffers locally when offline. Three sub-functions: `north_timer` (connectivity FSM), `north_telemetry` and `north_alert` (one per message type)

Supporting FBs: `fb_timer`, `fb_nvm`, `fb_watchdog` (from `fbs/drivers/`).

---

## Why it matters

- **Protocol-independent architecture** — field protocol (RS-485/Modbus) and cloud protocol (MQTT/TCP) are completely decoupled through the message bus. Change either without touching the other.
- **Offline resilience** — fb_north_bridge buffers telemetry locally during a 10-second connectivity outage and automatically flushes on reconnect. Alerts always go through (never buffered).
- **Observable by default** — every message, every FSM transition, every lifecycle event is captured by the Observatory automatically. fb_north_bridge adds one application-defined event (`APP`) per simulated send, buffer and link change — no print statements.
- **Runs on Linux today** — no hardware required. Same `cmake -B build` command as the thermostat.
- **IEC 62443 SL1/2 ready** — `SECURITY_INCIDENT` (0x61) and `AUTH_EVENT` (0x62) Observatory events are defined for industrial cybersecurity audit trail requirements. The edge gateway pattern is a natural IEC 62443 zone/conduit boundary. See `COMPLIANCE.md §2`.
- **India / AIS-190 alignment** — Industrial edge gateways are a primary deployment target for India's manufacturing and water/energy sectors. The gateway's tamper-evident Observatory output satisfies AIS-190 (aligned ISO 26262 ASIL A/B) audit logging requirements for connected systems. See `COMPLIANCE.md §2`.

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

The demo runs at Observatory level 2 with the STDOUT transport. Every line from
fb_north_bridge is an application-defined event:

```
[  23420154] APP fb_north_bridge → N/A  code=16 val=2310  seq=31    # TELEMETRY Tank_Temp 23.10 sent north
[  27420075] APP fb_north_bridge → N/A  code=81 val=920  seq=75     # ALERT_ON Tank_Pressure value 9.20
[  27420079] APP fb_north_bridge → N/A  code=113 val=850  seq=76    # ALERT_THRESHOLD Tank_Pressure 8.50
[  33420241] APP fb_north_bridge → N/A  code=1 val=12  seq=146      # LINK_DOWN at tick 12
[  33420276] APP fb_north_bridge → N/A  code=48 val=5  seq=148      # BUFFERED Tank_Temp seq 5
[  43420155] APP fb_north_bridge → N/A  code=2 val=22  seq=262      # LINK_RECONNECTING at tick 22
[  44420260] APP fb_north_bridge → N/A  code=4 val=10  seq=292      # FLUSH_COUNT 10 records
[  44420267] APP fb_north_bridge → N/A  code=3 val=23  seq=294      # LINK_RESTORED at tick 23
[  44420268] APP fb_north_bridge → N/A  code=32 val=2350  seq=296   # TELEMETRY_FLUSHED Tank_Temp 23.50
```

`code` is the event code and `val` its value, as defined in `fb_north_bridge.c`.
Per-sensor codes are a base plus the sensor id (0 = Tank_Temp, 1 = Tank_Pressure,
2 = Pump_Flow):

| Code (decimal) | Event | `val` |
| -------------- | ----- | ----- |
| 1 | LINK_DOWN — telemetry is buffered | tick |
| 2 | LINK_RECONNECTING | tick |
| 3 | LINK_RESTORED — buffer flushed | tick |
| 4 | FLUSH_COUNT (just before LINK_RESTORED) | records to flush |
| 16 + sensor | TELEMETRY sent north | value × 100 |
| 32 + sensor | TELEMETRY_FLUSHED after reconnect | value × 100 |
| 48 + sensor | BUFFERED while offline | seq |
| 64 + sensor | BUFFER_FULL_DROP | seq |
| 80 + sensor | ALERT_ON | value × 100 |
| 96 + sensor | ALERT_OFF | value × 100 |
| 112 + sensor | ALERT_THRESHOLD (after ALERT_ON / ALERT_OFF) | threshold × 100 |

Values are fixed-point and saturate at 0 and 65535. The trace tool names the
event `APP` in a capture (`--capture`, then `embediq_obs.py obs decode`).

---

## Integration test

```bash
ctest --test-dir build -R integration_gateway_full -V --timeout 60
```

Boots all 6 FBs on real POSIX threads, runs the full 30-second scenario, exits 0 on success.

---

## AI Event Constants

If your gateway FB wraps an AI inference call (e.g. anomaly detection on field sensor data),
use the Phase-1 AI Observatory constants to satisfy EU AI Act Art.12/13 logging requirements:

| Constant | Value | When to emit |
|---|---|---|
| `EMBEDIQ_OBS_EVT_AI_INFERENCE_START` | `0x17` | Before inference |
| `EMBEDIQ_OBS_EVT_AI_INFERENCE_END` | `0x18` | After inference returns |
| `EMBEDIQ_OBS_EVT_AI_CONFIDENCE_THRESHOLD` | `0x1A` | If confidence drops below threshold |

All events land in the same `.iqtrace` ring buffer as message bus, FSM, and fault events.
No separate AI logging pipeline. See `docs/architecture/AI_FIRST_ARCHITECTURE.md §2`.

## FB Metadata and safety_class

Register metadata for each FB to enable Registry introspection. Set `safety_class` to the
applicable standard. For IEC 62443 SL1/2 industrial deployments:
```c
static const EmbedIQ_FB_Meta_t fb_meta = {
    .name        = "fb_policy_engine",
    .version     = "1.0.0",
    .description = "Threshold policy engine for field sensor alerts.",
    .boot_phase  = EMBEDIQ_BOOT_PHASE_APPLICATION,
    .safety_class = "IEC62443:SL-1",   /* or "IEC62443:SL-2" */
};
```

See `COMPLIANCE.md §5` for all canonical safety_class encodings.

---

*Apache 2.0 · [embediq.com](https://embediq.com) · Built by [Ritesh Anand](https://www.linkedin.com/in/riteshanand101) · [Ritzy Lab](https://ritzylab.com)*
