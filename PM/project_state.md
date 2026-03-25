# EmbedIQ — PM Project State

Read this file at the start of every session alongside 13_gate_protocol.md.
Update it whenever a task completes or a decision is made.

---

## Repo

- GitHub: https://github.com/ritzylab-dev/embediq  (org: ritzylab-dev, NOT ritzylab)
- Primary branch: `dev`  (all work merges here)
- `main` is in sync with dev — promoted at v0.1.0 milestone (2026-03-24)

---

## Milestone Status

| Milestone | Status | Tag | Date |
|-----------|--------|-----|------|
| v0.1.0 — Phase 1 + HAL Refactor + Observability | **COMPLETE** | `v0.1.0 → cd035a8` | 2026-03-24 |
| v0.2.0 — FreeRTOS + ESP32 + MQTT + OTA | Pending | — | — |

---

## Layer Model (locked)
Layer 4  Commercial
Layer 3  Ecosystem
Layer 2  Driver FBs (fbs/drivers/) + Service FBs (fbs/services/)
Layer 1  Framework Engine
CONTRACTS (core/include/hal/hal_*.h)
HAL  (hal/posix/, hal/esp32/)
OSAL (osal/posix/, osal/freertos/)
Substrate

### Directory rules (enforced by boundary_checker.py)
- `hal/posix/*.c`       → zero embediq_*.h, zero framework headers
- `fbs/drivers/*.c`     → zero POSIX headers, uses hal/ via contract only
- `fbs/services/*.c`    → zero hal/ includes
- `core/include/hal/`   → contract headers only, no implementation

---

## Completed Work

| PR | Branch | Description |
|----|--------|-------------|
| #24 | arch/hal-contracts | HAL contract headers + endpoint router (Arch-2) |
| #25 | arch/boundary-hal-drivers | boundary_checker for hal/ and drivers/ layers (Arch-3) |
| #26 | arch/fbs-directory-rename | Rename drivers/ → fbs/drivers/, add fbs/services/ (Arch-3b) |
| #27 | arch/fb-timer-hal | fb_timer split: hal_timer_posix.c + fb_timer.c (Arch-4a) — accidentally merged to main, reconciled via PR #43 |
| #28 | arch/fb-nvm-hal | fb_nvm split: hal_flash_posix.c + fb_nvm.c (Arch-4b) |
| #29 | arch/fb-watchdog-hal | fb_watchdog split: hal_wdg_posix.c + fb_watchdog.c, delete platform/ (Arch-4c) |
| #30 | cleanup/msg-id-reconciliation | MSG_ ID fix: header IDs reconciled, range enforcement |
| #31 | cleanup/messages-registry | messages_registry.json 13 entries + validator.py registry check |
| #32 | obs/event-families | Obs-1: 7-family event taxonomy, EMBEDIQ_OBS_EVT_* bands |
| #33 | obs/trace-levels | Obs-2: EMBEDIQ_TRACE_LEVEL 0–3 + per-family compile-time flags |
| #34 | obs/session-metadata | Obs-3: EmbedIQ_Obs_Session_t (40B, sizeof enforced CODING_RULES I-15) |
| #35 | obs/iqtrace-format-spec | Obs-4: .iqtrace binary format v1.0 spec (docs/observability/iqtrace_format.md) |
| #36 | obs/binary-transport | Obs-5: embediq_obs_capture_begin/end() binary file transport |
| #37 | obs/cli-tooling | Obs-6: embediq_obs CLI — decode/stats/filter/export/tail |
| #38 | docs/pass1-readme-buildstatus | Pass 1: README.md + BUILD_STATUS.md |
| #39 | docs/pass2-agents-architecture | Pass 2: AGENTS.md + ARCHITECTURE.md |
| #40 | docs/pass3-cleanup | Pass 3: ROADMAP, COMMERCIAL_BOUNDARY, CONTRIBUTING, CODING_RULES, cli.md |
| #41 | feat/capture-wire-and-test | --capture flag wired, integration_thermostat_capture ctest, thermostat README |
| #42 | docs/handoff-milestone-update | HANDOFF.md full rewrite for v0.1.0 milestone |
| #43 | chore/reconcile-main | Reconcile diverged main history into dev (ours strategy, zero file changes) |
| #44 | dev→main | v0.1.0 milestone promotion — 38 commits, merge commit on main |
| #45 | docs/project-state-v010 | PM/project_state.md full rewrite — v0.1.0 complete, all PRs #24–#44 recorded |
| #46 | fix/fix-generated-gitignore | Commit generated headers, add CI drift-check — fixes fresh-clone build failure (reported by external contributor Michael) |

---

## In Progress

None.

---

## Pending Tasks (Phase 2 — in order)

1. **P2-T1** — FreeRTOS OSAL: `osal/freertos/embediq_osal_freertos.c`
2. **P2-T2** — ESP32 CMake platform target
3. **P2-T3** — `fb_cloud_mqtt`: MQTT 3.1.1 pure C
4. **P2-T4** — `fb_ota`: OTA FSM, dual-bank atomic write
5. **P2-T5** — Industrial gateway example

---

## Key Decisions (locked)

- `fbs/drivers/` = Driver FBs (portable, HAL-backed)
- `fbs/services/` = Service FBs (platform-agnostic, no HAL)
- Binary NVM format (not JSON) — HAL carries bytes, FB owns schema
- `EMBEDIQ_NVM_PATH` env var replaces `nvm__set_path()` test helper
- `platform/` retired — fully deleted in PR #29
- All Phase 2 work merges to `dev`. `main` promoted at milestone boundaries.
- PM role is read-only on repo. All changes go through agent prompts.
- Registry is authoritative for message ID namespacing. validator.py enforces.
- Gate protocol is 13 gates (13_gate_protocol.md) — Gate 13 added 2026-03-24.
- generated/*.h files are design-time artifacts (binary wire protocol contracts) — committed to repo, not gitignored. The .iq generator is a design tool, not a build tool. CI drift-check (I-16) enforces .iq → .h consistency on every PR. Decision date: 2026-03-24.

### Observability decisions (locked 2026-03-23)

- **Raw data open, visualization/insight commercial** (Apache compatible)
- **Event record frozen at 14 bytes** — _Static_assert enforced
- **Event families derived from event_type bands** — zero runtime overhead
  - 0x00–0x1F: SYSTEM  |  0x20–0x2F: MESSAGE  |  0x30–0x3F: STATE
  - 0x40–0x4F: RESOURCE  |  0x50–0x5F: TIMING  |  0x60–0x6F: FAULT  |  0x70–0x7F: FUNCTION
- **.iqtrace uses TLV framing** (type 2B + length 2B + payload NB)
- **CLI lives in same repo** under `tools/embediq_obs/` — stays in sync with format
- **.iqtrace format Apache 2.0 forever** — frozen, never made proprietary

---

## Known Issues / Watch List

- `MSG_TEMP_READING = 0x0420` falls in `fb_uart` reserved range (0x0400–0x046B)
  → No conflict now but MUST be resolved before fb_uart implementation (P2)

---

## Future Improvements (non-blocking)

- Observatory dual transport: write to .iqtrace AND stdout simultaneously.
  Currently capture replaces stdout transport.
- `--capture` in thermostat starts AFTER boot — boot LC events not in .iqtrace.
  Fix: call embediq_obs_capture_begin before embediq_engine_boot().

---

*Last updated: 2026-03-24 — v0.1.0 milestone complete. PRs #32–#44 merged. Tag v0.1.0 → cd035a8 on main.*
