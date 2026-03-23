# AGENTS.md — EmbedIQ Project North Star

**Read this file first. Every task. Every agent. Every contributor.**

This is the single entry point for understanding EmbedIQ before writing any code,
creating any task, or making any architectural decision.

---

## 1. What EmbedIQ Is (3 Sentences)

EmbedIQ is an open-source, Apache 2.0 licensed **application framework for
embedded/IoT firmware** — the layer that sits above the RTOS and gives developers
clean, reusable structure for building production firmware.

It provides a message-driven actor model (Functional Blocks), a reusable component
library (cloud, OTA, telemetry, observability), and hardware abstraction that enables
host/Linux simulation without physical hardware.

It is **RTOS-agnostic, hardware-vendor independent, and forever free** — with no
cloud-vendor lock-in.

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
## 3. Shell Model — The Complete Layer Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│  SHELL 4 — COMMERCIAL (future)                                  │
│  EmbedIQ Studio · EmbedIQ Cloud · AI Coder                     │
├─────────────────────────────────────────────────────────────────┤
│  CLIENT SDKs (future)                                           │
│  embediq-python · embediq-js · embediq-rust                     │
├─────────────────────────────────────────────────────────────────┤
│  SHELL 3 — ECOSYSTEM                                            │
│  Bridge daemon · bridge/websocket · bridge/unix_socket          │
│  Community BSPs · 3rd-party FB wrappers                         │
├─────────────────────────────────────────────────────────────────┤
│  SHELL 2 — PLATFORM COMPONENTS                                  │
│  fb_uart · fb_timer · fb_gpio · fb_watchdog · fb_logger         │
│  fb_cloud_mqtt · fb_ota · fb_telemetry · fb_nvm                 │
├─────────────────────────────────────────────────────────────────┤
│  SHELL 1 — FRAMEWORK ENGINE                                     │
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
│  SUBSTRATE                                                      │
│  FreeRTOS · Zephyr (Phase 2) · bare-metal (Phase 2) · host/Linux│
└─────────────────────────────────────────────────────────────────┘
```

**Layer dependency rule:** Each layer may only depend on the layer directly below it.
Shell 2 may call Shell 1 APIs. Shell 2 must NOT call Core internals or skip Shell 1.
Agents: never add an include that skips a layer.

---

## 4. Repository Structure

```
embediq-core/
│
├── AGENTS.md               ← YOU ARE HERE
├── CODING_RULES.md         ← read second, always
├── CONTRIBUTING.md
├── CHANGELOG.md
├── CODEOWNERS
├── CMakeLists.txt
├── embediq_config.h        ← all sizing parameters live here
│
├── core/                   ← LOCKED after v1.0 freeze
│   ├── include/            ← 13 header contracts (never change)
│   └── src/                ← engine implementation
│       ├── bus/
│       ├── registry/
│       ├── fsm/
│       ├── dispatcher/
│       └── observatory/
│
├── osal/                   ← threading model only (RTOS-specific, no hardware)
│   ├── posix/              ← macOS + Linux + WSL (any POSIX-compatible OS)
│   └── freertos/           ← FreeRTOS bare-metal (Phase 2)
│
├── platform/               ← hardware peripheral FBs + per-target implementations
│   ├── posix/              ← fb_uart, fb_nvm, fb_timer (POSIX implementations)
│   └── esp32/              ← fb_uart, fb_nvm, fb_timer (ESP32 — Phase 2)
│
├── components/             ← reusable application FBs (RTOS-agnostic)
│   └── fb_*/               ← fb_cloud_mqtt, fb_ota, fb_telemetry, fb_provisioning
│
├── contrib/                ← community contributions
│   ├── platform/
│   ├── components/
│   └── CONTRIB_GUIDE.md
│
├── examples/
│   ├── thermostat/         ← style reference for application FBs
│   └── industrial_gateway/
│
├── test/
│   ├── embediq_test.h      ← test infrastructure (excluded from release)
│   └── scenarios/
│
├── tools/
│   ├── check_invariants.sh ← run this before every PR
│   ├── validator.py        ← validates embediq_config.h
│   └── gen_msg_catalog.py  ← messages.iq → embediq_msg_catalog.h
│
└── docs/
    ├── architecture/
    ├── getting_started/
    └── migration/mbed/
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

---

## 6. File Placement Rules — Where Every File Lives

This table is binding. When implementing any module, use EXACTLY these paths.
Never place .c files flat in core/src/ — they belong in their subdirectory.

| Module              | File path                                     |
|---------------------|-----------------------------------------------|
| FB engine           | core/src/registry/fb_engine.c                 |
| Message bus         | core/src/bus/message_bus.c                    |
| FSM engine          | core/src/fsm/fsm_engine.c                     |
| Dispatcher          | core/src/dispatcher/dispatcher.c              |
| Observatory         | core/src/observatory/obs.c                    |
| OSAL POSIX          | osal/posix/embediq_osal_posix.c               |
| OSAL FreeRTOS       | osal/freertos/embediq_osal_freertos.c         |
| Platform POSIX FBs  | platform/posix/fb_<name>.c                    |
| Platform ESP32 FBs  | platform/esp32/fb_<name>.c                    |
| Components          | components/<fb_name>/<fb_name>.c              |
| Unit tests          | tests/unit/test_<module>.c                    |

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

> **Last updated:** Phase 1 complete (March 2026)
> This table is updated at milestone boundaries only.

| Layer | Module | Status | Notes |
|-------|--------|--------|-------|
| Core | All 13 headers | STABLE | Contracts frozen. Never change. |
| Core | messages.iq generator | STABLE | Python, zero deps. core.iq + thermostat.iq live. |
| Core | embediq_config.h | STABLE | All sizing constants. Use named constants only. |
| OSAL | posix (macOS + Linux + WSL) | STABLE | pthreads + POSIX. Phase 1 complete. |
| OSAL | freertos | NOT_STARTED | Phase 2 |
| Core / Engine | FB Registry + Dispatch | STABLE | embediq_engine_boot() + embediq_engine_dispatch_boot() |
| Core / Engine | Message Bus | STABLE | 3-queue routing, overflow policy, observatory drops |
| Core / Engine | FSM Engine | STABLE | Table-driven, guard/action, observatory events |
| Core / Engine | Observatory | STABLE | Ring buffer, stdout transport, level 0/1/2 filtering |
| Platform | fb_timer (posix) | STABLE | Drift-corrected, MSG_TIMER_1SEC/100MS/10MS/1MS |
| Platform | fb_watchdog | STABLE | Health-token model, 100ms check interval |
| Platform | fb_nvm | STABLE | Atomic JSON key-value store, ~/.embediq/ |
| Platform | fb_cloud_mqtt | NOT_STARTED | Phase 2 |
| Platform | fb_ota | NOT_STARTED | Phase 2 |
| Examples | thermostat | STABLE | 5 FBs, FSM cycles, Observatory output, zero printf |

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

| Decision | Choice | Do Not Change |
|----------|--------|---------------|
| Build system | CMake | Yes — locked |
| Core language | C11 | Yes — locked |
| License | Apache 2.0 | Yes — locked |
| v1 executor | Dedicated OS thread per FB | Yes — locked |
| v1 message passing | Copy-by-value | Yes — locked |
| v1 message priority | 3 queues per FB (HIGH/NORMAL/LOW) | Yes — locked |
| messages.iq v0 | msg_id + name + payload_size + schema_id | Yes — locked |
| Timestamp on MCU | uint32_t microseconds, wraps ~71 min | Yes — locked |
| Core header freeze | After thermostat + Observatory + Test Runner pass on host | Pending |

---

## 12. Where to Go Next

| What you need | Where to find it |
|---------------|-----------------|
| All coding rules + invariants + forbidden patterns | `CODING_RULES.md` |
| Layer-specific API surface and status | `{layer}/LAYER.md` |
| Module-specific contract, tests, dependencies | `{layer}/{module}/MODULE.md` |
| Complete header contracts with examples | `core/include/*.h` |
| Style reference for application FBs | `examples/thermostat/` |
| Style reference for Platform FBs | `platform/posix/fb_uart/` |
| How to add a new OSAL | `osal/OSAL_GUIDE.md` |
| How to add a new Platform target | `platform/PLATFORM_GUIDE.md` |
| How to contribute a new FB | `contrib/CONTRIB_GUIDE.md` |
| Run invariant checks locally | `tools/check_invariants.sh` |

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

*EmbedIQ — embediq.com — Apache 2.0*
*Agents: re-read this file at the start of every new task session.*
