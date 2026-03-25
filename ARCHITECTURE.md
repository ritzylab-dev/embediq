# ARCHITECTURE.md — EmbedIQ Technical Reference

**Read this before writing any code. Reference it while coding.**

This document covers the complete EmbedIQ architecture. For AI agents and contributors: also read [AGENTS.md](AGENTS.md) and [CODING_RULES.md](CODING_RULES.md).

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

## Layer Model

```
┌─────────────────────────────────────────────────────────────────────┐
│  LAYER 4 — COMMERCIAL                                               │
│  EmbedIQ Studio  ·  EmbedIQ Cloud  ·  MCP Server                   │
├─────────────────────────────────────────────────────────────────────┤
│  LAYER 3 — ECOSYSTEM                                                │
│  Bridge daemon  ·  bridge/websocket  ·  bridge/unix_socket          │
│  Registry  ·  Community BSPs  ·  3rd-party FB wrappers              │
│  External FBs (Python · Node.js · Java · any language)             │
├─────────────────────────────────────────────────────────────────────┤
│  LAYER 2 — DRIVER FBs + SERVICE FBs          (all Apache 2.0)      │
│  Driver FBs:   fb_uart · fb_gpio · fb_spi · fb_i2c                 │
│                fb_timer · fb_nvm · fb_watchdog                      │
│  Service FBs:  fb_cloud_mqtt · fb_ota · fb_telemetry · fb_logger   │
├─────────────────────────────────────────────────────────────────────┤
│  LAYER 1 — FRAMEWORK ENGINE                  (all Apache 2.0)      │
│  FB Registry  ·  Endpoint Router  ·  Message Bus (3-queue)         │
│  Sub-fn Dispatcher  ·  FSM Engine  ·  Observatory                  │
│  Test Runner [TEST BUILDS ONLY]                                     │
├─────────────────────────────────────────────────────────────────────┤
│  CONTRACTS  (core/include/ — header-only · frozen post-v1)         │
│  embediq_fb.h  ·  embediq_subfn.h  ·  embediq_bus.h                │
│  embediq_msg.h  ·  embediq_sm.h  ·  embediq_obs.h                  │
│  embediq_osal.h  ·  embediq_time.h  ·  embediq_bridge.h            │
│  embediq_meta.h  ·  embediq_endpoint.h  ·  embediq_msg_catalog.h   │
│  embediq_nvm.h  ·  embediq_timer.h  ·  embediq_wdg.h               │
│  embediq_ota.h  ·  embediq_mqtt.h                                  │
│  hal/ (HAL contract headers — see HAL section below)               │
├─────────────────────────────────────────────────────────────────────┤
│  HAL — Hardware Abstraction Layer                                   │
│  Contracts: core/include/hal/  (hal_uart.h · hal_gpio.h · etc.)    │
│  Implementations: hal/posix/ · hal/esp32/ · hal/stm32/             │
├─────────────────────────────────────────────────────────────────────┤
│  OSAL — OS Abstraction Layer                                        │
│  Contract: core/include/embediq_osal.h                             │
│  Implementations: osal/posix/ · osal/freertos/ · osal/zephyr/      │
├─────────────────────────────────────────────────────────────────────┤
│  SUBSTRATE                                                          │
│  FreeRTOS  ·  Pi/Linux POSIX  ·  Zephyr  ·  bare-metal MCU         │
└─────────────────────────────────────────────────────────────────────┘
```

**Layer dependency rule:** Each layer may only depend on the layer directly below it.
Layer 2 may call Layer 1 APIs. Layer 2 must NOT call Core internals or skip Layer 1.
Agents: never add an include that skips a layer.

---

## Layer Definitions

### Driver FB vs Service FB (both are Layer 2)

**Driver FB** — wraps a hardware peripheral into the EmbedIQ message model.
- Calls HAL contract (`core/include/hal/`) for hardware access
- Calls OSAL for threading and ISR wakeup
- One portable source file (`fbs/drivers/fb_uart.c`) — target differences are handled
  entirely inside the HAL implementation
- Examples: `fb_uart`, `fb_gpio`, `fb_timer`, `fb_nvm`, `fb_watchdog`

**Service FB** — platform-agnostic reusable service.
- Does NOT call HAL directly
- Calls an ops table (`embediq_ota_ops_t`, `embediq_mqtt_ops_t`) that is registered
  by the platform at boot — this is its hardware abstraction
- One source file runs on all targets unchanged
- Examples: `fb_cloud_mqtt`, `fb_ota`, `fb_telemetry`, `fb_logger`

**Rule:** If an FB directly accesses hardware or OS-provided device drivers, it is a
Driver FB. If it accesses hardware only through an ops table, it is a Service FB.

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

### Native FB vs External FB

**Native FB** — written in C, running inside the EmbedIQ runtime. All FBs in this
document are Native FBs unless stated otherwise.

**External FB** — running outside the EmbedIQ runtime as a separate process (Python,
Node.js, Java, any language). Communicates via the Bridge interface (Layer 3). Appears
as a named endpoint on the bus. External FBs boot in Phase 4 — BRIDGE.

**Rule:** `FB` alone means Native FB. `External FB` is always written in full.

---

## Contract Ownership Table

ALL contracts live in `core/include/`. A contract lives with the interface it defines,
not with the layer that implements it.

| Contract file          | Defines interface for       | Consumed by           |
| ---------------------- | --------------------------- | --------------------- |
| `embediq_fb.h`         | FB registration + lifecycle | All layers            |
| `embediq_bus.h`        | Message publish/receive     | All layers            |
| `embediq_endpoint.h`   | Endpoint registration       | Layer 1, Layer 3      |
| `embediq_osal.h`       | OS primitives               | Layer 1, Layer 2      |
| `embediq_obs.h`        | Observatory events          | All layers            |
| `embediq_sm.h`         | FSM table contract          | Layer 2 (Driver FBs)  |
| `embediq_time.h`       | Monotonic + wall clock      | All layers            |
| `embediq_nvm.h`        | NVM key-value store         | Layer 2+, Application |
| `embediq_timer.h`      | Timer tick messages         | Layer 2+, Application |
| `embediq_wdg.h`        | Watchdog checkin            | Layer 2+, Application |
| `embediq_ota.h`        | OTA ops table + FSM         | Layer 2 Service FB    |
| `embediq_mqtt.h`       | MQTT ops table + FSM        | Layer 2 Service FB    |
| `embediq_meta.h`       | FB metadata                 | Layer 3 Registry      |
| `embediq_bridge.h`     | External FB bridge          | Layer 3               |
| `embediq_endpoint.h`   | Endpoint Router contract    | Layer 1, Layer 3      |
| `core/include/hal/*.h`              | HAL peripheral access        | Layer 2 Driver FBs    |
| `core/include/hal/hal_obs_stream.h` | Observatory binary stream    | Layer 1 Observatory   |

---

## Layer Boundary Rules

Enforced by `tools/boundary_checker.py` in CI. Violations = CI failure.

| Source location  | May include                                      | Must NOT include                           |
| ---------------- | ------------------------------------------------ | ------------------------------------------ |
| `core/src/`      | `core/include/`, system headers                  | `hal/`, `osal/`, `platform/`, `fbs/`       |
| `osal/posix/`    | `core/include/`, system + OS headers             | `hal/`, `fbs/`, `examples/`                |
| `osal/freertos/` | `core/include/`, FreeRTOS headers                | `hal/`, `fbs/`, `examples/`                |
| `hal/posix/`     | `core/include/hal/`, system + OS headers         | `osal/`, `core/src/`, `fbs/`               |
| `hal/esp32/`     | `core/include/hal/`, ESP-IDF headers             | `osal/`, `core/src/`, `fbs/`               |
| `fbs/drivers/`   | `core/include/` (incl. `hal/`), C stdlib         | `hal/` impl paths, vendor BSP, `examples/` |
| `fbs/services/`  | `core/include/` (excl. `hal/`), C stdlib         | `core/include/hal/`, `hal/`, `osal/` impl  |
| `examples/`      | `core/include/`, `fbs/drivers/`, `fbs/services/` | `hal/` impl headers, `osal/` impl          |
| `tests/`         | `core/include/`, `examples/`, `fbs/` contracts   | `hal/` impl, `osal/` impl                  |

---

## The Functional Block

The FB is the unit of everything. Every concern in your firmware is one FB.

| Property                     | What it means                                                                               |
| ---------------------------- | ------------------------------------------------------------------------------------------- |
| Owns state exclusively       | Private `fb_data` struct. No globals. No shared memory across FB boundaries.                |
| Communicates by message only | Never calls another FB directly. Intra-FB helpers are normal C.                             |
| Single execution context     | Executes sequentially. Actor model. No concurrent execution within one FB. No locks needed. |
| Executor policy v1           | Dedicated OS thread per FB.                                                                 |
| Explicit lifecycle           | `IDLE → INITIALISING → RUNNING → FAULT → STOPPING → STOPPED`. Every transition observable.  |
| Observable by default        | All lifecycle, bus messages, FSM transitions auto-captured. Zero developer code.            |
| Testable by design           | Message-only cross-FB interface: inject messages, assert on output. No hardware required.   |
| Composed of sub-functions    | Internal structure from sub-functions, each with subscriptions and optional FSM.            |

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

### Three-Queue Priority Model

Each FB has three independent queues: `HIGH`, `NORMAL`, `LOW`. Drain order each cycle: HIGH fully → NORMAL fully → one from LOW (fairness).

Message priority and FB thread priority are **independent axes**. A high-priority message to a low-priority FB enters the HIGH queue and is processed at that FB's OS scheduling priority.

### messages.iq

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
every protocol change visible in PR diffs. Auto-generation at cmake time would allow
protocol contracts to change silently without review.

**Versioning rules:**
- IDs are never reused. A retired message ID is tombstoned.
- Changes are additive only. Never remove, rename, or reorder existing fields.
- Any change increments `schema_id`.
- AI agents: always add new fields at the END. Always increment `schema_id`.

---

## Core Headers

### `embediq_fb.h`

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
} EmbedIQ_FB_Config_t;

EmbedIQ_FB_Handle_t* embediq_fb_register(const EmbedIQ_FB_Config_t* config);
void embediq_publish(EmbedIQ_FB_Handle_t* fb, EmbedIQ_Msg_t* msg);
void embediq_fb_report_fault(EmbedIQ_FB_Handle_t* fb, uint32_t reason);
uint8_t embediq_bus_resolve_name(const char* name);
```


### Message ID Namespace

Message IDs are a global ABI. Two FBs with conflicting IDs silently corrupt each other's data.

| Range                        | Owner                                                              |
| ---------------------------- | ------------------------------------------------------------------ |
| 0x0000 – 0x03FF (0–1023)     | EmbedIQ Core system — framework internal only                      |
| 0x0400 – 0x13FF (1024–5119)  | Official EmbedIQ components — assigned by embediq/embediq          |
| 0x1400 – 0xFFFF (5120–65535) | Community / third-party — must reserve in `messages_registry.json` |

Community FBs **must** reserve a range in `messages_registry.json` before publishing. No reservation = undefined collision risk.


### Queue Overflow Policy

Undefined overflow behavior is a production bug class. This policy is binding:

| Queue  | Behavior when full                                                                    | Observatory event? |
| ------ | ------------------------------------------------------------------------------------- | ------------------ |
| HIGH   | **Block sender** — HIGH messages are safety-critical, dropping is worse than blocking | No                 |
| NORMAL | **Drop oldest** — recent data is more valuable than stale data                        | Yes                |
| LOW    | **Drop incoming** — best-effort delivery                                              | Yes                |

---

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
bool             embediq_osal_queue_send(EmbedIQ_Queue_t* q, const void* item, uint32_t timeout_ms);
bool             embediq_osal_queue_recv(EmbedIQ_Queue_t* q, void* item, uint32_t timeout_ms);
EmbedIQ_Signal_t* embediq_osal_signal_create(void);
void              embediq_osal_signal_wait(EmbedIQ_Signal_t* sig);
void              embediq_osal_signal_from_isr(EmbedIQ_Signal_t* sig); // ISR-safe
```

---

## Sub-function Model

Sub-functions are the internal structure of an FB. Each has subscriptions, an optional FSM, and init/run/exit lifecycle.

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

**Rule:** ISRs may only write to a ring buffer and call `osal_signal_from_isr()`. Nothing else. < 10 CPU cycles total.

---

## Observatory

Zero-instrumentation observability. Every FB state transition and message is captured automatically via the bus tap. No `printf`. No Percepio. No instrumentation code.

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

### Event Families

Every `event_type` encodes its family in its high nibble — no extra field needed.
The family is derived in the decoder with zero runtime overhead:

| Band       | Family    | Event types                                   |
| ---------- | --------- | --------------------------------------------- |
| 0x10–0x1F  | SYSTEM    | OVERFLOW (0x10)                               |
| 0x20–0x2F  | MESSAGE   | MSG_TX (0x20), MSG_RX (0x21), QUEUE_DROP (0x22) |
| 0x30–0x3F  | STATE     | LIFECYCLE (0x30), FSM_TRANS (0x31)            |
| 0x40–0x4F  | RESOURCE  | (reserved for queue depth, stack high-water)  |
| 0x50–0x5F  | TIMING    | (reserved for ISR latency, message latency)   |
| 0x60–0x6F  | FAULT     | FAULT (0x60)                                  |
| 0x70–0x7F  | FUNCTION  | (reserved for FB CPU slice)                   |

```c
// Derive family at zero cost — no struct field, no lookup table
embediq_obs_family_t fam = embediq_obs_event_family(evt.event_type);
// or at compile time:
EMBEDIQ_OBS_EVT_FAMILY(EMBEDIQ_OBS_EVT_FSM_TRANS)  // → EMBEDIQ_OBS_FAMILY_STATE
```

### Compile-time Trace Levels

`EMBEDIQ_TRACE_LEVEL` in `embediq_config.h` — set once at compile time.

| Level | Default for       | Captures                                                       |
| ----- | ----------------- | -------------------------------------------------------------- |
| 0     | Constrained MCU   | SYSTEM + FAULT families only (lifecycle, boot, faults)         |
| 1     | Pi/Linux host     | Level 0 + MESSAGE + STATE families (messages, FSM transitions) |
| 2     | Debug builds      | Level 1 + RESOURCE + TIMING (queue depth, ISR counters)        |
| 3     | Lab sessions only | Level 2 + FUNCTION (FB CPU slice, ISR latency)                 |

Per-family flags (`EMBEDIQ_TRACE_MESSAGE`, `EMBEDIQ_TRACE_STATE`, etc.) allow
fine-grain override of the level defaults. Disabled emit macros compile to
`(void)0` — zero binary overhead on constrained MCU builds.

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

## Cross-FB Dependency Declaration

When FB_A requires FB_B to be initialised first, declare it:

```c
static const char *cloud_deps[] = { "fb_nvm", "fb_uart", NULL };
EMBEDIQ_FB_DESCRIPTOR(fb_cloud_mqtt, .depends_on = cloud_deps, .depends_count = 2, ...);
```

The framework reads `depends_on` at boot, topologically sorts init order, and faults on cycles (`FAULT_DEPENDENCY_CYCLE`) or missing dependencies (`FAULT_DEPENDENCY_MISSING`).

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
COMMERCIAL:  Studio GUI, Cloud, AI Coder. v1 = framework + Observatory CLI
             (`tools/embediq_obs/`) + Test Runner. Observatory CLI is
             SHIPPED in v1. The project-management CLI (`embediq init/add/build`)
             is Phase 2.
```

---


## Boot Phase Model

Four phases execute sequentially. All FBs in phase N complete before phase N+1 begins.

```
PHASE 1 — PLATFORM      fb_uart · fb_timer · fb_gpio · fb_i2c
PHASE 2 — INFRASTRUCTURE  fb_nvm · fb_watchdog · fb_cloud_mqtt · fb_ota
PHASE 3 — APPLICATION    your FBs (default if boot_phase not declared)
PHASE 4 — BRIDGE         External FBs · Studio connections (best-effort)
```

**Why this exists:** `depends_on` guarantees `init_fn` was called — not that the FB is operationally ready. Boot phases guarantee phase N is fully operational before phase N+1 begins.

Declaration in FB config:
```c
.boot_phase = EMBEDIQ_BOOT_PHASE_INFRASTRUCTURE,  // omit for APPLICATION (default)
```

---


## Observatory Levels

`EMBEDIQ_TRACE_LEVEL` — compile-time constant in `embediq_config.h`.
See the Trace Levels table in the Observatory section above for the
full level/family matrix.

Studio and Cloud require Level 1 minimum. Never ship Level 3 to production constrained MCUs.

---


## Core Contract Stability

v1 Core headers are frozen at release. Additive only after v1:

- New fields appended to structs (with `_reserved` padding)
- New enum values
- New optional functions

Never: remove, rename, or reorder existing fields.

CI enforcement: `tests/compat/fb_v1_compat.c` compiles on every PR against the frozen v1 API surface. Breaking change = CI failure.

---

## AI Coding Checklist

Verify every item before emitting any EmbedIQ code.

| Rule     | Correct                                                              | Incorrect                                                                                    |
| -------- | -------------------------------------------------------------------- | -------------------------------------------------------------------------------------------- |
| R-sub-01 | `EMBEDIQ_SUBS(my_subs, MSG_X, MSG_Y)`                                | `.subscriptions = (uint16_t[]){MSG_X, MSG_Y}` — compound literal, lifetime bug               |
| R-sub-02 | `embediq_publish(fb, &msg)` for cross-FB                             | `other_fb_do_thing()` — direct call, violates R-01                                           |
| R-sub-03 | `embediq_fb_register(&config)`                                       | `xTaskCreate(...)` in application FB — no RTOS calls in app layer                            |
| R-sub-04 | `uint32_t delta = b.sequence - a.sequence`                           | `if (b.timestamp > a.timestamp)` — wrong after 71-min wrap                                   |
| R-sub-05 | `EmbedIQ_Msg_t msg = {.msg_id=X}` (stack)                            | `malloc(sizeof(EmbedIQ_Msg_t))` — no malloc in Layer 1                                       |
| R-sub-06 | Add fields at END only, increment schema_id                          | Remove/rename/reorder existing fields — breaks Observatory                                   |
| R-sub-07 | `report_fault()` when FB cannot continue                             | `report_fault()` for a timeout that should be retried                                        |
| R-sub-08 | Call all `subfn_register()` first, then return                       | Call `init_fn()` manually before registration completes                                      |
| R-sub-09 | `"fb.subfn"` dotted syntax in test files only                        | `embediq_bus_send(fb, "fb.subfn", &msg)` in production code                                  |
| R-sub-10 | Opaque types in Core: `typedef struct EmbedIQ_Task_s EmbedIQ_Task_t` | `#include "freertos/FreeRTOS.h"` in Core header                                              |
| R-sub-11 | `osal_signal` only in Driver FB sub-functions                        | `osal_signal` in application FB sub-function                                                 |
| R-sub-12 | See Non-Goals list                                                   | Worker pool, message pool, bridge auth, runtime subscriptions                                |
| R-sub-13 | Message ID in correct namespace range                                | Community FB ID outside 5120–65535 without registry reservation                              |
| R-sub-14 | `boot_phase` declared in every FB config                             | Missing boot_phase — will default to APPLICATION silently                                    |
| R-sub-15 | `sequence` for ordering, not `timestamp_us`                          | `if (b.timestamp > a.timestamp)` — wrong after 71-min wrap                                   |
| R-sub-16 | Driver FB calls `hal/*.h` only for hardware access                   | Driver FB calls `esp_uart_write()` or `HAL_UART_Transmit()` directly — bypasses HAL contract |

---

*See the full architecture specification in `docs/EmbedIQ_Architecture_v7_Core.docx` and `docs/EmbedIQ_Architecture_v7_Reference.docx`.*

*Apache 2.0 · embediq.com · © 2026 Ritzy Lab*
