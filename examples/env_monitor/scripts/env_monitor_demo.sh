#!/usr/bin/env bash
# examples/env_monitor/scripts/env_monitor_demo.sh
#
# Interactive developer demo for the EmbedIQ env_monitor example.
#
# Starts a local MQTT broker (or uses one already running), generates
# device NVM config, builds env_monitor if needed, and runs it with a
# live CLI menu for sending commands and viewing telemetry.
#
# No Cloud OSS required — Mosquitto only.
#
# Prerequisites:
#   - cmake, make (firmware build)
#   - python3 (NVM config generator)
#   - mosquitto, mosquitto_sub, mosquitto_pub, mosquitto-clients
#   - nc (netcat) for port checks
#
# Usage:
#   bash examples/env_monitor/scripts/env_monitor_demo.sh
#   DEVICE_ID=my-device MQTT_PORT=1884 bash ...  (override defaults)
#
# @author  Ritesh Anand
# @company embediq.com | ritzylab.com
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
DEVICE_ID="${DEVICE_ID:-env-monitor-001}"
MQTT_PORT="${MQTT_PORT:-1883}"
REPO_ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build"
NVM_BLOB="/tmp/embediq_demo.bin"
DEMO_MOSQUITTO_CONF="/tmp/embediq-demo-mosquitto.conf"
PID_MOSQUITTO=""
PID_DEVICE=""
DEMO_STARTED_MOSQUITTO=false

# ---------------------------------------------------------------------------
# Logging helpers
# ---------------------------------------------------------------------------
log()     { printf '\n[DEMO] %s\n' "$*"; }
step()    { printf '\n\033[1;36m>>> %s\033[0m\n' "$*"; }
ok()      { printf '  \033[0;32m✓\033[0m %s\n' "$*"; }
err()     { printf '  \033[0;31m✗\033[0m %s\n' "$*" >&2; }

# ---------------------------------------------------------------------------
# Cleanup trap
# ---------------------------------------------------------------------------
cleanup() {
    printf '\n[DEMO] Stopping demo...\n'
    [ -n "$PID_DEVICE" ] && kill "$PID_DEVICE" 2>/dev/null || true
    if [ "$DEMO_STARTED_MOSQUITTO" = "true" ] && [ -n "$PID_MOSQUITTO" ]; then
        kill "$PID_MOSQUITTO" 2>/dev/null || true
    fi
    rm -f "$NVM_BLOB" /tmp/demo_fleet.json /tmp/demo_factory.json \
          "$DEMO_MOSQUITTO_CONF" /tmp/device_demo.log
    printf '[DEMO] Done.\n'
}
trap cleanup EXIT

# ---------------------------------------------------------------------------
# Step 1: Prerequisites check
# ---------------------------------------------------------------------------
step "Checking prerequisites..."
MISSING=()
for cmd in cmake python3 mosquitto mosquitto_sub mosquitto_pub nc; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        MISSING+=("$cmd")
    fi
done
if [ "${#MISSING[@]}" -gt 0 ]; then
    err "Missing required command(s): ${MISSING[*]}"
    printf '  Install hints:\n'
    printf '    macOS:  brew install cmake python mosquitto netcat\n'
    printf '    Debian: sudo apt-get install cmake python3 mosquitto mosquitto-clients netcat-openbsd\n'
    exit 1
fi
ok "All prerequisites found"

# ---------------------------------------------------------------------------
# Step 2: Build env_monitor (if needed)
# ---------------------------------------------------------------------------
if [ -x "$BUILD_DIR/examples/env_monitor/env_monitor" ]; then
    log "env_monitor binary found — skipping build."
else
    step "Building env_monitor..."
    cd "$REPO_ROOT"
    cmake -S . -B "$BUILD_DIR" \
        -DCMAKE_BUILD_TYPE=Debug \
        -DEMBEDIQ_PLATFORM=host \
        -DCMAKE_BUILD_QUIET=ON 2>&1 | tail -3
    cmake --build "$BUILD_DIR" --target env_monitor 2>&1 | tail -5

    if [ ! -x "$BUILD_DIR/examples/env_monitor/env_monitor" ]; then
        err "env_monitor binary not found after build"
        exit 1
    fi
    ok "env_monitor built"
fi

# ---------------------------------------------------------------------------
# Step 3: Generate NVM config blob
# ---------------------------------------------------------------------------
step "Generating NVM config (report every 5 s)..."
cat > /tmp/demo_fleet.json << EOF
{
  "mqtt.host":          "127.0.0.1",
  "mqtt.port":          $MQTT_PORT,
  "mqtt.keepalive_sec": 60,
  "report_interval_ms": 5000
}
EOF

cat > /tmp/demo_factory.json << EOF
{
  "mqtt.client_id": "$DEVICE_ID",
  "device_id":      "$DEVICE_ID"
}
EOF

cd "$REPO_ROOT"
python3 tools/config_iq/embediq_nvs_gen.py \
    --schema config/config.iq \
    --values /tmp/demo_fleet.json \
    --device-overrides /tmp/demo_factory.json \
    --out "$NVM_BLOB"

ok "NVM blob: $NVM_BLOB"

# ---------------------------------------------------------------------------
# Step 4: Start MQTT broker (smart — use existing if already up)
# ---------------------------------------------------------------------------
step "MQTT broker..."
if nc -z 127.0.0.1 "$MQTT_PORT" 2>/dev/null; then
    ok "Using existing MQTT broker on 127.0.0.1:$MQTT_PORT"
else
    printf '[DEMO] Starting Mosquitto on port %s...\n' "$MQTT_PORT"
    printf 'listener %s 127.0.0.1\nallow_anonymous true\n' \
        "$MQTT_PORT" > "$DEMO_MOSQUITTO_CONF"
    mosquitto -c "$DEMO_MOSQUITTO_CONF" > /tmp/mosquitto_demo.log 2>&1 &
    PID_MOSQUITTO=$!
    DEMO_STARTED_MOSQUITTO=true
    # Wait up to 5 s for broker to be ready
    i=0
    while ! nc -z 127.0.0.1 "$MQTT_PORT" 2>/dev/null; do
        i=$((i + 1))
        if [ "$i" -ge 5 ]; then
            err "Mosquitto did not start on port $MQTT_PORT"
            exit 1
        fi
        sleep 1
    done
    ok "Mosquitto started (PID $PID_MOSQUITTO)"
fi

# ---------------------------------------------------------------------------
# Step 5: Start env_monitor
# ---------------------------------------------------------------------------
step "Starting env_monitor..."
EMBEDIQ_NVM_PATH="$NVM_BLOB" \
"$BUILD_DIR/examples/env_monitor/env_monitor" \
    > /tmp/device_demo.log 2>&1 &
PID_DEVICE=$!

sleep 2

if ! kill -0 "$PID_DEVICE" 2>/dev/null; then
    err "env_monitor crashed on startup"
    cat /tmp/device_demo.log
    exit 1
fi
ok "env_monitor running (PID $PID_DEVICE)"

# ---------------------------------------------------------------------------
# Step 6: Startup banner
# ---------------------------------------------------------------------------
printf '\n'
printf '╔══════════════════════════════════════════════════════════════╗\n'
printf '║         EmbedIQ env_monitor — Interactive Demo               ║\n'
printf '╠══════════════════════════════════════════════════════════════╣\n'
printf '║  Device ID   : %s\n' "$DEVICE_ID"
printf '║  MQTT broker : 127.0.0.1:%s\n' "$MQTT_PORT"
printf '║  Telemetry   : embediq/%s/telemetry  (every 5 s)\n' "$DEVICE_ID"
printf '║  Commands    : embediq/%s/cmd\n' "$DEVICE_ID"
printf '║  Device log  : /tmp/device_demo.log\n'
printf '╚══════════════════════════════════════════════════════════════╝\n'

# ---------------------------------------------------------------------------
# Step 7: Interactive CLI menu
# ---------------------------------------------------------------------------
# Disable errexit inside the menu so a non-zero return (e.g. read EOF, a
# mosquitto client timing out) does not abort the whole demo.
set +e
while true; do
    printf '\n'
    printf '━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n'
    printf '  Demo Menu\n'
    printf '  [t] Live telemetry  (5 messages, then return)\n'
    printf '  [c] Send command    (set alert threshold)\n'
    printf '  [l] Device log      (last 20 lines)\n'
    printf '  [s] Device status\n'
    printf '  [q] Quit\n'
    printf '━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n'
    printf 'Choice: '
    read -r choice || break

    case "$choice" in
        t)
            printf '\n[Telemetry — waiting for 5 messages, Ctrl+C to stop early]\n'
            mosquitto_sub -h 127.0.0.1 -p "$MQTT_PORT" \
                -t "embediq/$DEVICE_ID/telemetry" -C 5 2>/dev/null || true
            ;;
        c)
            printf 'Alert threshold (°C, e.g. 25.0): '
            read -r threshold
            mosquitto_pub -h 127.0.0.1 -p "$MQTT_PORT" \
                -t "embediq/$DEVICE_ID/cmd" \
                -m "alert:${threshold}"
            printf '  Sent: alert:%s\n' "$threshold"
            ;;
        l)
            printf '\n[Device log — last 20 lines]\n'
            tail -20 /tmp/device_demo.log 2>/dev/null || printf '  (no log yet)\n'
            ;;
        s)
            if kill -0 "$PID_DEVICE" 2>/dev/null; then
                printf '  env_monitor: RUNNING (PID %s)\n' "$PID_DEVICE"
            else
                printf '  env_monitor: STOPPED\n'
            fi
            if nc -z 127.0.0.1 "$MQTT_PORT" 2>/dev/null; then
                printf '  MQTT broker: UP on 127.0.0.1:%s\n' "$MQTT_PORT"
            else
                printf '  MQTT broker: DOWN\n'
            fi
            ;;
        q)
            break
            ;;
        *)
            printf '  Unknown choice: %s\n' "$choice"
            ;;
    esac
done
set -e
