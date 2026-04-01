# AGENTS.md — EmbedIQ Project North Star

**Read this file first. Every task. Every agent. Every contributor.**

This is the single entry point for understanding EmbedIQ before writing any code,
creating any task, or making any architectural decision.

---

## 1. What EmbedIQ Is (3 Sentences)

EmbedIQ is an open-source, Apache 2.0 licensed **application framework for
embedded and edge** — the layer that sits above the substrate (RTOS, bare-metal, or
Linux) and gives developers clean, reusable structure for building production firmware
and Linux gateway applications.

It provides a message-driven actor model (Functional Blocks), a reusable component
library (cloud, OTA, telemetry, observability), and hardware abstraction that enables
host/Linux simulation without physical hardware.

It is **substrate-agnostic, hardware-vendor independent, and forever free** — with no
cloud-vendor lock-in. Runs on FreeRTOS, bare-metal MCUs, and Linux gateway devices
from the same codebase.

---

## 2. The Four Core Principles (Non-Negotiable)

These are not guidelines. They are binding rules that define EmbedIQ.
Violating any one of them is an architectural defect, not a style issue.

```
PRINCIPLE 1 — Everything is a message (at FB boundaries)
  No Functional Block ever calls another FB directly.
  All cross-FB communication is a typed message through the bus.
  Intra-FB helper function calls are normal C — this rule applies at FB boundaries only.

PRINCIPLE 2 — Everything has a contract
  Every layer has a stable interface header and pluggable implementations.
  You program to the contract. RTOS, BSP, and platform swap beneath it.
  Core headers are the source of truth. No other file overrides them.

PRINCIPLE 3 — Everything is described
  Every module carries machine-readable metadata.
  Every message has a schema (messages.iq).
  Every topology can be exported.

PRINCIPLE 4 — The wrong patterns are structurally visible
  Fixed contracts between layers mean cross-FB coupling, hardware dependencies
  in application code, and untyped data passing cannot be hidden — they fail
  compilation, fail CI, or are obvious in code review.
  Structural discipline is enforced by the architecture, not hoped for.
```

---


## 2A. Message ID Namespace — Agent Must Verify

Before defining any message, verify the ID falls in the correct range.

```
0x0000 – 0x03FF   Core system (framework internal only — never use in your code)
0x0400 – 0x13FF   Official EmbedIQ components (assigned by embediq/embediq)
0x1400 – 0xFFFF   Community / third-party — reserve range in messages_registry.json first
```

**Agent rule:** When generating a new message definition, always state which namespace range you are using and why. If writing a community FB, check messages_registry.json first for range availability.

---

## 2B. Queue Overflow Policy — Agent Must Know

When writing code that publishes messages:

- **HIGH queue full** → sender blocks. Design HIGH publishers to be rare and fast.
- **NORMAL queue full** → oldest message dropped, new message enqueued. Observable event emitted.
- **LOW queue full** → new message dropped. Observable event emitted.

**Agent rule:** Never write code that assumes queue operations always succeed. Check return values. HIGH queue blocking is intentional — a caller that fills HIGH repeatedly has a design problem.

---

## 2C. Boot Phase Model — Agent Must Declare

Every FB must declare its boot_phase. If not declared, default = APPLICATION (Phase 3).

```
EMBEDIQ_BOOT_PHASE_PLATFORM       = 1  // fb_uart, fb_timer, fb_gpio — hardware only
EMBEDIQ_BOOT_PHASE_INFRASTRUCTURE = 2  // fb_nvm, fb_watchdog, fb_cloud — services
EMBEDIQ_BOOT_PHASE_APPLICATION    = 3  // developer FBs (default)
EMBEDIQ_BOOT_PHASE_BRIDGE         = 4  // External FBs, Studio connections
```

**Agent rule:** When generating an FB, always include boot_phase in the config. When writing fb_nvm, fb_watchdog, fb_cloud — use INFRASTRUCTURE. When writing application FBs — use APPLICATION. Never declare a Phase 2 FB with depends_on pointing to a Phase 3 FB.

---
## 3. Layer Model — The Complete Layer Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│  LAYER 4 — COMMERCIAL (future)                                  │
│  EmbedIQ Studio · EmbedIQ Cloud · AI Coder                     │
├─────────────────────────────────────────────────────────────────┤
│  CLIENT SDKs (future)                                           │
│  embediq-python · embediq-js · embediq-rust                     │
├─────────────────────────────────────────────────────────────────┤
│  LAYER 3 — ECOSYSTEM                                            │
│  Bridge daemon · bridge/websocket · bridge/unix_socket          │
│  Community Driver FBs · 3rd-party FB wrappers                   │
├─────────────────────────────────────────────────────────────────┤
│  LAYER 2 — DRIVER FBs + SERVICE FBs                             │
│  fb_uart · fb_timer · fb_gpio · fb_watchdog · fb_logger         │
│  fb_cloud_mqtt · fb_ota · fb_telemetry · fb_nvm                 │
├─────────────────────────────────────────────────────────────────┤
│  LAYER 1 — FRAMEWORK ENGINE                                     │
│  FB Registry · Endpoint Router · Message Bus (3-queue)          │
│  Sub-fn Dispatcher · Timer Manager · FSM Engine                 │
│  Observatory · Test Runner [TEST BUILDS ONLY]                   │
├─────────────────────────────────────────────────────────────────┤
│  CORE — CONTRACTS  ← STABLE AFTER FREEZE · NEVER CHANGE        │
│  embediq_fb.h · embediq_subfn.h · embediq_bus.h · embediq_msg.h │
│  embediq_sm.h · embediq_obs.h · embediq_osal.h · embediq_time.h │
│  embediq_bridge.h · embediq_meta.h · embediq_endpoint.h         │
│  embediq_msg_catalog.h · hal/embediq_hal_*.h (×6)               │
├─────────────────────────────────────────────────────────────────┤
│  HAL  ←  hal/posix/ · hal/esp32/ · hal/stm32/                  │
├─────────────────────────────────────────────────────────────────┤
│  OSAL  ←  osal/posix/ · osal/freertos/ · osal/zephyr/          │
├─────────────────────────────────────────────────────────────────┤
│  SUBSTRATE                                                      │
│  FreeRTOS · Zephyr (Phase 2) · bare-metal (Phase 2) · host/Linux│
└─────────────────────────────────────────────────────────────────┘
```

**Layer dependency rule:** Each layer may only depend on the layer directly below it.
Layer 2 may call Layer 1 APIs. Layer 2 must NOT call Core internals or skip Layer 1.
Agents: never add an include that skips a layer.

---

## 4. Repository Structure

```
embediq/
│
├── AGENTS.md               ← YOU ARE HERE
├── CODING_RULES.md         ← read second, always
├── COMPLIANCE.md           ← industry coverage table, SBOM, tamper evidence, safety_class encoding
├── CONTRIBUTING.md
├── BUILD_STATUS.md
├── CMakeLists.txt
├── messages_registry.json  ← authoritative ID namespace allocations
│
├── core/                   ← LOCKED after v1.0 freeze
│   ├── include/            ← all contract headers (never change)
│   │   └── hal/            ← HAL contract headers (hal_flash, hal_gpio,
│   │                          hal_i2c, hal_spi, hal_timer, hal_uart,
│   │                          hal_wdg, hal_obs_stream)
│   └── src/                ← engine implementation
│       ├── bus/
│       ├── registry/
│       ├── fsm/
│       ├── dispatcher/
│       └── observatory/
│
├── osal/
│   ├── posix/              ← macOS + Linux + WSL
│   └── freertos/           ← FreeRTOS bare-metal (Phase 2)
│
├── hal/                    ← HAL implementations (no embediq_*.h allowed)
│   ├── posix/              ← hal_timer_posix.c · hal_flash_posix.c ·
│   │                          hal_wdg_posix.c · hal_obs_stream_posix.c
│   └── esp32/              ← Phase 2
│
├── fbs/                    ← portable Functional Blocks
│   ├── drivers/            ← Driver FBs: fb_timer · fb_nvm · fb_watchdog
│   └── services/           ← Service FBs: fb_cloud_mqtt · fb_ota (Phase 2)
│
├── examples/
│   ├── thermostat/         ← style reference for application FBs (Phase 1)
│   └── gateway/            ← industrial edge gateway reference (Phase 1, 6 FBs)
│
├── tests/
│   ├── unit/               ← unit tests (host, no hardware)
│   ├── integration/        ← integration tests
│   ├── cli/                ← Python CLI tests (test_obs_cli.py)
│   └── compat/             ← fb_v1_compat.c — contract freeze CI check
│
├── tools/
│   ├── validator.py        ← validates embediq_config.h sizing constants
│   ├── boundary_checker.py ← enforces layer include rules in CI
│   ├── messages_iq/        ← messages.iq → C struct generator
│   └── embediq_obs/        ← Observatory CLI (embediq_obs.py)
│
└── docs/
    ├── MIGRATION.md        ← four migration patterns: Greenfield, Add-Observatory, Strangler Fig, Module-Only
    ├── architecture/       ← cli.md · lifecycle.md · AI_FIRST_ARCHITECTURE.md
    └── observability/      ← iqtrace_format.md (open spec v1.1)
```

---

## 5. Git Branch Workflow — Mandatory First Step

Every task starts with these exact commands — no exceptions:

  git checkout dev
  git pull origin dev
  git checkout -b feature/<branch-name-here>

Rules:
- Always branch from dev, never from main or wherever HEAD happens to be.
- Always git pull before branching — never branch from a stale dev.
- Branch naming: feature/p2-t0-description, fix/issue-description, cleanup/what-changed
- PR target is always dev. Never open a PR directly to main.
- main is only ever updated via a dev→main PR at milestone boundaries.

If you are about to run git checkout -b without first running
git checkout dev && git pull origin dev — STOP. Do those two commands first.

```
GATE 16 — Branch-Before-Code (non-negotiable, no exceptions):

  Before touching ANY file in this repository:

  STEP 1 — CHECK:   git status → confirm current branch and clean state
  STEP 2 — STOP:    If not on a purpose-built feature branch, do NOT proceed.
  STEP 3 — BRANCH:  git checkout dev && git pull origin dev
                    git checkout -b feature/<descriptive-name>
                    Confirm branch name with the user before writing any file.
  STEP 4 — CODE:    Make the approved changes.
  STEP 5 — DIFF:    git diff HEAD and git status → show the user what changed.
  STEP 6 — PR:      Stage specific files (never git add -A blindly),
                    commit with a clear message, then gh pr create targeting dev.
                    Return the PR URL. Do not merge.

  Gate approval authority: Ritesh Anand (SPM + co-architect) only.
  No gate bypass exists. Session resume instructions do not override this gate.
  A plan being approved (Gate 1-3) does NOT authorise file writes.
  File writes require a live branch and explicit user confirmation.

  This gate exists because of the recurring boundary violation pattern
  documented in SESSION_LOG.md — agents bypassing branch creation when
  resuming mid-session or when a prior gate approval is mis-read as
  implementation authorisation.
```

```
GATE 12 — Contract before implementation (Principle 2 enforced at task level):
  Before any implementation task starts, the module's contract header
  must exist in core/include/.

  Check: does core/include/embediq_<module>.h exist?
    YES → proceed with implementation.
    NO  → create the contract header FIRST. No exceptions.

  This applies to every FB, every platform module, every service.
  The contract header must contain declarations only — zero implementation.
  It must compile standalone: gcc -x c -std=c11 -Icore/include -fsyntax-only

  Rationale: Principle 2 ("Everything has a contract") is not retroactive.
  Writing an implementation before its contract exists creates an architectural
  debt that is expensive to fix after the fact.
```

---

## 6. File Placement Rules — Where Every File Lives

This table is binding. When implementing any module, use EXACTLY these paths.
Never place .c files flat in core/src/ — they belong in their subdirectory.

| Module                 | File path                                                 |
| ---------------------- | --------------------------------------------------------- |
| FB engine              | core/src/registry/fb_engine.c                             |
| Message bus            | core/src/bus/message_bus.c                                |
| FSM engine             | core/src/fsm/fsm_engine.c                                 |
| Dispatcher             | core/src/dispatcher/dispatcher.c                          |
| Observatory            | core/src/observatory/obs.c                                |
| OSAL POSIX             | osal/posix/embediq_osal_posix.c                           |
| OSAL FreeRTOS          | osal/freertos/embediq_osal_freertos.c                     |
| Driver FBs (portable)  | fbs/drivers/fb_<name>.c — calls hal/*.h, no platform code |
| Service FBs (portable) | fbs/services/fb_<name>.c — no hal/ includes permitted     |
| HAL implementations    | hal/<target>/hal_<peripheral>.c                           |
| Observatory CLI        | tools/embediq_obs/embediq_obs.py                          |
| CLI tests              | tests/cli/test_<tool>.py                                  |
| Components             | components/<fb_name>/<fb_name>.c                          |
| Unit tests             | tests/unit/test_<module>.c                                |

Rule: if you are about to create a .c file directly in core/src/ (not in a
subdirectory), STOP — wrong location. Check this table first.

---

## 7. FB Lifecycle Contract — OTA and Clean Shutdown

Any FB that owns persistent state or has in-flight work that must
complete before a firmware update must follow this contract:

REQUIRED:
- Subscribe to MSG_SYS_OTA_REQUEST (ID 0x0003)
- On receipt: finish in-flight work, flush NVM state, then publish
  MSG_SYS_OTA_READY (ID 0x0004)
- Must publish MSG_SYS_OTA_READY within 500ms or engine forces shutdown

OPTIONAL (FBs with no persistent state):
- No subscription required — engine handles shutdown via timeout

This contract is enforced by fb_ota (Phase 2 P2-T5).
See docs/architecture/lifecycle.md for full protocol description.

---

## 8. Current Build Status

> **Last updated:** Obs-6 complete — all pre-hardware tasks done (March 2026)

| Layer         | Module                         | Status      | Notes                                                        |
| ------------- | ------------------------------ | ----------- | ------------------------------------------------------------ |
| Core          | All contract headers           | STABLE      | Frozen. fb_v1_compat.c enforces no breaking changes.         |
| Core          | HAL contract headers (8)       | STABLE      | hal_flash · hal_gpio · hal_i2c · hal_spi · hal_timer · hal_uart · hal_wdg · hal_obs_stream |
| Core          | embediq_config.h               | STABLE      | All sizing constants. Use named constants only.              |
| Core          | messages.iq generator          | STABLE      | Python, zero deps. core.iq + thermostat.iq live.             |
| Core          | messages_registry.json         | STABLE      | 13 active ID allocations. validator.py range enforcement.    |
| OSAL          | posix (macOS + Linux + WSL)    | STABLE      | pthreads + POSIX. Blocking semaphore dispatch.               |
| OSAL          | freertos                       | NOT_STARTED | Phase 2                                                      |
| Core / Engine | FB Registry + Dispatch         | STABLE      | embediq_engine_boot() + embediq_engine_dispatch_shutdown()   |
| Core / Engine | Message Bus                    | STABLE      | 3-queue routing, overflow policy, observatory drops          |
| Core / Engine | FSM Engine                     | STABLE      | Table-driven, guard/action, observatory events               |
| Core / Engine | Observatory — core             | STABLE      | Ring buffer, 7-family event taxonomy, session management     |
| Core / Engine | Observatory — event families   | STABLE      | 7 families, band encoding, EMBEDIQ_OBS_EVT_FAMILY() macro    |
| Core / Engine | Observatory — trace levels     | STABLE      | EMBEDIQ_TRACE_LEVEL 0–3, per-family compile-time flags       |
| Core / Engine | Observatory — session API      | STABLE      | EmbedIQ_Obs_Session_t 40B (I-14), session_begin/get          |
| Core / Engine | Observatory — .iqtrace capture | STABLE      | Binary TLV file via hal_obs_stream.h HAL contract            |
| Core / Engine | Observatory — format spec      | STABLE      | docs/observability/iqtrace_format.md v1.1, Apache 2.0        |
| Core / Engine | Observatory — CLI              | STABLE      | tools/embediq_obs/embediq_obs.py — decode/stats/filter/export|
| Build         | libembediq_obs INTERFACE target | STABLE     | CMake INTERFACE target — zero-dependency Observatory-only deployment |
| HAL           | hal/posix/                     | STABLE      | hal_timer · hal_flash · hal_wdg · hal_obs_stream implemented |
| Driver FBs    | fb_timer                       | STABLE      | fbs/drivers/ + hal/posix/hal_timer_posix.c                   |
| Driver FBs    | fb_nvm                         | STABLE      | fbs/drivers/ + hal/posix/hal_flash_posix.c                   |
| Driver FBs    | fb_watchdog                    | STABLE      | fbs/drivers/ + hal/posix/hal_wdg_posix.c                     |
| Service FBs   | fb_cloud_mqtt                  | NOT_STARTED | Phase 2                                                      |
| Service FBs   | fb_ota                         | NOT_STARTED | Phase 2                                                      |
| Examples      | thermostat                     | STABLE      | 5 FBs, FSM cycles, Observatory output, zero printf           |
| Examples      | gateway                        | STABLE      | 6 FBs, edge-to-cloud pipeline, offline resilience, Observatory, zero printf |

**Status values:** `NOT_STARTED` · `IN_PROGRESS` · `STABLE`

---

## 9. What v1 Will NOT Build (Non-Goals)

Agents: **do not generate code for any item on this list for v1.**
These are named future work, not omissions. If you think something is missing,
check this list before adding it.

```
EXECUTOR:    Worker pool executor. v1 = dedicated OS thread per FB only.
MESSAGING:   Zero-copy message pool. v1 = copy-by-value at every queue send.
MESSAGING:   Variable-length or dynamic payload. v1 = fixed EMBEDIQ_MSG_MAX_PAYLOAD.
BRIDGE:      Authenticated bridge connections. v1 = no auth (dev mode only).
BRIDGE:      Reliable delivery (ack/retry). v1 = at-most-once delivery.
REGISTRY:    Runtime FB discovery. v1 = static registry only.
ROUTING:     Sub-fn consume/stop propagation. v1 = fan-out to all matching sub-fns.
ROUTING:     Dynamic subscription changes at runtime. v1 = set at init only.
OSAL:        Zephyr OSAL. v1 = FreeRTOS + POSIX/host only.
BSP:         STM32, nRF52, bare-metal Cortex-M0. v1 = posix (primary) + FreeRTOS host mock only.
PLATFORM FB: fb_i2c, fb_spi, fb_usb. v1 = fb_uart, fb_timer, fb_gpio only.
TIMESTAMP:   64-bit timestamps on MCU. v1 = uint32_t microseconds, modulo 2³².
```


- **Before defining a message ID:** verify range in correct namespace (0–1023 core, 1024–5119 official, 5120+ community). Community FBs must reserve range in messages_registry.json.
- **Before writing an FB config:** declare boot_phase explicitly. Infrastructure FBs = Phase 2. Application FBs = Phase 3 (default).
- **Before using timestamp_us for ordering:** use sequence instead. timestamp_us wraps at 71 min and is informational only.
- **Before writing ISR code:** verify ISR exits in &lt;10 cycles. Ring buffer write + osal_signal_from_isr() only. Overflow = drop oldest + increment counter + signal thread.

---

## 10. Truth Hierarchy — Which File Wins Conflicts

When you see a conflict between documents, this order decides:

```
1. core/include/*.h          ← contracts (wins all conflicts, always)
2. CODING_RULES.md           ← rules (wins over any doc description)
3. LAYER.md (per layer)      ← layer surface and status
4. MODULE.md (per module)    ← module detail and status
5. docs/architecture/        ← reference only, not normative
```

If a MODULE.md description conflicts with what a Core header says,
the Core header is correct. Update MODULE.md to match.

---

## 11. Build System & Key Decisions

| Decision            | Choice                                                    | Do Not Change |
| ------------------- | --------------------------------------------------------- | ------------- |
| Build system        | CMake                                                     | Yes — locked  |
| Core language       | C11                                                       | Yes — locked  |
| License             | Apache 2.0                                                | Yes — locked  |
| v1 executor         | Dedicated OS thread per FB                                | Yes — locked  |
| v1 message passing  | Copy-by-value                                             | Yes — locked  |
| v1 message priority | 3 queues per FB (HIGH/NORMAL/LOW)                         | Yes — locked  |
| messages.iq v0      | msg_id + name + payload_size + schema_id                  | Yes — locked  |
| Timestamp on MCU    | uint32_t microseconds, wraps ~71 min                      | Yes — locked  |
| Core header freeze  | After thermostat + Observatory + Test Runner pass on host | Pending       |

---

## 12. Where to Go Next

| What you need                                      | Where to find it                              |
| -------------------------------------------------- | --------------------------------------------- |
| All coding rules + invariants + forbidden patterns | `CODING_RULES.md`                             |
| Complete header contracts with examples            | `core/include/*.h`                            |
| HAL contract headers                               | `core/include/hal/`                           |
| .iqtrace binary format specification               | `docs/observability/iqtrace_format.md`        |
| AI-first architecture, AI Code Review Gate         | `docs/architecture/AI_FIRST_ARCHITECTURE.md`  |
| Observatory CLI usage                              | `tools/embediq_obs/embediq_obs.py --help`     |
| Style reference for application FBs               | `examples/thermostat/`                        |
| Style reference for edge/gateway applications              | `examples/gateway/`                           |
| Style reference for Driver FBs                    | `fbs/drivers/fb_timer.c`                      |
| Style reference for HAL implementations           | `hal/posix/hal_timer_posix.c`                 |
| Run invariant + layer checks locally              | `python3 tools/validator.py` and `python3 tools/boundary_checker.py` |
| Industry compliance table, SBOM formats, safety_class      | `COMPLIANCE.md`                               |
| Migration patterns — Greenfield/Add-Observatory/Strangler  | `docs/MIGRATION.md`                           |
| Message ID namespace allocations                  | `messages_registry.json`                      |

---

## 13. The Developer First Hour Test

Before any Phase 1 launch, this test must pass with 3 engineers
who have never seen EmbedIQ before:

```
Clone repo → cmake → make → run thermostat demo on host → see Observatory output
```

Time limit: under 30 minutes. No help from the author.
If it fails, the Getting Started guide and/or build system needs fixing.
Code correctness is secondary to this test passing.

---

## 14. AI Code Review Gate — Mandatory for Safety-Classified FBs

**Decision J (Final Decision Set v2.0). See full specification: `docs/architecture/AI_FIRST_ARCHITECTURE.md §4`.**

When an AI coding assistant modifies a Functional Block, the following rules apply.

### When the gate triggers

The gate is mandatory when **both** of the following are true:
1. The commit was made (in whole or in part) by an AI coding assistant (GitHub Copilot, Claude, Cursor, or any tool that emits an `AI_CODER_SESSION` TLV).
2. The modified FB has `safety_class != "NONE"` in its registered `EmbedIQ_FB_Meta_t`.

FBs with `safety_class == "NONE"` follow the standard code review process — no extra gate.

### What the gate requires

```
STEP 1 — SCOPE: Identify every FB modified in the AI session whose safety_class != "NONE".
STEP 2 — REVIEWER: Assign a human reviewer qualified at the highest safety level modified.
          ISO26262:ASIL-B or higher / IEC61508:SIL-2 or higher → functional safety competency required.
STEP 3 — REVIEW: Confirm the modification does not weaken safety mechanisms.
          Confirm all _Static_assert invariants referencing modified types are still correct.
          Run a static analysis pass appropriate to the safety_class level.
STEP 4 — RECORD: Set safety_class_reviewed = 1 (pass) or 2 (fail) in AI_CODER_SESSION TLV.
STEP 5 — GATE: If safety_class_reviewed == 2, BLOCK merge to main until issues are resolved.
```

### Agent rule

If you are an AI coding assistant and you modify a file that registers an FB with `safety_class != "NONE"`, you MUST include a comment in your PR/commit body declaring:
- Which FBs were modified
- What the safety_class of each FB is
- That the AI Code Review Gate is required before merge

You must NOT set `safety_class_reviewed = 1` yourself. Only a qualified human reviewer may do so.

### AI_CODER_SESSION TLV

The `AI_CODER_SESSION` TLV (type `0x0008`, defined in `docs/observability/iqtrace_format.md §5.7`) is the machine-readable record of the AI coding session. Its `safety_class_reviewed` field (byte 26) is the attestation field that records the gate outcome. See `AI_FIRST_ARCHITECTURE.md §3` for the full field layout.

---

## 15. Observatory AI Event Band — Current Status and Reservation

**Phase-1 AI constants (implemented — Decision J):**

| Constant | Value | Band |
|---|---|---|
| `EMBEDIQ_OBS_EVT_AI_INFERENCE_START` | `0x17` | SYSTEM |
| `EMBEDIQ_OBS_EVT_AI_INFERENCE_END` | `0x18` | SYSTEM |
| `EMBEDIQ_OBS_EVT_AI_MODEL_LOAD` | `0x19` | SYSTEM |
| `EMBEDIQ_OBS_EVT_AI_CONFIDENCE_THRESHOLD` | `0x1A` | SYSTEM |

**Phase-3 AI family band (reserved — do not use):**

The range `0x80–0x8F` is reserved for the Phase-3 AI event family. A comment block in `embediq_obs.h` records this reservation. **Do NOT allocate any constant in 0x80–0x8F before the Phase-3 specification is published.**

Phase-3 gate: ≥2 production deployments using Phase-1 constants (0x17–0x1A) for ≥6 months.

See `ROADMAP.md — Phase 3: Full AI Event Family Band` and `docs/architecture/AI_FIRST_ARCHITECTURE.md §5`.

---

*EmbedIQ — embediq.com — Apache 2.0*
*Agents: re-read this file at the start of every new task session.*
