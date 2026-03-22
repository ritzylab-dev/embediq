# BUILD_STATUS.md — EmbedIQ Implementation Status
# Last updated: Phase 1 complete
# Rule: update this file at every phase boundary. Never let it go stale.

## What Exists vs What Does Not

| Module | Status | What it means |
|--------|--------|---------------|
| Core headers (7 files) | STABLE | Contracts declared, compile clean, v1 API frozen |
| messages.iq generator | STABLE | Python tool, zero deps, generates typed C structs |
| Core schema (core.iq) | STABLE | Lifecycle, boot, watchdog, timer, observatory messages |
| CMake build system | STABLE | host + test targets, pre-build generation step |
| CI pipeline | STABLE | Validates build + validator + compat shim on every PR |
| validator.py | STABLE | Catches hardcoded sizing constants |
| fb_v1_compat.c | STABLE | Compile-time API contract enforcement |
| OSAL posix | STABLE | pthreads + POSIX message queues |
| OSAL freertos | NOT_STARTED | Phase 2 |
| FB Registry | STABLE | Phase 1 — embediq_engine_boot(), fb state machine |
| Message Bus | STABLE | Phase 1 — static routing table, pub/sub queues |
| Sub-fn Dispatcher | STABLE | Phase 1 — embediq_subfn_register(), dispatch loop |
| FSM Engine | STABLE | Phase 1 — table-driven FSM with guard/action |
| Observatory | STABLE | Phase 1 — ring buffer, stdout/UART transport |
| Test Runner | STABLE | Phase 1 — unit + integration tests via CTest |
| platform/posix/fb_timer | STABLE | 1 ms background tick, MSG_TIMER_1SEC every 1 s |
| platform/posix/fb_nvm | STABLE | Filesystem-backed JSON key-value store |
| platform/posix/fb_watchdog | STABLE | Health-token monitor, 100 ms check interval |
| examples/thermostat | STABLE | Phase 1 gate demo — all 5 FBs, FSM transitions verified |

## IMPORTANT FOR CONTRIBUTORS
The Phase 1 platform stack is complete and running.  The thermostat demo binary
(`embediq_thermostat`) boots all 5 FBs, runs for 30 s, and drives the thermal
FSM through NORMAL → WARNING → CRITICAL → NORMAL automatically.

The integration test (`test_thermostat_full`) passes all 6 assertions in ~15 s.

Phase 1 includes a framework-managed dispatch loop: `embediq_engine_dispatch_boot()`
creates one pthread per FB that has subscriptions.  Each thread reads from its FB's
priority queues (HIGH > NORMAL > LOW) and calls the matching sub-function — completing
the end-to-end chain from `fb_timer` through `fb_temp_sensor` to `fb_temp_controller`.

The current dispatch loop uses 1 ms polling (`embediq_osal_delay_ms(1u)`) when all
queues are empty.  Replacing this with a blocking OSAL semaphore is a Phase 2
prerequisite before porting to FreeRTOS.

`fb_cloud_mqtt` and `fb_ota` were deferred from Phase 1 and are Phase 2 deliverables.

New Phase 1 public API: `embediq_engine_dispatch_boot()` — declared in embediq_fb.h.

## Phase 0 — COMPLETE
  ✓ P0-T1: Repo scaffold, CMake, CI pipeline
  ✓ P0-T2: Core FB/SubFn/FSM headers (embediq_fb.h, embediq_subfn.h, embediq_sm.h)
  ✓ P0-T3: OSAL/Bus/Observatory/Msg headers (embediq_osal.h, embediq_msg.h, embediq_bus.h, embediq_obs.h)
  ✓ P0-T4: messages.iq generator + core.iq schema
  ✓ P0-T5: Full integration verify — all checks green

## Phase 1 — COMPLETE
  ✓ P1-T1: OSAL POSIX (pthreads, mqueues, signals, delays) — core/src/osal/
  ✓ P1-T2: FB Engine (register, boot, state machine) — core/src/registry/
  ✓ P1-T3: Message Bus (pub/sub routing, static table) — core/src/bus/
  ✓ P1-T4: Sub-fn Dispatcher + FSM Engine — core/src/subfn/, core/src/fsm/
  ✓ P1-T5: Observatory (ring buffer, stdout transport, CTest) — core/src/obs/
  ✓ P1-T6: Platform FBs (fb_timer, fb_nvm, fb_watchdog) — platform/posix/
  ✓ P1-T7: thermostat_msgs.h + message namespace (0x0420)
  ✓ P1-T8: Thermostat integration (fb_temp_sensor, fb_temp_controller, 5-FB demo,
            test_thermostat_full — all 6 assertions pass in 15 s real-time run)

## Phase 2 — NOT STARTED
  Next: P2-T1 — Replace 1 ms polling dispatch loop with blocking OSAL semaphore
  Next: P2-T2 — embediq_engine_dispatch_shutdown() for clean task teardown
  Next: P2-T3 — fb_cloud_mqtt (MQTT 3.1.1) and fb_ota (OTA FSM)

## Your Next Actions (Human)
  1. Merge PR to dev
  2. ✓ LinkedIn update #4 — PUBLISHED
  3. ✓ LinkedIn update #5 — PUBLISHED ("I Almost Built This 5 Years Ago — Here's What Changed") https://www.linkedin.com/pulse/i-almost-built-5-years-ago-heres-what-changed-ritesh-anand-q2j2c
