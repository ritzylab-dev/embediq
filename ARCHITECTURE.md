# ARCHITECTURE.md — EmbedIQ Technical Reference

**Read this before writing any code. Reference it while coding.**

This document covers the complete EmbedIQ architecture. For AI agents and
contributors: also read [AGENTS.md](AGENTS.md) and [CODING_RULES.md](CODING_RULES.md).

---

## How to Use This Document

| Section | Content | When to Read |
| ------- | ------- | ------------ |
| Four Core Principles | Three core sentences + CI-enforced rules | Before any architectural decision |
| Layer Model | Complete stack diagram + layer definitions | Before writing any code |
| Library Architecture | Integration pattern for third-party components | Phase 2+ work |
| The Functional Block | FB model, lifecycle, sub-functions | Before writing any FB |
| Message System | Envelope, three-queue model, messages.iq | Before defining any message |
| Core Headers | All contract header interfaces | During all implementation work |
| Sub-function Model | Registration flow, FSM pattern | Before writing any sub-function |
| ISR Boundary | Two-zone model, ring buffer pattern | Before writing any Driver FB |
| Boot Phase Model | Startup sequencing, boot phase ordering | Before writing multi-FB systems |
| Observatory | Event system, trace levels, .iqtrace format | Before writing any observable code |
| Cross-Layer Observability | XOBS Phase 2 OSAL/HAL event design | Phase 2+ implementation |
| AI-First Architecture | AI event constants, TLV, AI code review gate | AI-related FBs and tooling |
| Platform FB Specs | fb_uart, fb_watchdog, fb_nvm contracts | Before implementing Platform FBs |
| Architecture Validation | Three use-case validation records | For system-level understanding |
| Responsibility Register | What EmbedIQ owns vs. developer owns | Before any boundary decision |
| Non-Goals | Explicit de-scope list | Before any implementation work |
| AI Coding Checklist | Rule pairs: correct vs. incorrect | Before every PR |

---

## Four Core Principles

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
  Fixed contracts between layers mean cross-FB coupling, hardware dependencies in
  application code, and untyped data passing cannot be hidden — they fail
  compilation, fail CI, or are obvious in code review.
  Structural discipline is enforced by the architecture, not hoped for.
```

---

## Binding Rules — CI Enforced

| Rule | Statement | Enforcement |
| ---- | --------- | ----------- |
| R-01 | No cross-FB function calls. Only messages through the bus. | Code review + static analysis |
| R-02 | No dynamic allocation in Layer 1 or Core. | CI: no malloc/free symbols in Layer 1 objects |
| R-03 | C11 for Core and Layer 1. C++ opt-in wrapper only in Layer 3+. | CMake compiler flag |
| R-04 | Apache 2.0 only in Core/Layer 1/Layer 2. No GPL or LGPL. | CI license scanner |
| R-05 | Every layer: one interface header, one or more impl directories. | Directory structure enforced |
| R-06 | Every module: EmbedIQ_Module_Meta_t descriptor. | meta_scanner validates at build |
| R-07 | Test infrastructure compiles to zero bytes in production. | CI: zero test symbols in release binary |
| R-08 | Core headers compile standalone — no OSAL or platform headers. | CI: compile Core headers in isolation |
| R-09 | Native bus never depends on Bridge. | CI: compile without bridge flag |
| R-10 | messages.iq is the only authoritative payload schema source. | Tooling: all payload structs generated |
| R-11 | All sizing in embediq_config.h. Validated by validator.py at build. | CI: validator.py runs before compile |
| R-12 | Sub-function registrations only inside FB init_fn. When multiple sub-functions subscribe to the same message, execution order = registration order (init_order ascending). Deterministic, not configurable at runtime. | Code review + static analysis |

---

## Layer Model

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  LAYER 4 — COMMERCIAL (future)                                              │
│  Visual tooling  ·  Cloud connectivity  ·  IDE and AI integrations          │
├─────────────────────────────────────────────────────────────────────────────┤
│  LAYER 3 — ECOSYSTEM                                                        │
│  Bridge daemon  ·  bridge/websocket  ·  bridge/unix_socket                  │
│  Registry  ·  Community BSPs  ·  3rd-party FB wrappers                      │
│  External FBs (Python · Node.js · Java · any language)                      │
├─────────────────────────────────────────────────────────────────────────────┤
│  LAYER 2 — DRIVER FBs (Apache 2.0)                                         │
│  fb_uart · fb_timer · fb_gpio · fb_i2c · fb_spi                             │
│  fb_watchdog (basic) · fb_nvm (basic)                                        │
│  Pro FBs (commercial licence): fb_ota · fb_cloud_mqtt · …                  │
│  → embediq.com/pro                                                           │
├─────────────────────────────────────────────────────────────────────────────┤
│  LAYER 1 — FRAMEWORK ENGINE                  (all Apache 2.0)               │
│  FB Registry  ·  Endpoint Router  ·  Message Bus (3-queue)                  │
│  Sub-fn Dispatcher  ·  FSM Engine  ·  Observatory                           │
│  Test Runner [TEST BUILDS ONLY]                                             │
├─────────────────────────────────────────────────────────────────────────────┤
│  CONTRACTS  (core/include/ — header-only · frozen post-v1)                  │
│  embediq_fb.h  ·  embediq_subfn.h  ·  embediq_bus.h                         │
│  embediq_msg.h  ·  embediq_sm.h  ·  embediq_obs.h                           │
│  embediq_osal.h  ·  embediq_time.h  ·  embediq_bridge.h                     │
│  embediq_meta.h  ·  embediq_endpoint.h  ·  embediq_msg_catalog.h            │
│  embediq_nvm.h  ·  embediq_timer.h  ·  embediq_wdg.h                        │
│  embediq_ota.h  ·  embediq_mqtt.h                                           │
│  hal/ (HAL contract headers — see HAL section below)                        │
├─────────────────────────────────────────────────────────────────────────────┤
│  HAL — Hardware Abstraction Layer                                           │
│  Contracts: core/include/hal/  (hal_uart.h · hal_gpio.h · etc.)             │
│  Implementations: hal/posix/ · hal/esp32/ · hal/stm32/                      │
├─────────────────────────────────────────────────────────────────────────────┤
│  OSAL — OS Abstraction Layer                                                │
│  Contract: core/include/embediq_osal.h                                      │
│  Implementations: osal/posix/ · osal/freertos/ · osal/zephyr/               │
├─────────────────────────────────────────────────────────────────────────────┤
│  SUBSTRATE                                                                  │
│  FreeRTOS  ·  Linux (gateway/edge)  ·  Zephyr  ·  bare-metal MCU  ·  POSIX  │
└─────────────────────────────────────────────────────────────────────────────┘
```

**Layer dependency rule:** Each layer may only depend on the layer directly below it.
Layer 2 may call Layer 1 APIs. Layer 2 must NOT call Core internals or skip Layer 1.
Agents: never add an include that skips a layer.

---

## Layer Definitions

### Layer 1 — Framework Engine

The runtime that runs all FBs. Everything in Layer 1 is platform-agnostic and
compiles to any target. Layer 1 contains:

- **FB Registry** — static table of all registered FBs. Max EMBEDIQ_MAX_FB_COUNT (default 32, max 254).
- **Endpoint Router** — routes messages from publish() to all subscribed FB queues.
- **Message Bus (3-queue)** — per-FB HIGH/NORMAL/LOW priority queues with defined overflow policy.
- **Sub-fn Dispatcher** — routes received messages to registered sub-functions in init_order.
- **FSM Engine** — table-driven state machine execution for sub-functions that declare one.
- **Observatory** — zero-instrumentation event capture via bus tap. All lifecycle and message events are auto-captured.
- **Test Runner** — [TEST BUILDS ONLY] compiles to zero bytes in production. CI enforces this.

### Layer 2 — Driver FBs and Service FBs

All FBs that wrap hardware or provide reusable platform services. Two distinct sub-types:

**Driver FB** — wraps a hardware peripheral into the EmbedIQ message model.
- Calls HAL contract (`core/include/hal/`) for hardware access
- Calls OSAL for threading and ISR wakeup
- One portable source file (`fbs/drivers/fb_uart.c`) — target differences are handled entirely inside the HAL implementation
- Examples: `fb_uart`, `fb_gpio`, `fb_timer`, `fb_nvm`, `fb_watchdog`

**Service FB** — platform-agnostic reusable service.
- Does NOT call HAL directly
- Calls an ops table (`embediq_ota_ops_t`, `embediq_mqtt_ops_t`) that is registered by the platform at boot — this is its hardware abstraction
- One source file runs on all targets unchanged
- Examples: `fb_cloud_mqtt`, `fb_ota`, `fb_telemetry`, `fb_logger`

**Rule:** If an FB directly accesses hardware or OS-provided device drivers, it is a
Driver FB. If it accesses hardware only through an ops table, it is a Service FB.

### Layer 3 — Ecosystem

The bridge and registry layer. Layer 3 components run at the lowest priority —
they are best-effort. External FBs are never allowed to block native bus throughput.

- **Bridge daemon** — manages WebSocket/Unix socket connections between native bus and external processes
- **bridge/websocket, bridge/unix_socket** — transport implementations
- **Registry** — community FB discovery and manifest validation
- **Community BSPs** — third-party board support packages following the EmbedIQ BSP convention
- **3rd-party FB wrappers** — adapters that expose non-EmbedIQ libraries as properly-contracted FBs
- **External FBs** — Python, Node.js, Java, or any language process communicating via Bridge

### Layer 4 — Commercial

Layer 4 sits above the Apache 2.0 open-source boundary. It provides visual tooling,
cloud connectivity, and IDE and AI integrations built on the open Observatory data
substrate. Layer 4 is not described in this document. See commercial documentation.

### HAL — Hardware Abstraction Layer

HAL sits just above hardware. It provides thin C functions that abstract raw MCU
peripheral access. No FBs. No messages. No RTOS calls.

```c
hal_gpio_set(pin, value);
hal_uart_write(port, buf, len);
hal_flash_erase(addr, size);
```

HAL contracts live in `core/include/hal/`. Implementations live in:
- `hal/posix/` — POSIX (file I/O, termios, `clock_gettime`)
- `hal/esp32/` — ESP-IDF peripheral APIs
- `hal/stm32/` — STM32Cube HAL (Phase 3+)

Driver FBs call HAL. HAL implementations call vendor BSP or OS APIs.
Vendor BSPs (ESP-IDF, STM32Cube, NRF SDK) are not owned by EmbedIQ.

### OSAL — OS Abstraction Layer

OSAL abstracts the operating system — threads, queues, semaphores, mutexes, signals.
It does NOT abstract hardware peripherals (that is HAL).
Contract: `core/include/embediq_osal.h`.

```c
EmbedIQ_Task_t*  embediq_osal_task_create(const char* name, void(*fn)(void*), void* arg,
                                          uint8_t priority, uint16_t stack_bytes);
EmbedIQ_Queue_t* embediq_osal_queue_create(uint16_t depth, uint16_t item_size);
void             embediq_osal_queue_destroy(EmbedIQ_Queue_t* q);
bool             embediq_osal_queue_send(EmbedIQ_Queue_t* q, const void* item, uint32_t timeout_ms);
bool             embediq_osal_queue_recv(EmbedIQ_Queue_t* q, void* item, uint32_t timeout_ms);
EmbedIQ_Signal_t* embediq_osal_signal_create(void);
void              embediq_osal_signal_wait(EmbedIQ_Signal_t* sig);
void              embediq_osal_signal_from_isr(EmbedIQ_Signal_t* sig); // ISR-safe
```

### Native FB vs. External FB

**Native FB** — written in C, running inside the EmbedIQ runtime. All FBs in this
document are Native FBs unless stated otherwise.

**External FB** — running outside the EmbedIQ runtime as a separate process (Python,
Node.js, Java, any language). Communicates via the Bridge interface (Layer 3). Appears
as a named endpoint on the bus. External FBs boot in Phase 4 — BRIDGE.

**Rule:** `FB` alone means Native FB. `External FB` is always written in full.

---

## Library Architecture

### Design

Phase 2 introduces the standard integration path for third-party and platform components.
Every library that ships with or integrates with EmbedIQ follows this pattern.

**D-LIB-1: Libraries are not FBs.**
A library provides services through an ops table (function pointer struct). An FB owns a
library instance, calls it within its sub-function, and exposes results to the bus via
typed messages. A library has no message-bus identity.

**D-LIB-2: Every library declares itself via `platform_lib_declare()` before `PLATFORM_INIT` completes.**
This gives each library an Observatory source ID and makes initialization observable at
the platform level.

**D-LIB-3: Third-party source code lives exclusively in `third_party/<name>/` and is never modified.**
Wrapper and integration code lives in `components/<name>/`. This boundary is enforced by CI.

**D-LIB-4: The ops table is the integration contract.**
A library exposes an `embediq_<name>_ops_t` struct. The FB calls only through this struct —
the same pattern as the HAL contract, applied one layer up.

**D-LIB-5: `EMBEDIQ_OPS_STRICT_VERSION` fields in the ops table allow version compatibility to be verified at compile time.**
Mismatch is a build error, not a runtime discovery.

### Library Classification Rule

Every third-party library falls into exactly one category:

```
Pure utility (no state, no platform dependency)
    → COMPONENT  (components/)

Stateful capability with multiple platform implementations
    → OPS TABLE  (contract in core/include/ops/,  impl in hal/<target>/ops/)

Platform-coupled runtime (ISR-level, vendor SDK lock-in)
    → PLATFORM RUNTIME  (hal/<target>/runtime/ — FB never sees it)

Third-party complete FB
    → EXTERNAL FB  (Layer 3 — bridge protocol)
```

Tie-breaker for stateful single-implementation libraries: if state is caller-allocated,
it is a Component. If state is heap-allocated with no static alternative, it is an Ops
Table with a single implementation.

### Phase 2 MCU Observatory Design Constraints

The MCU Observatory implementation (Phase 2) must satisfy these constraints. Every
contributor writing MCU-targeted observation code must read this before touching
the Observatory ring buffer.

- **Ring buffer:** Lock-free, ISR-reentrant write. Two compile-time paths:
  - ARMv7-M / ARMv8-M (Cortex-M3, M4, M7, M33): LDREX/STREX atomic compare-and-swap.
  - ARMv6-M (Cortex-M0, M0+): CPSID/CPSIE interrupt disable — no LDREX/STREX on these cores.
  - Compile-time dispatch: `#if defined(__ARM_ARCH_6M__)` in `hal_obs_ring.c`.
- **Transport:** Dedicated observer task drains the ring. Never inline with `emit()`.
- **`emit_from_isr()` naming:** Exists so HAL ISR call sites can be audited by grep.
- **Overflow:** Silent drop in ISR path. Observer task records OVERFLOW event (0x10) after
  ring recovery. ISR does zero overflow handling.

### SYSTEM Band Admission Policy

The SYSTEM band (0x10–0x1F) accepts lifecycle events only. Admission criteria:

- Lifecycle events: power-cycle, OTA, watchdog reset, maintenance mode, firmware start, config load, AI model lifecycle.
- 14 of 16 slots are taken. 0x1E and 0x1F are held as genuine reserve.
- Library initialization is a resource acquisition event — it belongs in the RESOURCE band (0x40+), not SYSTEM.
- Any proposed SYSTEM band addition must answer: "Is this a system lifecycle event?" If no, it belongs in a different band.

### 16-Slot Library Source Named Constraint

`EMBEDIQ_LIB_SRC_*` occupies `0xE0–0xEF`: a maximum of 16 instrumented library sources
per firmware build. This is a deliberate named constraint, not an accidental limit.

One firmware build has a fixed, finite set of libraries. Only libraries that emit
Observatory events receive a source ID. In practice this count is always well under 16.
`platform_lib_declare()` enforces this with a `_Static_assert` at build time. Attempting
to register a 17th library source is a build error with a clear diagnostic message.

---

## The Functional Block

The FB is the unit of everything. Every concern in your firmware is one FB.

| Property | What it means |
| -------- | ------------- |
| Owns state exclusively | Private `fb_data` struct. No globals. No shared memory across FB boundaries. |
| Communicates by message only | Never calls another FB directly. Intra-FB helpers are normal C. |
| Single execution context | Executes sequentially. Actor model. No concurrent execution within one FB. No locks needed. |
| Executor policy v1 | Dedicated OS thread per FB. |
| Explicit lifecycle | `IDLE → INITIALISING → RUNNING → FAULT → STOPPING → STOPPED`. Every transition observable. |
| Observable by default | All lifecycle, bus messages, FSM transitions auto-captured. Zero developer code. |
| Testable by design | Message-only cross-FB interface: inject messages, assert on output. No hardware required. |
| Composed of sub-functions | Internal structure from sub-functions, each with subscriptions and optional FSM. |

### FB Registration

```c
// ALWAYS use these macros. NEVER use compound literals for subscription arrays.
#define EMBEDIQ_SUBS(name, ...) static const uint16_t name[] = { __VA_ARGS__ }
#define EMBEDIQ_PUBS(name, ...) static const uint16_t name[] = { __VA_ARGS__ }

typedef struct {
    const char*           name;
    uint8_t               priority;      // OS thread priority
    uint16_t              stack_size;    // OS stack bytes
    embediq_fb_init_fn    init_fn;
    embediq_fb_run_fn     run_fn;        // catch-all; NULL if sub-fns cover all
    embediq_fb_exit_fn    exit_fn;
    embediq_fb_fault_fn   fault_fn;
    void*                 fb_data;
    const uint16_t*       subscriptions;
    uint8_t               subscription_count;
    const uint16_t*       publications;
    uint8_t               publication_count;
    const char * const   *depends_on;   // null-terminated array of FB names
    uint8_t               depends_count;
    EmbedIQ_BootPhase_t   boot_phase;   // default = APPLICATION if not declared
} EmbedIQ_FB_Config_t;

EmbedIQ_FB_Handle_t* embediq_fb_register(const EmbedIQ_FB_Config_t* config);
void embediq_publish(EmbedIQ_FB_Handle_t* fb, EmbedIQ_Msg_t* msg);
void embediq_fb_report_fault(EmbedIQ_FB_Handle_t* fb, uint32_t reason);
uint8_t embediq_bus_resolve_name(const char* name);
```

### Endpoint Registry

Every FB is an Endpoint. The registry is a static table (max 255 entries, configurable
in `embediq_config.h`).

| Rule | Specification |
| ---- | ------------- |
| ID assignment | Sequential, assigned by FB registry at registration time. Deterministic per boot. |
| Reserved IDs | 0 = system/framework · 1–254 = FBs (native + external) · 255 = broadcast |
| Stability | IDs are NOT stable across boots in v1. Observatory exports name→ID map at session start. |
| Max FB count | `EMBEDIQ_MAX_FB_COUNT` in `embediq_config.h` — default 32, max 254. |
| Name → ID resolution | Static registry table. `embediq_bus_resolve_name(const char* name) → uint8_t id`. |
| External/Bridge endpoints | Get IDs from the same pool, assigned when Bridge connects. Same rules apply. |
| Observer mapping | Observatory events carry endpoint ID. Resolved to name via registry export. |

### Fault and Error Patterns

```
embediq_fb_report_fault(fb, reason) — use when the FB cannot continue operating normally.
  Examples: OSAL queue create failed, BSP init failed, required config missing.
  Effect: FB transitions to FAULT state. fb_watchdog handles recovery policy.

embediq_publish(MSG_ERROR_*) — use when FB encountered a recoverable problem.
  Examples: timeout waiting for a response, invalid payload received, resource temporarily unavailable.
  Effect: FB continues operating. Interested FBs react to the error message.

Rule: If the FB can continue → MSG_ERROR_*. If it cannot → report_fault().
```

| Canonical Error Message ID | When to Use | Payload Fields |
| -------------------------- | ----------- | -------------- |
| MSG_ERROR_TIMEOUT (0x0050) | Expected response not received within deadline | source_fb_id, operation_id, timeout_ms |
| MSG_ERROR_INVALID_PAYLOAD (0x0051) | Received message with malformed or out-of-range payload | source_fb_id, msg_id, reason_code |
| MSG_ERROR_CONFIG (0x0052) | Configuration parameter invalid or missing | source_fb_id, param_key[16], reason_code |
| MSG_ERROR_RESOURCE (0x0053) | Required resource temporarily unavailable | source_fb_id, resource_id, retry_after_ms |

---

## Contract Ownership Table

ALL contracts live in `core/include/`. A contract lives with the interface it defines,
not with the layer that implements it.

| Contract file | Defines interface for | Consumed by |
| ------------- | --------------------- | ----------- |
| `embediq_fb.h` | FB registration + lifecycle | All layers |
| `embediq_bus.h` | Message publish/receive | All layers |
| `embediq_endpoint.h` | Endpoint registration | Layer 1, Layer 3 |
| `embediq_osal.h` | OS primitives | Layer 1, Layer 2 |
| `embediq_obs.h` | Observatory events | All layers |
| `embediq_sm.h` | FSM table contract | Layer 2 (Driver FBs) |
| `embediq_time.h` | Monotonic + wall clock | All layers |
| `embediq_nvm.h` | NVM key-value store | Layer 2+, Application |
| `embediq_timer.h` | Timer tick messages | Layer 2+, Application |
| `embediq_wdg.h` | Watchdog checkin | Layer 2+, Application |
| `embediq_ota.h` | OTA ops table + FSM | Layer 2 Service FB |
| `embediq_mqtt.h` | MQTT ops table + FSM | Layer 2 Service FB |
| `embediq_meta.h` | FB metadata | Layer 3 Registry |
| `embediq_bridge.h` | External FB bridge | Layer 3 |
| `core/include/hal/*.h` | HAL peripheral access | Layer 2 Driver FBs |
| `core/include/hal/hal_obs_stream.h` | Observatory binary stream | Layer 1 Observatory |

---

## Layer Boundary Rules

Enforced by `tools/boundary_checker.py` in CI. Violations = CI failure.

| Source location | May include | Must NOT include |
| --------------- | ----------- | ---------------- |
| `core/src/` | `core/include/`, system headers | `hal/`, `osal/`, `platform/`, `fbs/` |
| `osal/posix/` | `core/include/`, system + OS headers | `hal/`, `fbs/`, `examples/` |
| `osal/freertos/` | `core/include/`, FreeRTOS headers | `hal/`, `fbs/`, `examples/` |
| `hal/posix/` | `core/include/hal/`, system + OS headers | `osal/`, `core/src/`, `fbs/` |
| `hal/esp32/` | `core/include/hal/`, ESP-IDF headers | `osal/`, `core/src/`, `fbs/` |
| `fbs/drivers/` | `core/include/` (incl. `hal/`), C stdlib | `hal/` impl paths, vendor BSP, `examples/` |
| `fbs/services/` | `core/include/` (excl. `hal/`), C stdlib | `core/include/hal/`, `hal/`, `osal/` impl |
| `examples/` | `core/include/`, `fbs/drivers/`, `fbs/services/` | `hal/` impl headers, `osal/` impl |
| `tests/` | `core/include/`, `examples/`, `fbs/` contracts | `hal/` impl, `osal/` impl |

---

## Message System

### Envelope — `EmbedIQ_Msg_t`

20-byte fixed header + opaque payload. The `static_assert` is the single source of truth:

```c
// embediq_msg.h
typedef struct {
    uint16_t msg_id;              // message type ID (from messages.iq)
    uint8_t  source_endpoint_id;  // sending FB (0=system)
    uint8_t  target_endpoint_id;  // 0xFF=broadcast
    uint16_t schema_id;           // payload schema ID from messages.iq
    uint16_t payload_len;
    uint32_t correlation_id;      // request/reply matching; 0=unused
    uint32_t timestamp_us;        // uint32_t microseconds — WRAPS at ~71 min
    uint8_t  priority;            // HIGH(0) / NORMAL(1) / LOW(2)
    uint8_t  _reserved[3];
    uint8_t  payload[EMBEDIQ_MSG_MAX_PAYLOAD]; // default 64 bytes
} EmbedIQ_Msg_t;

static_assert(offsetof(EmbedIQ_Msg_t, payload) == 20, "header must be 20 bytes");
```

Total message size: 20 + EMBEDIQ_MSG_MAX_PAYLOAD (default = 84 bytes). v1 message
passing is copy-by-value at every queue send. Zero-copy pool is a Phase 2+ non-goal.

### Three-Queue Priority Model

Each FB has three independent queues: `HIGH`, `NORMAL`, `LOW`. Drain order each
cycle: HIGH fully → NORMAL fully → one from LOW (fairness).

Message priority and FB thread priority are **independent axes**. A high-priority
message to a low-priority FB enters the HIGH queue and is processed at that FB's
OS scheduling priority.

Queue depth defaults in `embediq_config.h`:

```c
#define EMBEDIQ_FB_QUEUE_DEPTH_HIGH   4   // small — only urgent messages
#define EMBEDIQ_FB_QUEUE_DEPTH_NORMAL 16  // default working queue
#define EMBEDIQ_FB_QUEUE_DEPTH_LOW    8   // background / logging
```

FB thread drain order (Layer 1 engine — never change this):
1. Drain HIGH queue completely
2. Drain NORMAL queue completely
3. Drain one message from LOW queue (fairness)
4. If all queues empty: block on OSAL event (any queue)

### Queue Overflow Policy

Undefined overflow behavior is a production bug class. This policy is binding:

| Queue | Behavior when full | Observatory event? |
| ----- | ------------------ | ------------------ |
| HIGH | **Block sender** — HIGH messages are safety-critical, dropping is worse than blocking | No |
| NORMAL | **Drop oldest** — recent data is more valuable than stale data | Yes |
| LOW | **Drop incoming** — best-effort delivery | Yes |

### Message Dispatch Flow

```
embediq_publish(fb, msg)

1. Assign source_endpoint_id from FB registry
2. bus_tap(EVENT_MSG_TX, msg) ← TX tap fires HERE (message queued, not yet delivered)
3. Route: look up msg_id in subscription table → get target endpoint list
4. For each target endpoint:
     msg.priority → which of 3 queues: HIGH(0) / NORMAL(1) / LOW(2)
     embediq_osal_queue_send(fb->queue[priority], &msg)

[Later — in target FB thread]
  Drain HIGH queue fully → drain NORMAL fully → drain LOW (one per cycle)
  bus_tap(EVENT_MSG_RX, msg) ← RX tap fires HERE (about to dispatch to sub-fns)
  Look up msg_id in routing table → dispatch to sub-fn list
```

### Message ID Namespace

Message IDs are a global ABI. Two FBs with conflicting IDs silently corrupt each
other's data.

| Range | Owner |
| ----- | ----- |
| 0x0000 – 0x03FF (0–1023) | EmbedIQ Core system — framework internal only |
| 0x0400 – 0x13FF (1024–5119) | Official EmbedIQ components — assigned by embediq/embediq |
| 0x1400 – 0xFFFF (5120–65535) | Community / third-party — must reserve in `messages_registry.json` |

Community FBs **must** reserve a range in `messages_registry.json` before publishing.
No reservation = undefined collision risk.

### messages.iq — Schema System

All payload schemas are declared in `.iq` schema files under `messages/`. The generator
(`tools/messages_iq/generate.py`) produces C headers committed to `generated/`. No
hand-written payload structs anywhere in the codebase.

**Two phases — design time and build time:**

Design time (developer action — not cmake):
- Edit or create a schema in `messages/<name>.iq`
- Run: `python3 tools/messages_iq/generate.py messages/<name>.iq --out generated/ --output-name <name>_msg_catalog.h`
- Commit the `.iq` change and the regenerated `.h` in the same PR
- CI drift-check enforces they match — fails if committed `.h` does not reflect the current `.iq`

Build time (cmake — no Python required):
- Generated headers are committed and present after `git clone`
- cmake compiles them directly — generator is never invoked by cmake
- Works on any platform, any CI container, zero additional tooling

**Why generated headers are committed, not gitignored:**
Generated headers encode binary wire protocol contracts — message IDs and payload layouts.
Once firmware ships with a message ID, that ID is frozen for that hardware generation.
Committing the headers gives a full git-blame audit trail on every ID change and makes
every protocol change visible in PR diffs.

**Versioning rules:**
- IDs are never reused. A retired message ID is tombstoned.
- Changes are additive only. Never remove, rename, or reorder existing fields.
- Any change increments `schema_id`.
- AI agents: always add new fields at the END. Always increment `schema_id`.

### Large Payload — Chunked Message Pattern

For payloads larger than `EMBEDIQ_MSG_MAX_PAYLOAD` (e.g. OTA firmware chunks,
data logs): use the chunked message pattern. No new API — uses existing
`EmbedIQ_Msg_t` with structured payload.

```c
typedef struct {
    uint32_t transfer_id;     // unique transfer; same as correlation_id in header
    uint32_t chunk_offset;    // byte offset of this chunk in the full payload
    uint32_t total_length;    // total transfer size in bytes
    uint16_t chunk_length;    // bytes in data[] for this chunk
    uint8_t  chunk_index;
    uint8_t  total_chunks;
    uint8_t  data[EMBEDIQ_CHUNK_DATA_SIZE]; // EMBEDIQ_MSG_MAX_PAYLOAD - 14
} Payload_Chunk_t;

// MSG_CHUNK_BEGIN, MSG_CHUNK_DATA, MSG_CHUNK_END defined in msg_catalog.h.
// Receiver assembles chunks using chunk_offset + chunk_length.
```

### messages.iq — Schema Syntax

```
// messages.iq — example schema file
version 1

namespace "com.example.myproject"

message TEMP_READING id=0x1001 {
    temperature_c : f32
    humidity_pct  : f32
    sensor_id     : u8
}

message TEMP_ALERT id=0x1002 {
    temperature_c : f32
    alert_level   : u8    // 1=warning 2=critical 3=emergency
    sensor_id     : u8
}
```

The generator emits a `_Static_assert` for every schema payload, closing the gap
between schema definition and runtime correctness at zero runtime cost:

```c
// Generated by messages.iq for MSG_TEMP_READING:
static_assert(sizeof(MSG_TEMP_READING_Payload_t) <= EMBEDIQ_MSG_MAX_PAYLOAD,
    "MSG_TEMP_READING exceeds EMBEDIQ_MSG_MAX_PAYLOAD");
static_assert(offsetof(MSG_TEMP_READING_Payload_t, temperature_c) == 0,
    "MSG_TEMP_READING layout changed — increment schema_id");
```

`messages_registry.json` format:

```json
{
  "core":     { "range": [0, 1023],    "owner": "embediq/embediq" },
  "official": {
    "fb_cloud_mqtt": { "range": [1024, 1099], "owner": "embediq/embediq-pro" },
    "fb_ota":        { "range": [1100, 1149], "owner": "embediq/embediq-pro" },
    "fb_nvm":        { "range": [1150, 1199], "owner": "embediq/embediq-pro" }
  },
  "community": {
    "fb_modbus_rtu": { "range": [5120, 5179], "owner": "github.com/user/repo" }
  }
}
```

Rule: message IDs are never reused. A retired ID is tombstoned in `messages_registry.json`.

### FB Package Manifest

Every distributable FB includes `embediq_manifest.json`. The FB package system
(`embediq registry`, `embediq add`) is Phase 2 implementation, but the manifest
format is a Phase 1 design constraint — it must not contradict Phase 1 choices.

```json
{
  "name": "fb_modbus_rtu",
  "version": "1.0.0",
  "author": "github.com/user/repo",
  "license": "Apache-2.0",
  "osal": ["freertos", "posix"],
  "layer": 2,
  "msg_range": [5120, 5179],
  "depends": ["fb_uart"],
  "boot_phase": 2
}
```

Rules: every FB published to the community registry MUST include `embediq_manifest.json`.
`msg_range` MUST match a reserved range in `messages_registry.json`. `license` MUST be
`Apache-2.0` for inclusion in the official registry.

---

## Core Headers

### `embediq_sm.h`

```c
typedef struct {
    uint8_t                current_state;
    uint16_t               event_msg_id;
    embediq_sm_guard_fn    guard_fn;    // NULL = always match
    embediq_sm_action_fn   action_fn;   // NULL = no side effect
    uint8_t                next_state;
} EmbedIQ_SM_Row_t;

#define EMBEDIQ_SM_TABLE_END { 0xFF, 0xFFFF, NULL, NULL, 0xFF }
```

### `embediq_osal.h`

Opaque handle types only. No RTOS headers in Core.

```c
EmbedIQ_Task_t*  embediq_osal_task_create(const char* name, void(*fn)(void*), void* arg,
                                          uint8_t priority, uint16_t stack_bytes);
EmbedIQ_Queue_t* embediq_osal_queue_create(uint16_t depth, uint16_t item_size);
void             embediq_osal_queue_destroy(EmbedIQ_Queue_t* q);
bool             embediq_osal_queue_send(EmbedIQ_Queue_t* q, const void* item, uint32_t timeout_ms);
bool             embediq_osal_queue_recv(EmbedIQ_Queue_t* q, void* item, uint32_t timeout_ms);
EmbedIQ_Signal_t* embediq_osal_signal_create(void);
void              embediq_osal_signal_wait(EmbedIQ_Signal_t* sig);
void              embediq_osal_signal_from_isr(EmbedIQ_Signal_t* sig); // ISR-safe
```

---

## Sub-function Model

Sub-functions are the internal structure of an FB. Each has subscriptions, an optional
FSM, and init/run/exit lifecycle.

```c
// Step 1: static const subscription arrays — MANDATORY
EMBEDIQ_SUBS(reader_subs, MSG_TEMP_READING, MSG_TIMER_100MS);
EMBEDIQ_SUBS(monitor_subs, MSG_TEMP_READING);

// Step 2: register in FB init_fn — register() ONLY builds tables, does NOT call init_fn
static void my_fb__init(EmbedIQ_FB_Handle_t* fb, void* fb_data) {
    embediq_subfn_register(fb, &(EmbedIQ_SubFn_Config_t){
        .name="reader", .init_order=0,
        .init_fn=reader__init, .run_fn=reader__run, .exit_fn=reader__exit,
        .subscriptions=reader_subs, .subscription_count=2,
        .fsm=NULL, .osal_signal=NULL,
    });
    // After init_fn returns: framework runs ONE deterministic init pass in init_order.
    // Bus is fully configured before any sub_fn.init_fn() executes.
    // Fan-out: all matching sub-fns invoked in init_order. No stop/consume in v1.
}
```

**Sub-fn routing:** All sub-functions subscribed to a message receive it, in `init_order`
ascending. There is no stop/consume semantic in v1.

**osal_signal:** Used only in Driver FB sub-functions that need ISR wakeup. Not valid
in application FB sub-functions.

---

## ISR-to-Message Boundary

```
INTERRUPT ZONE   (< 10 CPU cycles — ring buffer write + osal_signal_from_isr only)

  UART ISR → byte → ring buffer write → osal_signal_from_isr()

  ─────────────── OSAL signal crosses the boundary ───────────────

THREAD ZONE      (Driver FB thread — full framework access)

  byte_collector sub-fn wakes → drain ring buffer → assemble frame
  → embediq_publish(MSG_UART_FRAME_RX)

APPLICATION ZONE (application FB — never knows hardware exists)
```

**ISR Ring Buffer Overflow — Binding:**
When the ISR ring buffer is full, the behavior MUST be:
1. Drop the oldest byte (overwrite head).
2. Increment a volatile overflow counter (`isr_overflow_count` in `fb_data`).
3. Signal the thread zone via `osal_signal_from_isr()` as normal.
4. Thread zone detects overflow counter change and emits Observatory event.

NEVER: assert, abort, or block inside an ISR. The ISR must always exit in < 10 CPU cycles.

---

## Boot Phase Model

Four phases execute sequentially. All FBs in phase N complete before phase N+1 begins.
This guarantees operational readiness, not merely that `init_fn` was called.

```
PHASE 1 — PLATFORM      fb_uart · fb_timer · fb_gpio · fb_i2c · fb_spi
PHASE 2 — INFRASTRUCTURE  fb_nvm · fb_watchdog · fb_cloud_mqtt · fb_ota
PHASE 3 — APPLICATION    your FBs (default if boot_phase not declared)
PHASE 4 — BRIDGE         External FBs · Bridge connections (best-effort)
```

**Why this exists:** `depends_on` guarantees `init_fn` was called — not that the FB is
operationally ready. Boot phases guarantee phase N is fully operational before phase
N+1 begins. A cloud FB that declares `depends_on={fb_nvm}` without boot phases may still
start before `fb_nvm` has completed its NVM scan.

```c
typedef enum {
    EMBEDIQ_BOOT_PHASE_PLATFORM       = 1,  // fb_uart, fb_timer, fb_gpio
    EMBEDIQ_BOOT_PHASE_INFRASTRUCTURE = 2,  // fb_nvm, fb_watchdog, fb_cloud
    EMBEDIQ_BOOT_PHASE_APPLICATION    = 3,  // developer FBs (default)
    EMBEDIQ_BOOT_PHASE_BRIDGE         = 4,  // External FB connections
} EmbedIQ_BootPhase_t;

// Declaration in FB config:
.boot_phase = EMBEDIQ_BOOT_PHASE_INFRASTRUCTURE,  // omit for APPLICATION (default)
```

| Scenario | Result |
| -------- | ------ |
| Phase 1 FB declares `depends_on` Phase 2 FB | BOOT FAULT — detected at startup |
| Phase 3 FB declares `depends_on` Phase 2 FB | VALID — preferred pattern |
| Two Phase 3 FBs with `depends_on` each other | Topological sort resolves order |
| Circular `depends_on` within same phase | BOOT FAULT — cycle detected |
| FB with no `boot_phase` declared | Defaults to Phase 3 (APPLICATION) |

**Rule:** `boot_phase` takes precedence over `depends_on` for cross-phase ordering.
Within a phase, topological sort on `depends_on` determines order.

---

## Cross-FB Dependency Declaration

When FB_A requires FB_B to be initialised first, declare it:

```c
static const char *cloud_deps[] = { "fb_nvm", "fb_uart", NULL };
EMBEDIQ_FB_DESCRIPTOR(fb_cloud_mqtt, .depends_on = cloud_deps, .depends_count = 2, ...);
```

The framework reads `depends_on` at boot, topologically sorts init order, and faults on
cycles (`FAULT_DEPENDENCY_CYCLE`) or missing dependencies (`FAULT_DEPENDENCY_MISSING`).

---

## Observatory

Zero-instrumentation observability. Every FB state transition and message is captured
automatically via the bus tap. No `printf`. No Percepio. No instrumentation code.

### Event Record — `EmbedIQ_Event_t`

```c
// embediq_obs.h — exactly 14 bytes on all targets
typedef struct {
    uint8_t  event_type;
    uint8_t  source_fb_id;
    uint8_t  target_fb_id;
    uint8_t  state_or_flag;
    uint16_t msg_id;
    uint32_t timestamp_us;   // wraps at ~71 min — use sequence for gap detection
    uint32_t sequence;       // monotonic counter — gap-detection source of truth
} EmbedIQ_Event_t;

static_assert(sizeof(EmbedIQ_Event_t) == 14, "Observatory event size mismatch");
```

The 14-byte size is enforced by `_Static_assert` at compile time. Any change that alters
the record size fails the build immediately on every target. This guarantees:
- A ring buffer of N records always occupies exactly `N × 14` bytes of RAM.
- `.iqtrace` files from any device are structurally identical — decoders need no per-device schema.
- Per-event storage cost is known at compile time and does not vary with payload or compiler version.

### Event Families — Band Table

Every `event_type` encodes its family in its high nibble — no extra field needed.
The family is derived in the decoder with zero runtime overhead:

| Band | Family | Event types |
| ---- | ------ | ----------- |
| 0x10–0x1F | SYSTEM | OVERFLOW(0x10), BOOT(0x11), SHUTDOWN(0x12), WATCHDOG_RST(0x13), HEARTBEAT(0x14), OTA_UPDATE(0x15), CONFIG_CHANGE(0x16), AI_INFERENCE_START(0x17)†, AI_INFERENCE_END(0x18)†, AI_MODEL_LOAD(0x19)†, AI_CONFIDENCE_THRESHOLD(0x1A)†, MAINTENANCE_MODE(0x1B), FW_START(0x1C), CONFIG_LOAD(0x1D) |
| 0x20–0x2F | MESSAGE | MSG_TX(0x20), MSG_RX(0x21), QUEUE_DROP(0x22) |
| 0x30–0x3F | STATE | LIFECYCLE(0x30), FSM_TRANS(0x31) |
| 0x40–0x4F | RESOURCE | LIB_INIT(0x40), LIB_DEINIT(0x41) — Phase 2 activates XOBS-3 (queue depth, stack high-water) |
| 0x50–0x5F | TIMING | Phase 2 activates XOBS-4 (watchdog checkin, message latency) |
| 0x60–0x6F | FAULT | FAULT(0x60), SECURITY_INCIDENT(0x61), AUTH_EVENT(0x62), FAULT_CLEARED(0x63), LIB_ALLOC_FAIL(0x64), LIB_TIMEOUT(0x65), OSAL_FAULT(0x66 — Phase 2), HAL_FAULT(0x67 — Phase 2) |
| 0x70–0x7F | FUNCTION | TEST_START(0x70), TEST_END(0x71) |
| 0x80–0x8F | AI (Phase 3) | RESERVED — Phase 3 AI family. Do not allocate. Taxonomy defined after Phase 1 AI production data. |
| 0x90–0xFF | VENDOR | Community and vendor extension range — 7 full 16-slot sub-bands. Never allocated by the framework. |

† Decision J AI Phase-1 constants — see `docs/architecture/AI_FIRST_ARCHITECTURE.md`

```c
// Derive family at zero cost — no struct field, no lookup table
embediq_obs_family_t fam = embediq_obs_event_family(evt.event_type);
// or at compile time:
EMBEDIQ_OBS_EVT_FAMILY(EMBEDIQ_OBS_EVT_FSM_TRANS)  // → EMBEDIQ_OBS_FAMILY_STATE
```

### Source ID Namespace

`source_fb_id` serves dual purpose. Values `0x00–0xBF` identify FB endpoint instances.
Values `0xC0–0xFF` identify non-FB layer sources. Decoders **must** branch on this boundary.

```
source_fb_id (uint8_t)

0x00–0xBF  FB endpoint indices         (192 slots; EMBEDIQ_MAX_ENDPOINTS default = 64)
0xC0–0xCF  OSAL resource type IDs      (16 slots)
  0xC0 TASKS   0xC1 QUEUES   0xC2 MUTEXES   0xC3 SIGNALS   0xC4 SEMS
  0xC5–0xCF   reserve
0xD0–0xDF  HAL peripheral sources      (16 slots)
  0xD0 UART   0xD1 I2C    0xD2 SPI    0xD3 GPIO
  0xD4 FLASH  0xD5 TIMER  0xD6 WDG
  0xD7–0xDF   reserve
0xE0–0xEF  Library sources             (16 slots — named constraint, _Static_assert enforced)
  Assigned at build time by platform_lib_declare().
0xF0–0xFF  Reserve
```

### Compile-time Trace Levels

`EMBEDIQ_TRACE_LEVEL` in `embediq_config.h` — set once at compile time.

| Level | Default for | Captures |
| ----- | ----------- | -------- |
| 0 | Constrained MCU | SYSTEM + FAULT families only (lifecycle, boot, faults) |
| 1 | Pi/Linux host | Level 0 + MESSAGE + STATE families (messages, FSM transitions) |
| 2 | Debug builds | Level 1 + RESOURCE + TIMING (queue depth, ISR counters) |
| 3 | Lab sessions only | Level 2 + FUNCTION (FB CPU slice, ISR latency) |

Per-family flags (`EMBEDIQ_TRACE_MESSAGE`, `EMBEDIQ_TRACE_STATE`, etc.) allow fine-grain
override of the level defaults. Disabled emit macros compile to `(void)0` — zero binary
overhead on constrained MCU builds.

Commercial tools (Layer 4) require trace Level 1 minimum. Never ship Level 3 to
production constrained MCUs.

### Session Identity

Every `.iqtrace` capture begins with a 40-byte session record:

```c
// embediq_obs.h — frozen at v1 (I-14)
typedef struct {
    uint32_t  device_id;           // unique device identifier
    uint32_t  fw_version;          // EMBEDIQ_OBS_FW_VERSION(maj, min, pat)
    uint64_t  timestamp_base_us;   // wall-clock anchor for sequence numbers
    uint32_t  session_id;          // monotonic session counter
    uint8_t   platform_id;         // POSIX=0, FreeRTOS=1, Baremetal=2, Zephyr=3
    uint8_t   trace_level;         // EMBEDIQ_TRACE_LEVEL at capture time
    uint8_t   _pad[2];
    uint8_t   build_id[16];        // null-terminated build string
} EmbedIQ_Obs_Session_t;

_Static_assert(sizeof(EmbedIQ_Obs_Session_t) == 40, "session struct size mismatch");
```

### File Capture and Open Format

Binary `.iqtrace` files are written via the `hal_obs_stream.h` HAL contract
(implementation: `hal/posix/hal_obs_stream_posix.c`). The format is TLV-framed,
little-endian, and forward-compatible. Full specification:
`docs/observability/iqtrace_format.md` (Apache 2.0, open).

```bash
# Capture a session to file
EMBEDIQ_OBS_PATH=/tmp/session.iqtrace ./build/examples/thermostat/embediq_thermostat

# Decode, analyse, or export with the open CLI
python3 tools/embediq_obs/embediq_obs.py obs decode /tmp/session.iqtrace
python3 tools/embediq_obs/embediq_obs.py obs stats  /tmp/session.iqtrace
python3 tools/embediq_obs/embediq_obs.py obs export /tmp/session.iqtrace
```

---

## Cross-Layer Observability — Phase 2

**Status: Planned — activates in Phase 2.**

Phase 1 established the Observatory foundation: seven event family bands, per-family
compile-time trace flags, and the `.iqtrace` binary capture format. Three bands were
left deliberately empty in Phase 1 — RESOURCE (0x40–0x4F), TIMING (0x50–0x5F), FUNCTION
(0x70–0x7F) — reserved for Phase 2 activation.

Phase 2 adds constants to those reserved bands and obligation hooks to the layer contract
headers. No structural change to the Observatory is required.

**Design decisions (locked):**

- **D-XOBS-1: Additive activation.** Phase 2 adds constants and obligation hooks. The emit macros, trace flags, ring buffer, and transport are already in place.
- **D-XOBS-2: Compile-time obligation enforcement.** Obligation macros in `hal_defs.h` and `embediq_osal.h` make observation a structural requirement. CI scripts verify every failure path calls the relevant macro. A HAL implementation that omits the observation hook fails CI before it can merge.
- **D-XOBS-3: One constant per failure category.** Each layer contributes one fault event constant, not one per failure mode. The sub-type is carried in `state_or_flag`.
- **D-XOBS-4: ISR-safe emission by design.** `embediq_obs_emit_from_isr()` is declared so HAL obligation macros use the correct call site from their first commit.

**Phase 2 XOBS deliverables:**

| ID | Gate | Description |
|----|------|-------------|
| XOBS-1 | Before FreeRTOS OSAL implementation | OSAL event constants (OSAL_FAULT 0x66), `check_osal_obs.py` CI script, obligation macros in `embediq_osal.h` |
| XOBS-2 | Before ESP32 HAL implementation | HAL event constants (HAL_FAULT 0x67), `check_hal_obs.py` CI script, obligation macros in `hal_defs.h` |
| XOBS-3 | After FreeRTOS OSAL is operational | RESOURCE band activation: queue depth events, stack high-water events |
| XOBS-4 | After fb_watchdog is stable | TIMING band activation: watchdog checkin latency, message latency events |

---

## AI-First Architecture

EmbedIQ is not an AI runtime. It is an **audit substrate** that makes AI-enhanced
embedded systems trustworthy, certifiable, and regulatorily legible.

### Phase 1 — Implemented

- **Four AI event type constants** in the SYSTEM band (0x17–0x1A): `AI_INFERENCE_START`, `AI_INFERENCE_END`, `AI_MODEL_LOAD`, `AI_CONFIDENCE_THRESHOLD`.
- **One TLV type** (`AI_CODER_SESSION`, 0x0008): firmware provenance for AI-generated code. Defined in `docs/observability/iqtrace_format.md §5.7`.
- **AI Code Review Gate:** mandatory human review when AI modifies safety-classified FBs. See `AGENTS.md §AI-Code-Review-Gate` and `docs/architecture/AI_FIRST_ARCHITECTURE.md §4`.

**`AI_CODER_SESSION` TLV — 27 bytes fixed:**

| Offset | Size | Field | Description |
| ------ | ---- | ----- | ----------- |
| 0 | 2 | `agent_id` | AI agent type: 0x0001=Copilot, 0x0002=Claude, 0x0003=Cursor |
| 2 | 16 | `model_version` | NUL-padded model version string |
| 18 | 8 | `prompt_hash` | FNV-1a 64-bit hash of the prompt |
| 26 | 1 | `safety_class_reviewed` | 0=not reviewed, 1=reviewed-pass, 2=reviewed-fail |

**AI Code Review Gate triggers** when both are true: (1) commit was made by an AI
assistant, and (2) the modified FB has `safety_class != "NONE"` in its
`EmbedIQ_FB_Meta_t` registration.

**Phase 2 planned:** AI-08 — `AI_CODER_SESSION` TLV CI plugin that auto-emits TLV and invokes the review gate for safety-classified FBs.

### Phase 3 — Planned

- Full AI event family band `0x80–0x8F` reserved in `embediq_obs.h`.
- Gate: ≥2 production deployments using Phase-1 constants before Phase-3 band is finalised.
- See `docs/architecture/AI_FIRST_ARCHITECTURE.md §5` for planned constant taxonomy.

Full specification: `docs/architecture/AI_FIRST_ARCHITECTURE.md`

---

## Platform FB Specifications

### fb_uart — ISR Pattern (Two-Zone Model)

`fb_uart` implements the two-zone ISR model that all Driver FBs follow when hardware
interrupts are involved. The ISR zone is thin and fast. The thread zone has full
framework access. The application zone never knows hardware exists.

**Sub-functions:**
- `byte_collector` — woken by ISR OSAL signal, drains ring buffer, assembles frames, publishes `MSG_UART_FRAME_RX`
- `tx_handler` — subscribed to `MSG_UART_TX_REQUEST`, drives UART transmit

**ISR rule:** The ISR writes one byte to the ring buffer and calls `osal_signal_from_isr()`. Nothing else. < 10 CPU cycles total.

**On overflow:** Head pointer advances (oldest byte lost). Overflow counter incremented. Thread zone detects and emits Observatory event.

**Host simulation:** POSIX termios or file pipe. Same sub-function code path, zero hardware required.

### fb_watchdog — Hardware Watchdog Manager

`fb_watchdog` makes watchdog health a first-class, observable signal. Missed checkin = visible Observatory event before reset. Reset = logged to `fb_nvm`, never silent.

| Aspect | Contract |
| ------ | -------- |
| Registration model | Each FB calls `embediq_wdg_register(fb, timeout_ms)` at init |
| Kick gate | Hardware WDT kicked ONLY when ALL registered FBs have called `embediq_wdg_checkin()` within their timeout window |
| Miss detection | Any FB missing checkin: log Observatory event with FB identity BEFORE allowing reset |
| Reset reason logging | On startup, reads hardware reset cause register. Logs to Observatory. Persists to `fb_nvm` if available. |
| ISR safety | Hardware WDT ISR routes via `osal_signal_from_isr()` to `fb_watchdog` thread |
| Host simulation | Logs checkins and violations but does NOT call `abort()` on host |

```c
// embediq_wdg.h — public API
embediq_status_t embediq_wdg_register(EmbedIQ_FB_t* fb, uint32_t timeout_ms);
embediq_status_t embediq_wdg_checkin(EmbedIQ_FB_t* fb);
embediq_status_t embediq_wdg_unregister(EmbedIQ_FB_t* fb);
EmbedIQ_WDG_ResetReason_t embediq_wdg_last_reset_reason(void);

// Messages published by fb_watchdog:
// MSG_WDG_RESET_REASON — published on startup: reason + last_fb_to_miss
// MSG_WDG_MISSED_CHECKIN — published before allowing reset: fb_handle + last_seen_ts
```

### fb_nvm — Non-Volatile Memory Platform FB

`fb_nvm` is the shared foundation. Without it: state does not survive reset. `fb_watchdog`
cannot persist reset reason. `fb_ota` cannot persist rollback state.

| Aspect | Contract |
| ------ | -------- |
| Storage model | Key-value store. Key = `uint16_t`. Value = typed struct declared in `messages.iq`. Size bounded at compile time. |
| Write safety | Journalled write. Power-loss-safe. No partial writes committed. Atomic swap pattern. |
| HAL contract | `fb_nvm` HAL: `read_page()`, `write_page()`, `erase_sector()`. Platform implements for target Flash/EEPROM. |
| Wear levelling | Basic sector rotation for Flash targets. Advanced wear levelling = declared out of v1. |
| Schema versioning | `schema_id` stored with each key via `messages.iq`. Migration hooks called on version mismatch at boot. |
| Host simulation | Volatile hashmap backend. Tests run without real flash. Same API, zero hardware required. |

```c
// embediq_nvm.h — public API
embediq_status_t embediq_nvm_get(uint16_t key, void* buf, size_t len);
embediq_status_t embediq_nvm_set(uint16_t key, const void* buf, size_t len);
embediq_status_t embediq_nvm_delete(uint16_t key);
embediq_status_t embediq_nvm_reset_defaults(void);
embediq_status_t embediq_nvm_get_schema_version(uint16_t key, uint16_t* schema_id);
```

**Deep sleep pattern:** Before entering deep sleep, application FBs send `MSG_NVM_WRITE`
for each key they need on wake. On wake, `fb_nvm` initialises before application FBs.
Deep sleep entry MUST flush `fb_nvm` before suspending — declared invariant.

---

## Frame Assembly Pattern

Architecture validation against a stateful protocol handler (Section 2.3) surfaced
a gap: `fb_uart` delivers bytes. Who assembles those bytes into frames?

The answer: a `frame_assembler` sub-function inside the FB that owns the protocol.
`fb_uart` does not know about Modbus, custom protocols, or application framing.

```c
// Pattern: Protocol FB owns UART and assembles frames internally.
// fb_modbus owns Modbus framing; byte_collector sub-fn assembles Modbus frames.
static void byte_collector__run(const EmbedIQ_Msg_t* m, void* d) {
    ModbusReader_Data_t* mb = (ModbusReader_Data_t*)d;
    while (ring_buffer_has_data()) {
        uint8_t byte = ring_buffer_read();
        frame_assembler_push(&mb->assembler, byte);
        if (frame_assembler_complete(&mb->assembler)) {
            // Validate CRC, then publish complete, validated frame
            EmbedIQ_Msg_t msg = {.msg_id=MSG_MODBUS_FRAME_RX, .priority=MSG_PRIORITY_NORMAL};
            memcpy(msg.payload, mb->assembler.frame, mb->assembler.frame_len);
            msg.payload_len = mb->assembler.frame_len;
            embediq_publish(mb->fb_handle, &msg);
            frame_assembler_reset(&mb->assembler);
        }
    }
}
```

Rules:
- `fb_uart` publishes raw byte streams (`MSG_UART_FRAME_RX`). It does not know about application protocols.
- The protocol FB owns framing. The published message is always a validated, complete frame.
- CRC validation happens in `byte_collector` — never in the application FB.
- The frame assembler is pure C logic, no hardware dependency — tests run on host.

---

## Architecture Validation — Three Use Cases

The architecture was validated against three real-world embedded product types before
code was written. These records document where the model fits cleanly and where it strains.

### Connected Sensor Device

**Verdict: Strongest use case.** The FB model maps exactly to product thinking. Each concern (sensing, cloud, OTA, watchdog) is one FB. FSM is the right tool for the cloud connection state machine. Observatory captures the moment a sensor fault triggered a cloud publish without a single `printf`.

**Strain:** A 1kHz sensor flooding the bus with `MSG_SENSOR_READING` is architectural friction. Resolution: aggregate inside the Platform FB before publishing. The bus carries semantic messages, not hardware interrupts. Rule: high-frequency hardware data is aggregated inside the Platform FB.

### Industrial Edge Gateway

**Verdict: Strong fit.** Protocol isolation is the key feature. A Modbus framing error faults `fb_modbus_reader` — `fb_can_reader` and `fb_ble_scanner` keep running. The External FB pattern for Python ML inference runs as a separate process, communicates via Bridge, appears as a named endpoint. No native FB knows Python exists.

**Strain:** CAN at 1Mbit/s with dense traffic floods the bus. Resolution: `fb_can_reader` aggregates internally, publishing one `MSG_CAN_DATA` summary per 100ms.

**Gap found during validation:** `fb_cloud_mqtt` requires `fb_nvm` to be ready before reading its configuration. This gap led to the `depends_on` array in `EmbedIQ_FB_Descriptor_t`.

### Stateful Protocol Handler

**Verdict: Strong fit for application-layer protocol state management.** The table-driven FSM makes every state, event, and transition explicit. Observatory is strongest here — field device protocol failures show the exact sequence of states and messages leading to failure. No reproduction needed.

**Strain:** Timing-sensitive protocols (LIN bus response window: 1.4ms; ISO 15765-4 CAN timing) require byte-level timing guarantees that belong below the OSAL, not in the message bus layer. EmbedIQ adds latency that violates hard byte-timing requirements.

**Resolution:** For LIN and similar protocols, byte-level timing logic belongs in the Platform FB BSP layer, below the OSAL. See Timing Boundary section below.

---

## Timing and Resource Boundaries

These constraints are binding. Violations are CI failures or architecture violations.

| Constraint | Value / Rule | Enforcement |
| ---------- | ------------ | ----------- |
| Max ISR execution time | < 10 CPU cycles (ring buffer write + `osal_signal_from_isr()` only) | Code review; ISR profiling in CI |
| Observatory event struct size | Exactly 14 bytes (`EmbedIQ_Event_t`) | `_Static_assert` in header; CI multi-target compile |
| Message header size | Exactly 20 bytes (`offsetof(EmbedIQ_Msg_t, payload) == 20`) | `_Static_assert` in header |
| Message payload max size | `EMBEDIQ_MSG_MAX_PAYLOAD` — default 64 bytes, configurable | `embediq_config.h` + `validator.py` |
| Observatory CPU overhead | < 5% CPU, < 20 bytes/message on Cortex-M4 class target | Phase 1 benchmark before launch |
| No dynamic allocation in Layer 1 | `malloc` / `free` / `new` / `delete` never called in Layer 1 objects | CI: nm analysis of release binary |
| Message passing model v1 | Copy-by-value. Full `EmbedIQ_Msg_t` copied at every queue send. | Architecture decision; no pool API in v1 |
| Max FB count — FreeRTOS small MCU (128KB RAM) | 8–16 FBs recommended. Each FB = 1 RTOS task + 3 queues + `fb_data`. | `embediq_config.h` + `validator.py` |
| Max FB count — FreeRTOS large MCU (512KB+ RAM) | 16–32 FBs recommended. | `embediq_config.h` |
| Max FB count — Pi/Linux POSIX | Unlimited. Practical limit is design complexity. | — |
| Timestamp width on MCU | `uint32_t` microseconds. Wraps at ~71 minutes. Expected and handled. | Tools use `sequence` for gap detection, not timestamp delta. |
| Subscription arrays | Must be `static const` — never compound literals or stack arrays | R-sub-01 in AI checklist |
| Sub-fn routing v1 | Fan-out: all matching sub-fns invoked in `init_order`. No stop/consume. | Architecture decision |
| Bridge reliability v1 | At-most-once. No ack, no retry. Non-blocking enqueue. | Architecture decision |

**Timing Guarantee Table:**

| What | Guarantee Level | How to Achieve |
| ---- | --------------- | -------------- |
| Message delivery order within one FB | GUARANTEED — HIGH before NORMAL before LOW. Deterministic drain order. | Message priority config |
| Message delivery between FBs | BEST EFFORT — depends on RTOS scheduling and FB thread priorities. | Set FB thread priority in FB config |
| ISR latency < 10 CPU cycles | FRAMEWORK INVARIANT — ISR code is ring buffer write + signal only. Structural. | Code review + ISR profiling in CI |
| Response within N microseconds | NOT GUARANTEED — hard deadline guarantees require a certified RTOS below EmbedIQ. | RTOS configuration + scheduling analysis |
| Byte-level timing (LIN bus, ISO 15765) | NOT IN SCOPE — LIN/CAN byte timing must be handled in Platform FB BSP layer, below OSAL. | Platform FB BSP implementation |
| Time-triggered execution at exact intervals | BEST EFFORT — `fb_timer` uses RTOS callbacks. Jitter depends on RTOS tick rate and load. | RTOS timer config + task priority |

EmbedIQ adds structure above the RTOS. It cannot replace the RTOS's scheduling guarantees.
For hard real-time products (IEC 61508, automotive safety), the certified RTOS below
EmbedIQ provides the hard RT guarantees. EmbedIQ provides the application structure above
them. EmbedIQ does not claim or prevent safety certification.

For protocols requiring byte-level timing guarantees (LIN, ISO 15765-4 CAN frame timing),
the timing logic must live in the Platform FB BSP layer, below the OSAL. EmbedIQ sits
at the message layer — the right abstraction level for application-layer protocol
state management, not for byte-timing guarantees.

---

## Responsibility Register

### Declared Out — Not EmbedIQ v1 Scope

| Problem | Owner / Reason |
| ------- | -------------- |
| Priority inversion (RTOS scheduler) | RTOS layer. EmbedIQ message-passing reduces lock contention but gives no PI guarantee. |
| Atomic operations / generic atomics | OSAL covers ISR-signal primitives. `stdatomic.h` generic usage = developer responsibility. |
| Memory alignment on ARM Cortex-M | BSP/Platform FB layer. EmbedIQ message structs use correct alignment via `messages.iq`. |
| Targets with < 64KB flash / 16KB RAM | Non-goal v1. Minimal bare-metal build = future work. |
| Pin multiplexing / GPIO mux conflicts | BSP/Platform FB layer. EmbedIQ does not model pin mux. |
| DMA buffer alignment and management | Platform FB internals. Generic DMA abstraction not in v1. |
| BLE/WiFi RF coexistence | Chip SDK/radio driver. EmbedIQ sits above radio drivers. |
| Secrets / key storage | Platform/secure element. EmbedIQ does not access keys directly. |
| Hard real-time byte timing | Platform FB BSP layer. See Timing Boundary section. |
| Side-channel / fault injection attacks | Security certification concern. Out of scope v1. |
| Battery life modelling | FB suspend/resume hooks exist. Power policy = application. |

### Partial Coverage — EmbedIQ Helps, Developer Owns the Rest

| Problem | EmbedIQ PROVIDES / DEVELOPER OWNS |
| ------- | ---------------------------------- |
| ISR/thread data races | PROVIDES: two-zone model, `osal_signal_from_isr()`, ring-buffer pattern. DEVELOPER OWNS: cannot enforce at compile time in v1. |
| Mutex deadlock | PROVIDES: message-passing eliminates most mutex need. DEVELOPER OWNS: any OSAL mutexes inside Platform FB internals. |
| Stack overflow detection | PROVIDES: explicit stack size in `osal_task_create()`. DEVELOPER OWNS: sizing. Stack high-water mark via Observatory = planned Phase 2. |
| Non-reproducible field bugs | PROVIDES: Observatory event log captures pre-bug state sequence + timing metrics. DEVELOPER OWNS: root cause analysis. |
| OTA rollback mechanism | PROVIDES: `fb_ota` framework. DEVELOPER OWNS: rollback policy and hardware flash partition scheme. |
| Schema migration across firmware versions | PROVIDES: `schema_id` in message envelope, versioning in `messages.iq`. DEVELOPER OWNS: backward-compat migration policy. |
| OTA firmware signature verification | PROVIDES: `fb_ota` framework with signature verification hook. DEVELOPER OWNS: signature scheme (RSA, ECDSA, Ed25519). |

---

## Non-Goals for v1

Do not generate code for any of these. Named future work, not omissions.

```
EXECUTOR:    Worker pool. v1 = dedicated thread per FB.
MESSAGING:   Zero-copy message pool. v1 = copy-by-value.
MESSAGING:   Variable-length payload. v1 = fixed EMBEDIQ_MSG_MAX_PAYLOAD (64 bytes default).
BRIDGE:      Authenticated connections. v1 = no auth.
BRIDGE:      Reliable delivery. v1 = at-most-once.
OSAL:        Zephyr OSAL. v1 = FreeRTOS + Pi/Linux POSIX.
HAL:         hal/stm32/, hal/nrf52/ implementations. v1 = hal/posix/ + hal/esp32/ only.
DRIVER FBs:  fb_i2c, fb_spi, fb_usb. v1 = fb_timer, fb_nvm, fb_watchdog, fb_uart.
REGISTRY:    Runtime FB discovery. v1 = static registry table only.
ROUTING:     Sub-fn consume/stop propagation semantics. v1 = fan-out to all matching sub-fns.
ROUTING:     Dynamic subscription changes at runtime. v1 = subscriptions set at init only.
COMMERCIAL:  Studio GUI, Cloud, AI Coder. v1 = framework + Observatory CLI + Test Runner.
TIMESTAMP:   64-bit timestamps on MCU. v1 = uint32_t microseconds, sequence for gap detection.
```

---

## Core Contract Stability

v1 Core headers are frozen at release. Additive only after v1:

- New fields appended to structs (with `_reserved` padding)
- New enum values
- New optional functions

Never: remove, rename, or reorder existing fields.

CI enforcement: `tests/compat/fb_v1_compat.c` compiles on every PR against the frozen v1
API surface. Breaking change = CI failure.

**Frozen headers:** `embediq_fb.h`, `embediq_bus.h`, `embediq_msg.h`, `embediq_osal.h`,
`embediq_subfn.h`, `embediq_sm.h`, `embediq_obs.h`.

---

## Apache 2.0 Commitment

The following are Apache 2.0 forever — permanent, irrevocable:

- All of `core/` — FB engine, message bus, FSM engine, Observatory runtime, OSAL
- All OSAL implementations — FreeRTOS, POSIX, Zephyr, bare-metal
- Base Driver FBs — `fb_uart`, `fb_i2c`, `fb_spi`, `fb_timer`, `fb_gpio`
- Infrastructure FBs — `fb_watchdog` (basic), `fb_nvm` (basic)
- All tools — `messages.iq` generator, `embediq` CLI, Observatory CLI
- The `.iqtrace` binary format specification — open forever
- All examples and tests in this repository

**EmbedIQ Pro** — production accelerator FBs (`fb_ota`, `fb_cloud_mqtt`,
`fb_nvm` Pro, and others) are commercial and proprietary.
They plug into the Apache 2.0 framework via the standard FB interface.
See [COMMERCIAL_BOUNDARY.md](COMMERCIAL_BOUNDARY.md) and
[embediq.com/pro](https://embediq.com/pro).

---

## India Market — Strategic Positioning

India is a priority geography for EmbedIQ. The positioning is grounded in three structural
facts about the Indian embedded/edge market.

**The Missing Middle.** India's embedded systems market is bifurcated: hyperscale IoT
platforms at one end; deeply resource-constrained bare-metal BSPs at the other. EmbedIQ
targets the middle tier — systems with enough compute to run inference and connectivity,
but not enough to run full cloud agent stacks. The fixed 14-byte Observatory record, the
zero-overhead OSAL abstraction, and the host/POSIX simulation capability are all properties
that matter specifically to this segment.

**IndiaAI Mission and DPI.** The IndiaAI Mission (MEITY, 2024) establishes Digital Public
Infrastructure (DPI) as a policy objective for AI. EmbedIQ's `.iqtrace` format is open and
vendor-neutral — it produces AI training data at the edge without proprietary lock-in.

**PLI Scheme Alignment.** India's Production-Linked Incentive (PLI) scheme for electronics
incentivises domestic hardware manufacturing. Domestic hardware running EmbedIQ Observatory
accumulates domestic `.iqtrace` datasets, fulfilling data-locality expectations without
requiring cloud infrastructure.

### RISC-V Compatibility

EmbedIQ runs on RISC-V without modification. The OSAL abstraction presents only opaque
handle types to the framework — no architecture-specific types, no ARM intrinsics, no
register-level code in Core or Layer 1. A RISC-V OSAL implementation for SHAKTI
(IIT Madras), VEGA (C-DAC), or any RISC-V RTOS implements the `embediq_osal.h`
contract exactly. Application FBs, Service FBs, and Driver FBs need zero changes.

| Target | Compatibility | Status |
| ------ | ------------- | ------ |
| SHAKTI (IIT Madras) | Open source RISC-V processor. EmbedIQ OSAL target: FreeRTOS on SHAKTI or bare-metal POSIX-compatible runtime. | Phase 2 OSAL target |
| VEGA (C-DAC) | India's indigenous RISC-V ecosystem. | Phase 2 OSAL target |
| Any RISC-V RTOS | FreeRTOS, Zephyr, RT-Thread all have RISC-V ports. EmbedIQ runs above any of them via OSAL. | Same OSAL pattern as any target |

Apache 2.0 license means Indian government institutions (C-DAC, DRDO, ISRO) and academic
institutions can use EmbedIQ in closed-source products without source disclosure.

**India-Specific Standards:**
- **CDSCO / MDR 2017:** Aligned with IEC 62304 Class A/B. EmbedIQ's Class A/B support covers CDSCO-regulated medical devices.
- **AIS-190:** India's automotive cybersecurity standard, aligned with ISO 26262 ASIL A/B and UNECE R155. EmbedIQ's `SECURITY_INCIDENT` and `AUTH_EVENT` event types provide the required audit trail.
- **TEC IoT Security Guidelines:** Tamper-evident audit trail plus `SECURITY_INCIDENT` event type satisfy TEC logging requirements.
- **NITI Aayog Responsible AI for All:** The AI Code Review Gate and `safety_class_reviewed` field directly implement the human-oversight requirements.

---

## AI Coding Checklist

Verify every item before emitting any EmbedIQ code.

| Rule | Correct | Incorrect |
| ---- | ------- | --------- |
| R-sub-01 | `EMBEDIQ_SUBS(my_subs, MSG_X, MSG_Y)` | `.subscriptions = (uint16_t[]){MSG_X, MSG_Y}` — compound literal, lifetime bug |
| R-sub-02 | `embediq_publish(fb, &msg)` for cross-FB | `other_fb_do_thing()` — direct call, violates R-01 |
| R-sub-03 | `embediq_fb_register(&config)` | `xTaskCreate(...)` in application FB — no RTOS calls in app layer |
| R-sub-04 | `uint32_t delta = b.sequence - a.sequence` | `if (b.timestamp > a.timestamp)` — wrong after 71-min wrap |
| R-sub-05 | `EmbedIQ_Msg_t msg = {.msg_id=X}` (stack) | `malloc(sizeof(EmbedIQ_Msg_t))` — no malloc in Layer 1 |
| R-sub-06 | Add fields at END only, increment `schema_id` | Remove/rename/reorder existing fields — breaks Observatory |
| R-sub-07 | `report_fault()` when FB cannot continue | `report_fault()` for a timeout that should be retried |
| R-sub-08 | Call all `subfn_register()` first, then return | Call `init_fn()` manually before registration completes |
| R-sub-09 | `"fb.subfn"` dotted syntax in test files only | `embediq_bus_send(fb, "fb.subfn", &msg)` in production code |
| R-sub-10 | Opaque types in Core: `typedef struct EmbedIQ_Task_s EmbedIQ_Task_t` | `#include "freertos/FreeRTOS.h"` in Core header |
| R-sub-11 | `osal_signal` only in Driver FB sub-functions | `osal_signal` in application FB sub-function |
| R-sub-12 | See Non-Goals list | Worker pool, message pool, bridge auth, runtime subscriptions |
| R-sub-13 | Message ID in correct namespace range | Community FB ID outside 5120–65535 without registry reservation |
| R-sub-14 | `boot_phase` declared in every FB config | Missing `boot_phase` — will default to APPLICATION silently |
| R-sub-15 | `sequence` for ordering, not `timestamp_us` | `if (b.timestamp > a.timestamp)` — wrong after 71-min wrap |
| R-sub-16 | Driver FB calls `hal/*.h` only for hardware access | Driver FB calls `esp_uart_write()` or `HAL_UART_Transmit()` directly — bypasses HAL contract |

---

*See `docs/architecture/AI_FIRST_ARCHITECTURE.md` for the full AI observability specification.*
*See `docs/observability/iqtrace_format.md` for the binary trace format specification.*

*Apache 2.0 · embediq.com · © 2026 Ritzy Lab*