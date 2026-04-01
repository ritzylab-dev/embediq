PHASE_COMPLETE: Phase 1 + HAL Refactor + Observability Track
MILESTONE: v0.1.0 — dev branch cleared, pending dev→main promotion
NEXT_SESSION: P2-T1 — FreeRTOS OSAL (osal/freertos/embediq_osal_freertos.c)

CURRENT STATE (as of 2026-03-31):
  Branch: main @ 10f408f
  Tests:  17/17 ctest pass (12 unit + 4 integration + 1 CLI). Python CLI assertions pass.
  Build:  0 errors, 0 warnings.
  E2E:    Full pipeline validated — thermostat + gateway --capture → .iqtrace → CLI decode/stats/filter/export/tail.

WHAT WAS BUILT (complete record):

Phase 0 — Scaffold (PRs #1–#4)
  ✓ Repo scaffold, CMake, CI pipeline
  ✓ Core Layer 1 headers (embediq_fb.h, embediq_obs.h, embediq_sm.h)
  ✓ OSAL + message bus + Observatory headers
  ✓ messages.iq generator + embediq_msg_catalog.h

Phase 1 — Core Engine (PRs #5–#9)
  ✓ OSAL POSIX            — osal/posix/embediq_osal_posix.c
  ✓ FB Engine             — core/src/registry/fb_engine.c
  ✓ Message Bus           — core/src/bus/message_bus.c
  ✓ FSM Engine            — core/src/fsm/fsm_engine.c
  ✓ Observatory           — core/src/observatory/obs.c
  ✓ Dispatch blocking     — blocking OSAL semaphore (no polling)
  ✓ Dispatch shutdown     — embediq_engine_dispatch_shutdown(), pthread_join

Phase 1 Thermostat demo (examples/thermostat/)
  ✓ fb_temp_sensor, fb_temp_controller — application FBs
  ✓ Boots 5 FBs: fb_timer, fb_nvm, fb_watchdog, fb_temp_sensor, fb_temp_controller
  ✓ --capture <file> flag — writes .iqtrace binary capture file
  ✓ FSM: NORMAL → WARNING (75°C) → CRITICAL (85°C) → NORMAL (<70°C)

HAL Refactor (PRs #24–#29)
  ✓ platform/posix/ RETIRED — replaced by fbs/drivers/ + hal/posix/
  ✓ HAL contract headers (core/include/hal/):
      hal_timer.h, hal_flash.h, hal_wdg.h, hal_obs_stream.h (+3 reserved)
  ✓ HAL POSIX implementations (hal/posix/):
      hal_timer_posix.c, hal_flash_posix.c, hal_wdg_posix.c, hal_obs_stream_posix.c
  ✓ Driver FBs (fbs/drivers/):
      fb_timer.c, fb_nvm.c, fb_watchdog.c
  ✓ fbs/services/ directory reserved for Service FBs (Phase 2)

MSG ID Cleanup (PRs #30–#31)
  ✓ MSG_ IDs reconciled with registry
  ✓ Range enforcement added to validator.py
  ✓ tools/validator.py — validates sizing constants + MSG ID ranges + registry

Observability Track (PRs #32–#37)
  ✓ Obs-1: 7-family event taxonomy (SYSTEM/MESSAGE/STATE/RESOURCE/TIMING/FAULT/FUNCTION)
  ✓ Obs-2: EMBEDIQ_TRACE_LEVEL 0–3 compile-time control + per-family flags
  ✓ Obs-3: EmbedIQ_Obs_Session_t (40 bytes, sizeof enforced by CODING_RULES I-15)
  ✓ Obs-4: .iqtrace binary format v1.0 — docs/observability/iqtrace_format.md
  ✓ Obs-5: embediq_obs_capture_begin/end() — binary file transport
  ✓ Obs-6: embediq_obs CLI — decode/stats/filter/export/tail
      tools/embediq_obs/embediq_obs.py  (Apache 2.0 open format, forever)
      tests/cli/test_obs_cli.py         28 assertions, 9 test functions

Final Decision Set v2.0 (PR #73)
  ✓ 11 decisions implemented (A–K): event constants, TLV types, safety_class field,
    libembediq_obs INTERFACE target, SBOM formats, migration patterns, AI-first architecture,
    India market positioning, compliance table (ASIL A/B, SIL 1/2, SL 1/2, IEC 62304 A/B)
  ✓ New files: COMPLIANCE.md, docs/MIGRATION.md, docs/architecture/AI_FIRST_ARCHITECTURE.md
  ✓ iqtrace_format.md promoted to v1.1 (5 new TLV types documented)
  ✓ AI Phase-1 constants: AI_INFERENCE_START/END (0x17/0x18), AI_MODEL_LOAD (0x19),
    AI_CONFIDENCE_THRESHOLD (0x1A) — EU AI Act Art.12/13 compliance
  ✓ AI Code Review Gate documented in AGENTS.md §14
  ✓ safety_class[EMBEDIQ_FB_SAFETY_CLASS_LEN] in EmbedIQ_FB_Meta_t (canonical "STD:LEVEL")

Industrial Gateway Example (PR #73)
  ✓ examples/gateway/ — 6-FB edge-to-cloud pipeline (water treatment plant simulation)
  ✓ fb_field_receiver, fb_policy_engine, fb_north_bridge (2 sub-functions)
  ✓ Offline buffering, deduplication, FSM-driven connectivity
  ✓ integration_gateway_full ctest — 30-second scenario, all assertions pass

Documentation Passes (PRs #38–#41)
  ✓ PR #38: README.md + BUILD_STATUS.md — reflect HAL refactor + Obs track
  ✓ PR #39: AGENTS.md + ARCHITECTURE.md — full repo topology + Observatory API
  ✓ PR #40: ROADMAP.md, COMMERCIAL_BOUNDARY.md, CONTRIBUTING.md,
             CODING_RULES.md, docs/architecture/cli.md
  ✓ PR #41: --capture flag wired, integration_thermostat_capture ctest,
             thermostat README 5-step Observatory walkthrough

LAYER TOPOLOGY (current, authoritative):
  core/include/        — contract headers (embediq_*.h, hal/hal_*.h)
  core/src/            — framework engine (registry, bus, fsm, observatory)
  osal/posix/          — POSIX threading abstraction
  hal/posix/           — POSIX HAL implementations
  fbs/drivers/         — portable Driver FBs (no POSIX headers)
  fbs/services/        — reserved for Service FBs (Phase 2)
  examples/thermostat/ — reference application
  examples/gateway/    — industrial edge gateway reference application (6 FBs)
  tools/               — validator.py, boundary_checker.py, embediq_obs/
  tests/unit/          — 12 C unit test binaries
  tests/integration/   — 3 C integration test binaries (incl. capture E2E)
  tests/cli/           — Python CLI self-test (28 assertions)
  docs/architecture/   — cli.md, lifecycle.md
  docs/observability/  — iqtrace_format.md
  PM/                  — project_state.md, e2e reports (not shipped)

TEST INVENTORY (17 ctest entries):
  cli_obs_tool                    — Python, 28 assertions
  unit_osal_posix                 — OSAL threading
  unit_fb_engine                  — FB registry + lifecycle
  unit_message_bus                — publish/subscribe
  unit_fsm_engine                 — FSM transitions
  unit_observatory                — event emit + ring buffer
  unit_fb_watchdog                — watchdog Driver FB
  unit_fb_timer                   — timer Driver FB
  unit_blocking_dispatch          — semaphore-based dispatch
  unit_dispatch_shutdown          — clean teardown
  unit_fb_nvm                     — NVM Driver FB
  unit_hal_contracts              — HAL contract header validation
  unit_obs_capture                — binary capture API
  integration_thermostat_observable — Observatory events in live run
  integration_thermostat_full     — 5-FB boot + 15s run
  integration_thermostat_capture  — --capture → .iqtrace → CLI decode exit 0
  integration_gateway_full        — 6-FB boot + 30s scenario, offline buffering verified

ARCHITECTURE DECISIONS LOCKED (do not revisit without RFC):
  HAL contract model:
    - HAL contract header in core/include/hal/ must exist before any .c implementation
    - Driver FB must have zero POSIX includes — talks only to HAL contract
    - HAL impl may include POSIX headers freely
    - CODING_RULES I-15: sizeof(EmbedIQ_Obs_Session_t) == 40 (compiler-checked)

  Observatory design:
    - .iqtrace format is Apache 2.0 open format — frozen, never made proprietary
    - embediq_obs CLI is Apache 2.0 open tooling — forever
    - EmbedIQ Studio (commercial) reads open .iqtrace data — never writes it
    - EMBEDIQ_TRACE_LEVEL 0 = off, 1 = faults only, 2 = all events, 3 = verbose

  OTA lifecycle protocol (docs/architecture/lifecycle.md):
    MSG_SYS_OTA_REQUEST  0x0003 — fb_ota → all FBs
    MSG_SYS_OTA_READY    0x0004 — each FB → fb_ota
    MSG_SYS_SHUTDOWN     0x0005 — engine → all FBs
    Timeout: 500 ms forced shutdown regardless.
    FBs without persistent state: no OTA_READY subscription required.

  Queue retain semantics:
    - Messages in queues at shutdown are NOT drained — lost by design.
    - FBs needing durability must flush to NVM before OTA_READY.

KNOWN FUTURE IMPROVEMENTS (not blockers for v0.1.0):
  - Observatory dual transport: write to .iqtrace AND stdout simultaneously
    during a capture session. Currently capture replaces stdout transport.
  - thermostat --capture currently starts AFTER boot — boot LC events
    (seq 0–9) appear on stdout but are not in the .iqtrace file.
    Fix: call embediq_obs_capture_begin before embediq_engine_boot().

PHASE 2 MUST DO (in order):
  [DONE] P2-T5: Industrial gateway example — examples/gateway/ (COMPLETE, PR #73)
  1. P2-T1: FreeRTOS OSAL — osal/freertos/embediq_osal_freertos.c
  2. P2-T2: ESP32 platform target
  3. P2-T3: fb_cloud_mqtt — MQTT 3.1.1 pure C
  4. P2-T4: fb_ota — OTA FSM, dual-bank atomic write
