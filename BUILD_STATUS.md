# BUILD_STATUS.md — EmbedIQ Implementation Status
# Last updated: Obs-6 complete (all pre-hardware tasks done)
# Rule: update this file at every phase boundary. Never let it go stale.

## What Exists vs What Does Not

| Module                          | Status      | Location                                   | Notes                                                    |
| ------------------------------- | ----------- | ------------------------------------------ | -------------------------------------------------------- |
| Core headers (all contracts)    | STABLE      | core/include/                              | Contracts frozen. v1 API enforced by fb_v1_compat.c      |
| HAL contract headers (7)        | STABLE      | core/include/hal/                          | hal_flash, hal_gpio, hal_i2c, hal_spi, hal_timer, hal_uart, hal_wdg |
| embediq_config.h                | STABLE      | core/include/                              | All sizing constants. Named constants only.              |
| messages.iq generator           | STABLE      | tools/messages_iq/                         | Python, zero deps. core.iq + thermostat.iq live.         |
| messages_registry.json          | STABLE      | messages_registry.json                     | 13 active ID allocations, range enforcement in validator |
| validator.py                    | STABLE      | tools/validator.py                         | Catches hardcoded sizes + namespace range violations     |
| boundary_checker.py             | STABLE      | tools/boundary_checker.py                  | Enforces layer rules for hal/, fbs/drivers/, fbs/services/ |
| CMake build system              | STABLE      | CMakeLists.txt                             | host + test targets, pre-build generation step           |
| CI pipeline                     | STABLE      | .github/workflows/ci.yml                   | build + validator + compat shim + boundary checker       |
| fb_v1_compat.c                  | STABLE      | tests/compat/                              | Compile-time API contract freeze enforcement             |
| OSAL posix                      | STABLE      | osal/posix/                                | pthreads + POSIX. Blocking semaphore dispatch.           |
| OSAL freertos                   | NOT_STARTED | osal/freertos/                             | Phase 2                                                  |
| FB Registry + Dispatch          | STABLE      | core/src/registry/                         | embediq_engine_boot() + dispatch_shutdown()              |
| Message Bus                     | STABLE      | core/src/bus/                              | 3-queue routing, overflow policy, observatory drops      |
| FSM Engine                      | STABLE      | core/src/fsm/                              | Table-driven, guard/action, observatory events           |
| Observatory — core              | STABLE      | core/src/observatory/obs.c                 | Ring buffer, 7-family event taxonomy, session management |
| Observatory — event families    | STABLE      | core/include/embediq_obs.h                 | 7 families, band encoding, EMBEDIQ_OBS_EVT_FAMILY() macro |
| Observatory — trace levels      | STABLE      | core/include/embediq_config.h              | EMBEDIQ_TRACE_LEVEL 0–3, per-family compile-time flags   |
| Observatory — emit macros       | STABLE      | core/include/embediq_obs.h                 | EMBEDIQ_OBS_EMIT_* compile to (void)0 when disabled     |
| Observatory — session API       | STABLE      | core/include/embediq_obs.h                 | EmbedIQ_Obs_Session_t 40B (I-14), session_begin/get      |
| Observatory — HAL stream        | STABLE      | core/include/hal/hal_obs_stream.h          | HAL contract for binary stream output                    |
| Observatory — POSIX stream impl | STABLE      | hal/posix/hal_obs_stream_posix.c           | fopen/fwrite/fflush/fclose, zero framework deps          |
| Observatory — .iqtrace capture  | STABLE      | core/src/observatory/obs.c                 | 8B header + SESSION TLV + EVENT TLVs + STREAM_END        |
| Observatory — format spec       | STABLE      | docs/observability/iqtrace_format.md       | Open spec v1.1, Apache 2.0, TLV-framed                   |
| Observatory — CLI               | STABLE      | tools/embediq_obs/embediq_obs.py           | decode/stats/filter/export/tail — reads any .iqtrace     |
| fb_timer (Driver FB)            | STABLE      | fbs/drivers/fb_timer.c                     | Portable. HAL-backed. Drift-corrected tick messages.     |
| fb_timer (POSIX HAL)            | STABLE      | hal/posix/hal_timer_posix.c                | POSIX clock_gettime implementation                       |
| fb_nvm (Driver FB)              | STABLE      | fbs/drivers/fb_nvm.c                       | Portable. HAL-backed. Atomic key-value store.            |
| fb_nvm (POSIX HAL)              | STABLE      | hal/posix/hal_flash_posix.c                | Binary file-backed flash implementation                  |
| fb_watchdog (Driver FB)         | STABLE      | fbs/drivers/fb_watchdog.c                  | Portable. HAL-backed. Health-token model.                |
| fb_watchdog (POSIX HAL)         | STABLE      | hal/posix/hal_wdg_posix.c                  | POSIX signal-based watchdog implementation               |
| platform/posix/                 | RETIRED     | platform/posix/.gitkeep                    | Replaced by fbs/drivers/ + hal/posix/ in PRs #27–#29    |
| fb_cloud_mqtt                   | NOT_STARTED | fbs/services/                              | Phase 2                                                  |
| fb_ota                          | NOT_STARTED | fbs/services/                              | Phase 2                                                  |
| Examples — thermostat           | STABLE      | examples/thermostat/                       | 5 FBs, FSM cycles, Observatory output, zero printf       |
| Examples — gateway              | STABLE      | examples/gateway/                          | 6 FBs, edge-to-cloud pipeline, offline resilience, Observatory, zero printf |
| Test Runner                     | STABLE      | tests/                                     | 17 ctest entries: 12 unit + 4 integration (incl. gateway) + 1 CLI. 100%.  |

## Phase 0 — COMPLETE
  ✓ P0-T1: Repo scaffold, CMake, CI pipeline
  ✓ P0-T2: Core FB/SubFn/FSM headers
  ✓ P0-T3: OSAL/Bus/Observatory/Msg headers
  ✓ P0-T4: messages.iq generator + core.iq schema
  ✓ P0-T5: Full integration verify — all checks green

## Phase 1 — COMPLETE
  ✓ P1-T1: OSAL POSIX — osal/posix/
  ✓ P1-T2: FB Engine — core/src/registry/
  ✓ P1-T3: Message Bus — core/src/bus/
  ✓ P1-T4: Sub-fn Dispatcher + FSM Engine
  ✓ P1-T5: Observatory — ring buffer, stdout transport
  ✓ P1-T6: Platform FBs (original) — fb_timer, fb_nvm, fb_watchdog
  ✓ P1-T7: thermostat_msgs.h + message namespace (0x0420)
  ✓ P1-T8: Thermostat integration — 5-FB demo, 6 assertions pass

## Phase 2 Pre-Hardware — COMPLETE
  ✓ P2-T0:  Blocking OSAL semaphore dispatch (replaces 1ms polling)
  ✓ P2-T0b: embediq_engine_dispatch_shutdown() — clean teardown
  ✓ Arch-2: HAL contract headers (7) — core/include/hal/
  ✓ Arch-3: boundary_checker.py extended for hal/ + fbs/ layers
  ✓ Arch-3b: fbs/drivers/ + fbs/services/ directories
  ✓ Arch-4a: fb_timer → fbs/drivers/ + hal/posix/hal_timer_posix.c (PR #27)
  ✓ Arch-4b: fb_nvm → fbs/drivers/ + hal/posix/hal_flash_posix.c (PR #28)
  ✓ Arch-4c: fb_watchdog → fbs/drivers/ + hal/posix/hal_wdg_posix.c (PR #29)
  ✓ Cleanup-2: MSG_ ID namespace reconciliation + validator range enforcement (PR #30)
  ✓ Cleanup-4: messages_registry.json populated, 13 entries (PR #31)

## Observability Track — COMPLETE (all pre-hardware obs work done)
  ✓ Obs-0: Audit — all call sites use named constants, renumbering safe
  ✓ Obs-1: Event type family bands + EMBEDIQ_OBS_EVT_FAMILY() macro (PR #32)
  ✓ Obs-2: EMBEDIQ_TRACE_LEVEL + per-family compile-time flags (PR #33)
  ✓ Obs-3: EmbedIQ_Obs_Session_t (40B) + session_begin/get API (PR #34)
  ✓ Obs-4: docs/observability/iqtrace_format.md v1.0 open spec (PR #35)
  ✓ Obs-5: hal_obs_stream.h + hal_obs_stream_posix.c + binary capture in obs.c (PR #36)
  ✓ Obs-6: tools/embediq_obs/embediq_obs.py CLI + tests/cli/test_obs_cli.py (PR #37)

## Final Decision Set v2.0 (PR #73) — COMPLETE
  ✓ Decision A: Frozen struct invariants verified — no regressions
  ✓ Decision B: 14 new event constants across SYSTEM (0x11–0x1D), FAULT (0x61–0x63), FUNCTION (0x70–0x71) bands
  ✓ Decision C: 5 new TLV types — HASH_CHAIN (0x0005), COMPLIANCE_EVENT (0x0006), DEVICE_CERT (0x0007), AI_CODER_SESSION (0x0008), TRACE_SIGNATURE (0x0009 Phase 2 placeholder)
  ✓ Decision D: safety_class[EMBEDIQ_FB_SAFETY_CLASS_LEN] field in EmbedIQ_FB_Meta_t — canonical "STD:LEVEL" format
  ✓ Decision E: libembediq_obs INTERFACE CMake target — zero-dependency Observatory-only deployment
  ✓ Decision F: SBOM format — CycloneDX primary + SPDX via Protobom + ML-BOM
  ✓ Decision G: Fixed 14-byte ring buffer documented with full field breakdown and compliance rationale
  ✓ Decision H: Industry coverage table — corrections applied (Space + Nuclear → Not supported; DO-178C DAL C/D only)
  ✓ Decision I: Four migration entry points (Strangler Fig restricted to IEC 62304 Class A/B only)
  ✓ Decision J: AI-First Architecture — 4 Phase-1 constants (0x17–0x1A), AI_CODER_SESSION TLV, AI Code Review Gate
  ✓ Decision K: India market positioning — IndiaAI Mission, Missing Middle thesis, PLI, AIS-190, CDSCO, RISC-V

  New files added:
  ✓ COMPLIANCE.md — Industry coverage table, MISRA stance, tamper evidence tiers, SBOM rationale
  ✓ docs/MIGRATION.md — Four migration patterns, Strangler Fig IEC 62304 Class C restriction
  ✓ docs/architecture/AI_FIRST_ARCHITECTURE.md — Full AI-first architecture spec
  ✓ iqtrace_format.md promoted to v1.1

## Phase 2 Hardware — IN PROGRESS
  ✓ P2-T5: Industrial gateway example — COMPLETE (examples/gateway/, 6 FBs, integration_gateway_full ctest)
  Next: P2-T1 — FreeRTOS OSAL (osal/freertos/embediq_osal_freertos.c)
  Next: P2-T2 — ESP32 CMake target
  Next: P2-T3 — fb_cloud_mqtt (MQTT 3.1.1)
  Next: P2-T4 — fb_ota (OTA FSM, dual-bank)

## dev → main sync — COMPLETE (v0.1.0)
  Phase 1 milestone promoted to main.
  PR #73 (Final Decision Set v2.0) merged to dev, then dev promoted to main as v0.1.0.
  17/17 ctest on main. validator.py + boundary_checker.py clean.
  Next sync target: v0.2.0 — after FreeRTOS OSAL (P2-T1) + ESP32 target (P2-T2) land on dev.
