#!/usr/bin/env bash
# examples/env_monitor/scripts/run_e2e_test.sh
#
# Automated end-to-end test: EmbedIQ env_monitor → Mosquitto → Cloud OSS
#
# What this tests:
#   1. env_monitor binary builds and runs
#   2. Device connects to MQTT broker
#   3. Telemetry messages arrive at the broker (verified with mosquitto_sub)
#   4. Cloud OSS admin API shows device as ONLINE
#   5. Cloud command reaches the device (via mosquitto_pub → MSG_MQTT_CMD_RX)
#
# Prerequisites:
#   - mosquitto and mosquitto-clients installed (apt: mosquitto mosquitto-clients)
#   - python3 + pip installed
#   - cmake + make installed
#   - curl + jq installed
#   - embediq-cloud repo cloned alongside embediq repo
#     (default path: ../embediq-cloud — override with CLOUD_REPO env var)
#
# Usage:
#   bash examples/env_monitor/scripts/run_e2e_test.sh
#
# @author  Ritesh Anand
# @company embediq.com | ritzylab.com
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

# ---------------------------------------------------------------------------
# Configuration — override with environment variables
# ---------------------------------------------------------------------------
DEVICE_ID="${DEVICE_ID:-env-monitor-001}"
DEVICE_PASS="${DEVICE_PASS:-embediq-test-pass}"
MQTT_PORT="${MQTT_PORT:-1883}"
CLOUD_PORT="${CLOUD_PORT:-8080}"
CLOUD_REPO="${CLOUD_REPO:-../embediq-cloud}"
REPO_ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build"
NVM_BLOB="/tmp/embediq_env_monitor_test.bin"
PID_MOSQUITTO=""
PID_CLOUD=""
PID_DEVICE=""
PASS_COUNT=0
FAIL_COUNT=0

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

log()  { echo "[TEST] $*"; }
pass() { echo "[PASS] $*"; PASS_COUNT=$((PASS_COUNT + 1)); }
fail() { echo "[FAIL] $*"; FAIL_COUNT=$((FAIL_COUNT + 1)); }

cleanup() {
    log "Cleaning up..."
    [ -n "$PID_DEVICE"    ] && kill "$PID_DEVICE"    2>/dev/null || true
    [ -n "$PID_CLOUD"     ] && kill "$PID_CLOUD"     2>/dev/null || true
    [ -n "$PID_MOSQUITTO" ] && kill "$PID_MOSQUITTO" 2>/dev/null || true
    rm -f "$NVM_BLOB" /tmp/e2e_fleet.json /tmp/e2e_factory.json
    log "Cleanup done."
}
trap cleanup EXIT

wait_for_port() {
    local host="$1" port="$2" label="$3" attempts="${4:-15}"
    local i=0
    while ! nc -z "$host" "$port" 2>/dev/null; do
        i=$((i + 1))
        if [ "$i" -ge "$attempts" ]; then
            fail "$label did not start on $host:$port after ${attempts}s"
            return 1
        fi
        sleep 1
    done
    pass "$label is up on $host:$port"
}

# ---------------------------------------------------------------------------
# Step 0: Prerequisites
# ---------------------------------------------------------------------------

log "=== Step 0: Prerequisites ==="

for cmd in mosquitto mosquitto_sub mosquitto_pub cmake python3 curl jq nc; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        fail "Required command not found: $cmd"
        echo "  Install: sudo apt-get install mosquitto mosquitto-clients cmake python3 curl jq netcat-openbsd"
        exit 1
    fi
done
pass "All prerequisites found"

if [ ! -d "$CLOUD_REPO" ]; then
    fail "Cloud OSS repo not found at: $CLOUD_REPO"
    echo "  Set CLOUD_REPO env var to the path of embediq-cloud"
    exit 1
fi
pass "Cloud OSS repo found: $CLOUD_REPO"

# ---------------------------------------------------------------------------
# Step 1: Build env_monitor
# ---------------------------------------------------------------------------

log ""
log "=== Step 1: Build env_monitor ==="

cd "$REPO_ROOT"
cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug -DEMBEDIQ_PLATFORM=host \
    -DCMAKE_BUILD_QUIET=ON 2>&1 | tail -3
cmake --build "$BUILD_DIR" --target env_monitor 2>&1 | tail -5

if [ -x "$BUILD_DIR/examples/env_monitor/env_monitor" ]; then
    pass "env_monitor binary built: $BUILD_DIR/examples/env_monitor/env_monitor"
else
    fail "env_monitor binary not found after build"
    exit 1
fi

# ---------------------------------------------------------------------------
# Step 2: Generate device NVM config
# ---------------------------------------------------------------------------

log ""
log "=== Step 2: Generate NVM config ==="

cat > /tmp/e2e_fleet.json << EOF
{
  "mqtt.host": "localhost",
  "mqtt.port": $MQTT_PORT,
  "mqtt.keepalive_sec": 60
}
EOF

cat > /tmp/e2e_factory.json << EOF
{
  "mqtt.client_id": "$DEVICE_ID",
  "device_id":      "$DEVICE_ID"
}
EOF

python3 tools/config_iq/embediq_nvs_gen.py \
    --schema config/config.iq \
    --values /tmp/e2e_fleet.json \
    --device-overrides /tmp/e2e_factory.json \
    --out "$NVM_BLOB"

pass "NVM config blob generated: $NVM_BLOB"

# ---------------------------------------------------------------------------
# Step 3: Start Mosquitto broker
# ---------------------------------------------------------------------------

log ""
log "=== Step 3: Start Mosquitto broker ==="

# Write a minimal conf so we can start on a specific port without -d.
# This avoids pgrep-based PID capture, which is fragile if mosquitto is
# already running (e.g. from dev-with-broker.sh).
MOSQUITTO_CONF="/tmp/embediq-e2e-mosquitto.conf"
printf 'listener %s\nallow_anonymous true\nlog_type error\nlog_type warning\n' \
    "$MQTT_PORT" > "$MOSQUITTO_CONF"
mosquitto -c "$MOSQUITTO_CONF" &
PID_MOSQUITTO=$!

wait_for_port "localhost" "$MQTT_PORT" "Mosquitto"

# ---------------------------------------------------------------------------
# Step 4: Start Cloud OSS API (with MQTT bridge enabled)
# ---------------------------------------------------------------------------

log ""
log "=== Step 4: Start Cloud OSS API ==="

cd "$CLOUD_REPO"

if [ ! -d .venv ]; then
    python3 -m venv .venv
fi
# shellcheck disable=SC1091
source .venv/bin/activate
pip install --quiet -e ".[dev]"

export ADMIN_USER="admin"
ADMIN_PASS_HASH="$(python3 -c 'import bcrypt; print(bcrypt.hashpw(b"admin", bcrypt.gensalt()).decode())')"
export ADMIN_PASS_HASH
JWT_SECRET="$(openssl rand -hex 32)"
export JWT_SECRET
export DB_PATH="/tmp/e2e_cloud_test.db"
export BRIDGE_ENABLED="true"
export MQTT_HOST="localhost"
export MQTT_PORT="$MQTT_PORT"
export INFLUXDB_ENABLED="false"  # no InfluxDB in dev mode — bridge continues gracefully

# Remove stale test DB
rm -f "$DB_PATH"

uvicorn app.main:app --port "$CLOUD_PORT" > /tmp/cloud_api.log 2>&1 &
PID_CLOUD=$!

cd "$REPO_ROOT"
wait_for_port "localhost" "$CLOUD_PORT" "Cloud OSS API"

# ---------------------------------------------------------------------------
# Step 5: Authenticate and register device
# ---------------------------------------------------------------------------

log ""
log "=== Step 5: Register device with Cloud OSS ==="

API="http://localhost:$CLOUD_PORT/api/v1"

TOKEN=$(curl -sf -X POST "$API/auth/login" \
    -H "Content-Type: application/json" \
    -d "{\"username\":\"admin\",\"password\":\"admin\"}" \
    | jq -r '.data.token // .token')

if [ -z "$TOKEN" ] || [ "$TOKEN" = "null" ]; then
    fail "Authentication failed — cannot get JWT token"
    cat /tmp/cloud_api.log | tail -20
    exit 1
fi
pass "Authenticated with Cloud OSS"

HTTP_STATUS=$(curl -sf -o /dev/null -w "%{http_code}" -X POST "$API/devices" \
    -H "Authorization: Bearer $TOKEN" \
    -H "Content-Type: application/json" \
    -d "{\"id\":\"$DEVICE_ID\",\"password\":\"$DEVICE_PASS\"}" || echo "000")

if [ "$HTTP_STATUS" = "201" ] || [ "$HTTP_STATUS" = "409" ]; then
    pass "Device registered (status: $HTTP_STATUS)"
else
    fail "Device registration failed (HTTP $HTTP_STATUS)"
    exit 1
fi

# ---------------------------------------------------------------------------
# Step 6: Start env_monitor device
# ---------------------------------------------------------------------------

log ""
log "=== Step 6: Start env_monitor device ==="

EMBEDIQ_NVM_PATH="$NVM_BLOB" \
"$BUILD_DIR/examples/env_monitor/env_monitor" \
    > /tmp/device.log 2>&1 &
PID_DEVICE=$!

log "env_monitor started (PID $PID_DEVICE), waiting for connection..."
sleep 3

if ! kill -0 "$PID_DEVICE" 2>/dev/null; then
    fail "env_monitor crashed on startup"
    cat /tmp/device.log
    exit 1
fi
pass "env_monitor is running"

# ---------------------------------------------------------------------------
# Step 7: Verify telemetry arrives at MQTT broker
# ---------------------------------------------------------------------------

log ""
log "=== Step 7: Verify telemetry at MQTT broker (60s timeout) ==="

TELEMETRY_TOPIC="embediq/$DEVICE_ID/telemetry"
TELEMETRY_MSG=$(mosquitto_sub -h localhost -p "$MQTT_PORT" \
    -t "$TELEMETRY_TOPIC" -C 1 -W 60 2>/dev/null || true)

if [ -n "$TELEMETRY_MSG" ]; then
    pass "Telemetry received on $TELEMETRY_TOPIC"
    log "  Payload: $TELEMETRY_MSG"
else
    fail "No telemetry received on $TELEMETRY_TOPIC within 60s"
fi

# ---------------------------------------------------------------------------
# Step 8: Verify device appears ONLINE in Cloud API
# ---------------------------------------------------------------------------

log ""
log "=== Step 8: Verify device online in Cloud OSS ==="

sleep 2  # allow bridge to process status message

DEVICE_STATE=$(curl -sf "$API/devices/$DEVICE_ID" \
    -H "Authorization: Bearer $TOKEN" \
    | jq -r '.data.state.online // .state.online // false' 2>/dev/null || echo "false")

if [ "$DEVICE_STATE" = "1" ] || [ "$DEVICE_STATE" = "true" ]; then
    pass "Device is ONLINE in Cloud OSS API"
else
    fail "Device not showing as online (state=$DEVICE_STATE)"
    log "  Device detail: $(curl -sf "$API/devices/$DEVICE_ID" -H "Authorization: Bearer $TOKEN" | jq .)"
fi

# ---------------------------------------------------------------------------
# Step 9: Send cloud command, verify device receives it
# ---------------------------------------------------------------------------

log ""
log "=== Step 9: Send cloud command ==="

CMD_TOPIC="embediq/$DEVICE_ID/cmd"
mosquitto_pub -h localhost -p "$MQTT_PORT" \
    -t "$CMD_TOPIC" \
    -m "alert:25.0"

sleep 2
if grep -q "Alert threshold updated" /tmp/device.log; then
    pass "Device received and processed cloud command (alert threshold updated)"
elif grep -q "Cloud command received" /tmp/device.log; then
    pass "Device received cloud command (MSG_MQTT_CMD_RX)"
else
    fail "No evidence of command receipt in device logs"
    log "  Device log tail:"
    tail -20 /tmp/device.log | sed 's/^/    /'
fi

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------

log ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  E2E TEST SUMMARY"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  PASSED: $PASS_COUNT"
echo "  FAILED: $FAIL_COUNT"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""
echo "  Device logs: /tmp/device.log"
echo "  Cloud logs:  /tmp/cloud_api.log"
echo ""

if [ "$FAIL_COUNT" -gt 0 ]; then
    exit 1
fi
exit 0
