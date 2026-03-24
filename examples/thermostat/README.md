# Smart Thermostat Demo

This is the Phase 1 gate demo for EmbedIQ. Five Functional Blocks work together — from a hardware timer tick through application FSM logic — all observable without a single printf in application code. It runs entirely on Mac or Linux. No hardware required.

## What it does

- **fb_timer** fires `MSG_TIMER_1SEC` every second
- **fb_temp_sensor** receives each tick and simulates temperature rising from 20°C to 90°C (+5°C/tick), then falling back to 20°C
- **fb_temp_controller** watches temperature and drives a thermal FSM: NORMAL → WARNING (>75°C) → CRITICAL (>85°C) → NORMAL (<70°C)
- **fb_watchdog** monitors health — fb_temp_controller must check in every 5 seconds or a FAULT fires
- **fb_nvm** provides persistent key-value storage, available to all FBs

The Observatory captures every lifecycle event, FSM transition, and fault automatically — zero printf in any application FB.

## Build and run
```bash
cmake -B build -DEMBEDIQ_PLATFORM=host
cmake --build build
./build/examples/thermostat/embediq_thermostat
```

The demo runs for 30 seconds then exits.

## Reading the Observatory output

Every line follows this format:
[timestamp_us] TYPE  source → target   detail   seq=N

| Field | Meaning |
|-------|---------|
| `timestamp_us` | Microseconds since system boot. uint32 — wraps at ~71 min. |
| `TYPE` | `LC` = lifecycle, `FSM` = state transition, `TX` = message sent, `FLT` = fault, `DRP` = queue drop |
| `source → target` | Which FB emitted the event. `N/A` = no specific target. |
| `seq=N` | Monotonic sequence counter. A gap means the Observatory ring buffer overflowed. |

## Expected output — annotated
EmbedIQ Smart Thermostat Demo — Phase 1
// Boot phase PLATFORM: fb_timer starts first
[xxxxxxxxx] LC  fb_timer → N/A  INITIALISING  seq=0
[xxxxxxxxx] LC  fb_timer → N/A  RUNNING  seq=1
// Boot phase INFRASTRUCTURE: fb_nvm and fb_watchdog start next
[xxxxxxxxx] LC  fb_nvm → N/A  INITIALISING  seq=2
[xxxxxxxxx] LC  fb_nvm → N/A  RUNNING  seq=3
[xxxxxxxxx] LC  fb_watchdog → N/A  INITIALISING  seq=4
[xxxxxxxxx] LC  fb_watchdog → N/A  RUNNING  seq=5
// Boot phase APPLICATION: application FBs start last
[xxxxxxxxx] LC  fb_temp_sensor → N/A  INITIALISING  seq=6
[xxxxxxxxx] LC  fb_temp_sensor → N/A  RUNNING  seq=7
[xxxxxxxxx] LC  fb_temp_controller → N/A  INITIALISING  seq=8
[xxxxxxxxx] LC  fb_temp_controller → N/A  RUNNING  seq=9
[MAIN] All FBs initialised. Running demo (30 s)...
// Watchdog confirms all FBs are healthy — one log line per second
[WDG] WDT kick [every-1s count=1]
...
[WDG] WDT kick [every-1s count=11]
// ~12s: temperature reaches 80°C — FSM transitions NORMAL → WARNING
[xxxxxxxxx] FSM state_0 → state_1  msg=0x0420  seq=11
// ~14s: temperature reaches 90°C — FSM transitions WARNING → CRITICAL
[xxxxxxxxx] FSM state_1 → state_2  msg=0x0420  seq=13
// ~19s: temperature falls to 65°C — FSM transitions CRITICAL → NORMAL
[xxxxxxxxx] FSM state_2 → state_0  msg=0x0420  seq=15
[MAIN] Demo complete.

## What to verify

- [ ] All 5 FBs reached `RUNNING` in correct order: PLATFORM → INFRASTRUCTURE → APPLICATION
- [ ] FSM transitioned `NORMAL → WARNING` at approximately 12 seconds
- [ ] FSM transitioned `WARNING → CRITICAL` at approximately 14 seconds
- [ ] FSM transitioned `CRITICAL → NORMAL` at approximately 19 seconds
- [ ] Watchdog never reported a missed checkin (no `FAULT` from `fb_watchdog`)
- [ ] All `seq=` numbers are gapless — no Observatory ring buffer overflow

## Run the automated test
```bash
./build/tests/integration/test_thermostat_full
```

Expected: `All N tests passed. (0 failed)`

Runs the same scenario for 15 seconds and asserts all conditions programmatically.

## Observatory: Capturing a .iqtrace Session

The `--capture` flag writes a binary `.iqtrace` file that can be decoded,
filtered, and exported with the open-source CLI.

**Step 1 — Run the thermostat with capture enabled:**
```bash
./build/examples/thermostat/embediq_thermostat --capture /tmp/thermo.iqtrace
```

**Step 2 — Decode and inspect events:**
```bash
python3 tools/embediq_obs/embediq_obs.py obs decode /tmp/thermo.iqtrace
```

**Step 3 — Show event statistics:**
```bash
python3 tools/embediq_obs/embediq_obs.py obs stats /tmp/thermo.iqtrace
```

**Step 4 — Filter to lifecycle events only:**
```bash
python3 tools/embediq_obs/embediq_obs.py obs filter /tmp/thermo.iqtrace --family STATE
```

**Step 5 — Export to JSON:**
```bash
python3 tools/embediq_obs/embediq_obs.py obs export /tmp/thermo.iqtrace
```

The `.iqtrace` format is open (Apache 2.0) and fully documented at
`docs/observability/iqtrace_format.md`.
