# EmbedIQ Environmental Monitor — Cloud Demo

A simulated environmental sensor node that publishes temperature, humidity and CO2
to an MQTT broker and accepts a command from the cloud. It shows a Functional Block
pipeline from a timer tick to a cloud connection, observable end to end.

<!--
@author  Ritesh Anand
@company embediq.com | ritzylab.com
SPDX-License-Identifier: Apache-2.0
-->

---

## What it does

| FB | Phase | Role |
| -- | ----- | ---- |
| `fb_timer` | PLATFORM | `MSG_TIMER_1SEC` tick source |
| `fb_nvm` | INFRASTRUCTURE | Reads the `mqtt.*` configuration from `EMBEDIQ_NVM_PATH` |
| `fb_watchdog` | INFRASTRUCTURE | Health monitor |
| `fb_telemetry` | INFRASTRUCTURE | Aggregates gauges into telemetry batches |
| `fb_cloud_mqtt` | INFRASTRUCTURE | MQTT transport: publishes telemetry, receives commands |
| `fb_env_sim` | APPLICATION | Simulated sensors (triangle waves) and the cloud command handler |

Every second `fb_env_sim` publishes four gauges:
- temperature, 18–30 °C;
- humidity, 45–65 %;
- CO2, 380–560 ppm;
- an alert level, which is 1 when the temperature is above the threshold (default 27.0 °C).

A command `alert:NN.N` on topic `embediq/<client_id>/cmd` sets a new threshold.

---

## Build and run

```bash
cmake -B build -DEMBEDIQ_PLATFORM=host
cmake --build build --target env_monitor
```

The device reads its broker and identity from an NVM blob:

```bash
cat > /tmp/fleet.json <<'EOF'
{ "mqtt.host": "127.0.0.1", "mqtt.port": 1883, "mqtt.keepalive_sec": 60 }
EOF
cat > /tmp/factory.json <<'EOF'
{ "mqtt.client_id": "env-monitor-001", "device_id": "env-monitor-001" }
EOF
python3 tools/config_iq/embediq_nvs_gen.py --schema config/config.iq \
    --values /tmp/fleet.json --device-overrides /tmp/factory.json \
    --out /tmp/env_monitor.bin

mosquitto -p 1883 &
EMBEDIQ_NVM_PATH=/tmp/env_monitor.bin ./build/examples/env_monitor/env_monitor
```

Send a command from another terminal:

```bash
mosquitto_pub -h 127.0.0.1 -p 1883 -t embediq/env-monitor-001/cmd -m "alert:25.0"
```

Without a broker, or with no `mqtt.client_id`, the demo still runs: the sensors and
their trace are unaffected, and nothing is sent.

`scripts/run_e2e_test.sh` runs the full path: broker, Cloud OSS API, device
registration, telemetry, and a command.

---

## What you see

Only the host program (`main.c`) prints. The Functional Blocks report through the
Observatory. The demo uses the STDOUT transport at level 2, which shows
application-defined events. `fb_env_sim` emits them as `APP` lines:

```
[ 229980746] APP fb_env_sim → N/A  code=1 val=270  seq=14     # READY, threshold 27.0 °C
[ 239665565] APP fb_env_sim → N/A  code=2 val=200  seq=108    # TEMP 20.0 °C
[ 239665639] APP fb_env_sim → N/A  code=3 val=477  seq=109    # HUMIDITY 47.7 %
[ 239665652] APP fb_env_sim → N/A  code=4 val=398  seq=110    # CO2 398 ppm
[ 239665661] APP fb_env_sim → N/A  code=5 val=0  seq=111      # ALERT level 0
[ 261717938] APP fb_env_sim → N/A  code=6 val=10  seq=408     # CMD received, 10 bytes
[ 261717983] APP fb_env_sim → N/A  code=7 val=250  seq=410    # THRESHOLD_SET 25.0 °C
```

| Code | Event | `val` |
| ---- | ----- | ----- |
| 1 | READY — FB initialised | alert threshold, °C × 10 |
| 2 | TEMP (every 10th tick) | temperature, °C × 10 |
| 3 | HUMIDITY (every 10th tick) | humidity, % × 10 |
| 4 | CO2 (every 10th tick) | CO2, ppm |
| 5 | ALERT (every 10th tick) | alert level, 0 or 1 |
| 6 | CMD — cloud command received | command length, bytes (the text is not traced) |
| 7 | THRESHOLD_SET — threshold changed by a command | new threshold, °C × 10 |

The codes are defined in `fb_env_sim.c`. Values are fixed-point and saturate at 0
and 65535. An event carries one code and one 16-bit value, so a reading of several
values is several events.
