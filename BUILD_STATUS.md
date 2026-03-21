# BUILD_STATUS.md — EmbedIQ Implementation Status
# Last updated: Phase 0 complete
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
| OSAL posix | NOT_STARTED | Phase 1 — pthreads + POSIX queues |
| OSAL freertos | NOT_STARTED | Phase 2 |
| FB Registry | NOT_STARTED | Phase 1 |
| Message Bus | NOT_STARTED | Phase 1 |
| Sub-fn Dispatcher | NOT_STARTED | Phase 1 |
| FSM Engine | NOT_STARTED | Phase 1 |
| Observatory | NOT_STARTED | Phase 1 |
| Test Runner | NOT_STARTED | Phase 1 |
| platform/posix/fb_timer | NOT_STARTED | Phase 1 |
| platform/posix/fb_uart | NOT_STARTED | Phase 1 |
| platform/posix/fb_gpio | NOT_STARTED | Phase 1 |
| platform/posix/fb_watchdog | NOT_STARTED | Phase 1 |
| examples/thermostat | NOT_STARTED | Phase 1 gate demo |

## IMPORTANT FOR CONTRIBUTORS
The headers in core/include/ define contracts only — zero implementation exists.
Do not assume any runtime engine is present. Phase 1 builds the engine.
The thermostat example binary (embediq_thermostat) does not run yet.

## Phase 0 — COMPLETE
  ✓ P0-T1: Repo scaffold, CMake, CI pipeline
  ✓ P0-T2: Core FB/SubFn/FSM headers (embediq_fb.h, embediq_subfn.h, embediq_sm.h)
  ✓ P0-T3: OSAL/Bus/Observatory/Msg headers (embediq_osal.h, embediq_msg.h, embediq_bus.h, embediq_obs.h)
  ✓ P0-T4: messages.iq generator + core.iq schema
  ✓ P0-T5: Full integration verify — all checks green

## Phase 1 — NOT STARTED
  Next: P1-T1 — OSAL POSIX implementation

## Your Next Actions (Human)
  1. Merge this PR to dev
  2. Merge dev to main (Phase 0 complete milestone)
  3. Set up Claude Projects on claude.ai (see CLAUDE_PROJECTS_SETUP.md)
  4. Post LinkedIn update #4 — March 23
