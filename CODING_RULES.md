# CODING_RULES.md — EmbedIQ Coding Rules

**Single source of truth for all rules. Read after AGENTS.md.**
**Agents and contributors: verify every rule below before opening a PR.**

---

## 1. Binding Invariants — CI Enforced

These 12 rules are enforced by CI on every PR.
A PR that breaks any invariant will not merge. No exceptions.

| ID | Invariant | CI Check |
|----|-----------|----------|
| I-01 | Core headers compile standalone — zero OSAL or BSP includes | Compile Core headers in isolation |
| I-02 | `sizeof(EmbedIQ_Event_t) == 14` on all targets | `static_assert` + CI multi-target compile |
| I-03 | `offsetof(EmbedIQ_Msg_t, payload) == 20` on all targets | `static_assert` |
| I-04 | `messages.iq` is the only payload schema source | Grep: no hand-written payload structs |
| I-05 | Zero `test/*` symbols in production binary | `nm`/`readelf` on release binary |
| I-06 | `EMBEDIQ_DEBUG_JSON` never defined in release builds | CMake release config check |
| I-07 | No `malloc`/`free` in Shell 1 | Binary analysis: no malloc in Shell 1 objects |
| I-08 | All sizing parameters in `embediq_config.h` | `validator.py` fails build if hardcoded |
| I-09 | Native bus compiles without bridge code when flag undefined | CI build without bridge flag |
| I-10 | Sub-fn registrations only inside FB `init_fn` | Code review + static analysis |
| I-11 | `osal_signal` non-NULL only in Platform FB sub-functions | Code review: only in `bsp/` + `shell2/` |
| I-12 | Developer First Hour test passes before Phase 1 launch | Manual gate — 3 naive engineer sessions |
| I-13 | `sequence` is canonical event ordering — never use `timestamp_us` for ordering or gap detection | Code review + static analysis on Observatory consumers |
| I-14 | Core header v1 API surface unchanged after v1 release — CI v1 compatibility shim enforces no breaking changes | `tests/compat/fb_v1_compat.c` compile on every PR |

---

## 2. Architectural Rules — Code Review Enforced

These rules cannot all be automated but are enforced at code review.
Any PR violating these will be rejected.

| ID | Rule |
|----|------|
| R-01 | No cross-FB function calls. Cross-FB communication = messages only. |
| R-02 | No dynamic allocation (`malloc`/`free`/`new`/`delete`) in Shell 1 or Core. |
| R-03 | C11 for Core and Shell 1. C++ opt-in wrapper only in Shell 3 and above. |
| R-04 | Apache 2.0 only in Core/Shell 1/Shell 2. No GPL or LGPL anywhere. |
| R-05 | Every layer: one interface header, one or more impl directories. |
| R-06 | Every module: `EmbedIQ_Module_Meta_t` descriptor. |
| R-07 | Test infrastructure compiles to zero bytes in production. |
| R-08 | Core headers compile standalone — no OSAL or platform headers. |
| R-09 | Native bus never depends on Bridge. |
| R-10 | `messages.iq` is the only authoritative payload schema source. |
| R-11 | All sizing in `embediq_config.h`. Validated by `validator.py` at build. |
| R-12 | Sub-function registrations only inside FB `init_fn`. |
| R-15 | Sub-function execution order when multiple sub-fns match same message = registration order (init_order ascending). Must be deterministic. | Code review |

---

## 3. AI Coding Checklist — Verify Before Every PR

Work through every item below before submitting. Each has a ✓ correct and ✗ wrong example.

---

### R-sub-01 · Subscription arrays must be static const

```c
// ✓ CORRECT — use EMBEDIQ_SUBS() macro, enforces static const storage
EMBEDIQ_SUBS(my_subs, MSG_TEMP_READING, MSG_TIMER_100MS);
// then in config:
.subscriptions = my_subs, .subscription_count = 2

// ✗ WRONG — compound literal has automatic storage duration
.subscriptions = (uint16_t[]){MSG_TEMP_READING, MSG_TIMER_100MS}
// Pointer is invalid after fb_init() returns. Silent memory corruption.
```

---

### R-sub-02 · No cross-FB function calls

```c
// ✓ CORRECT — message to another FB
embediq_publish(fb_handle, &msg);

// ✓ ALSO CORRECT — intra-FB helper, same FB only
float dew = helper_calc_dewpoint(temp, humidity);

// ✗ WRONG — direct call into another FB
other_fb_process_temp(reading);
// Breaks Actor model. Violates R-01.
```

---

### R-sub-03 · No OSAL calls from application FB code

```c
// ✓ CORRECT — let the framework create the thread
embediq_fb_register(&config);

// ✗ WRONG — never call RTOS directly from app FB
xTaskCreate(...);
embediq_osal_task_create(...);
```

---

### R-sub-04 · Never compare timestamps across a wrap boundary

```c
// ✓ CORRECT — sequence is always monotonic
uint32_t gap = event_b.sequence - event_a.sequence;

// ✗ WRONG — timestamp_us wraps at ~71 minutes
if (event_b.timestamp_us > event_a.timestamp_us) { ... }
```

---

### R-sub-05 · Never allocate messages dynamically

```c
// ✓ CORRECT — stack allocation
EmbedIQ_Msg_t msg = { .msg_id = MSG_TEMP_ALERT };

// ✗ WRONG — no malloc in Shell 1 or Core
EmbedIQ_Msg_t *msg = malloc(sizeof(EmbedIQ_Msg_t));
```

---

### R-sub-06 · Message IDs are never reused — only additive payload changes

```c
// ✓ CORRECT — add new optional field at the END of an existing message
// ✓ CORRECT — increment schema_id when any change is made

// ✗ WRONG — remove, rename, or reorder an existing field
// ✗ WRONG — reuse a retired message ID for a new message type
// Breaks Observatory replay, Studio compatibility, stored traces.
```

---

### R-sub-07 · Fault vs Error — choose correctly

```c
// ✓ CORRECT — FB cannot continue (hardware fail, config invalid)
embediq_fb_report_fault(fb, reason);

// ✓ CORRECT — FB can continue, notifying others of recoverable problem
embediq_publish(fb, &error_msg);  // MSG_ERROR_TIMEOUT etc.

// ✗ WRONG — report_fault() for a timeout that should be retried
// ✗ WRONG — publish MSG_ERROR_* when the FB is actually broken
```

---

### R-sub-08 · Sub-fn registration: no init logic before all registrations complete

```c
// ✓ CORRECT — register all sub-fns first, then return from init_fn
static void my_fb__init(EmbedIQ_FB_Handle_t *fb, void *data) {
    embediq_subfn_register(fb, &subfn_a_config);
    embediq_subfn_register(fb, &subfn_b_config);
    // Framework runs init pass AFTER this returns
}

// ✗ WRONG — calling sub_fn init_fn manually before all registrations done
// ✗ WRONG — calling embediq_publish() before framework init pass runs
```

---

### R-sub-09 · Dotted addressing is test-only

```c
// ✓ CORRECT — only valid inside test/ scenario files
// "temp_controller.temp_monitor"  →  test/embediq_test.h only

// ✗ WRONG — dotted addressing in production code
embediq_bus_send(fb, "temp_controller.temp_monitor", &msg);
// Sub-fns are invisible to the bus. Resolver not compiled in release.
```

---

### R-sub-10 · Platform headers never in Core headers

```c
// ✓ CORRECT — opaque handle types only in Core
typedef struct EmbedIQ_Task_s EmbedIQ_Task_t;

// ✗ WRONG — platform includes in Core header
#include "freertos/FreeRTOS.h"   // in any embediq_*.h
#include "esp_system.h"          // in any embediq_*.h
// Core must compile standalone on all targets.
```

---

### R-sub-11 · osal_signal is for Platform FBs only

```c
// ✓ CORRECT — ISR-driven Platform FB sub-function
.osal_signal = uart_rx_signal   // in shell2/fb_uart/ only

// ✗ WRONG — application FB sub-function with osal_signal
.osal_signal = some_signal      // in any application FB
// App FBs wake on bus messages only. Never on hardware signals directly.
```

---

### R-sub-12 · Do not generate code for v1 non-goals

```
✗ Worker pool executor
✗ Zero-copy message pool
✗ Bridge auth or retry logic
✗ Dynamic subscription changes
✗ Zephyr OSAL
✗ STM32 / nRF52 BSP
✗ fb_i2c / fb_spi / fb_usb
✗ 64-bit MCU timestamps
See AGENTS.md Section 6 for the full list.
```

---

## 4. C11 Style Guide

### Naming

| Element | Convention | Example |
|---------|------------|---------|
| Types | `PascalCase_t` | `EmbedIQ_FB_Handle_t` |
| Functions (public) | `embediq_module_verb()` | `embediq_fb_register()` |
| Functions (private) | `module__verb()` | `temp_reader__init()` |
| Sub-function names | `subfn_name__verb()` | `byte_collector__run()` |
| Constants / macros | `UPPER_SNAKE_CASE` | `EMBEDIQ_MSG_MAX_PAYLOAD` |
| Config macros | `EMBEDIQ_FEATURE_*` | `EMBEDIQ_FB_QUEUE_DEPTH_HIGH` |
| Message IDs | `MSG_NOUN_VERB` | `MSG_TEMP_READING` |
| Files | `embediq_module.h/.c` | `embediq_fb.h` |

### File structure (every .c file)

```c
// 1. File header comment (module name, brief description, license)
// 2. Includes — system headers first, then embediq headers, then local
// 3. Private type definitions
// 4. Private macro definitions
// 5. Static (private) function declarations
// 6. Static data
// 7. Public function implementations
// 8. Static function implementations
```

### Header guards

```c
#ifndef EMBEDIQ_MODULE_H
#define EMBEDIQ_MODULE_H

#ifdef __cplusplus
extern "C" {
#endif

/* content */

#ifdef __cplusplus
}
#endif

#endif /* EMBEDIQ_MODULE_H */
```

### Formatting

- Indentation: 4 spaces. No tabs.
- Max line length: 100 characters.
- Opening brace on same line for functions and control structures.
- One blank line between logical sections inside a function.
- All public API functions documented with a brief comment above the declaration.
- `.clang-format` at repo root is the authority. Run before every PR.

---

## 5. Forbidden Patterns — Instant PR Rejection

```c
// ✗ FORBIDDEN: Cross-FB direct call
other_fb_do_something(data);

// ✗ FORBIDDEN: malloc/free anywhere in Shell 1 or Core
void *buf = malloc(size);

// ✗ FORBIDDEN: Compound literal for subscription array
.subscriptions = (uint16_t[]){MSG_A, MSG_B}

// ✗ FORBIDDEN: RTOS/platform header in Core header
#include "FreeRTOS.h"  // inside any embediq_*.h

// ✗ FORBIDDEN: Hand-written payload struct (must come from messages.iq)
typedef struct { float temp; uint8_t id; } MyPayload_t;  // not generated

// ✗ FORBIDDEN: Hardcoded sizing (must be in embediq_config.h)
uint8_t queue[32];   // 32 must come from EMBEDIQ_FB_QUEUE_DEPTH_NORMAL

// ✗ FORBIDDEN: test/ infrastructure in production path
#include "test/embediq_test.h"  // in any non-test source file

// ✗ FORBIDDEN: GPL or LGPL dependency in Core/Shell1/Shell2
// Check every new dependency license before adding it.

// ✗ FORBIDDEN: Timestamp comparison across possible wrap boundary
if (b.timestamp_us > a.timestamp_us)  // use sequence delta instead
```

---

## 6. Pre-PR Checklist

Run this before opening every PR. Agents must include this checklist
as a comment on their own PR.

```
[ ] tools/check_invariants.sh passes (run locally before push)
[ ] cmake --build . --config Release succeeds on host
[ ] All unit tests pass: ctest -C Release
[ ] Thermostat scenario passes (if Shell 1 or Shell 2 change)
[ ] No forbidden patterns present (Section 5 above)
[ ] All new subscription arrays use EMBEDIQ_SUBS() macro
[ ] No new platform headers added to Core headers
[ ] MODULE.md status updated if module status changed
[ ] LAYER.md updated if layer surface changed
[ ] CHANGELOG.md entry added for any public API change
[ ] clang-format applied: clang-format -i <changed_files>
[ ] No non-goals (AGENTS.md Section 6) implemented
```

---

## 7. Promotion Checklist (contrib/ → main)

For a contribution in `contrib/` to be promoted to `bsp/` or `shell2/`:

```
[ ] CI passes on host (all invariant checks green)
[ ] Unit tests exist and pass
[ ] Integration test or usage example exists
[ ] Module follows naming and style rules (this document)
[ ] No new Core API changes required
[ ] MODULE.md complete with status, dependencies, tests listed
[ ] Usage example exists in module or examples/ directory
[ ] Maintainer/owner identified (GitHub username)
[ ] No GPL or LGPL dependencies introduced
```

---

## 8. Architectural Review Triggers

The following changes require architectural review before merging.
Open an issue tagged `arch-review` and wait for sign-off.

```
- Any change to a Core header (post v0.1.0 freeze)
- Any change to EmbedIQ_Event_t layout (Observatory binary format)
- Any change to EmbedIQ_Msg_t layout (bridge protocol)
- Any new layer dependency direction (e.g. Shell 1 depending on Shell 2)
- Any new message ID in the framework-reserved range (0x0001–0x00FF)
- Any new build system dependency
- Any change to the messages.iq schema format
```

---

*EmbedIQ — embediq.com — Apache 2.0*
*This file is maintained by the core team. Changes require architectural review.*

---

## 4. Message ID Namespace Rules — Binding

All message IDs must fall within the assigned namespace range.
Violating this rule causes silent ecosystem corruption between community FBs.

| Range | Owner | Rule |
|-------|-------|------|
| 0x0000 – 0x03FF (0–1023) | EmbedIQ Core | Framework internals only. Never use in application or community FBs. |
| 0x0400 – 0x13FF (1024–5119) | Official EmbedIQ components | Assigned by embediq/embediq repo. Listed in messages_registry.json. |
| 0x1400 – 0xFFFF (5120–65535) | Community / third-party | MUST reserve range in messages_registry.json before use. |

**Rules:**
- Message IDs are never reused. A retired ID is tombstoned in messages_registry.json.
- Community FBs must reserve a range BEFORE publishing. No reservation = undefined collision risk.
- CI lint checks that no two messages.iq files in the repo define the same ID.
- AI agents: verify message IDs fall in the correct namespace range before emitting any message definition.

---

## 5. Boot Phase Rules — Binding

Every FB must declare its boot phase. Default = APPLICATION (Phase 3).

| Phase | Value | FBs | Rule |
|-------|-------|-----|------|
| PLATFORM | 1 | fb_uart, fb_timer, fb_gpio | Hardware peripherals only |
| INFRASTRUCTURE | 2 | fb_nvm, fb_watchdog, fb_cloud | Services consumed by application FBs |
| APPLICATION | 3 | Developer FBs (default) | Business logic |
| BRIDGE | 4 | External FBs, Studio | Best-effort, may join late |

**Rules:**
- A Phase 2 FB with depends_on pointing to a Phase 3 FB = BOOT FAULT.
- Within a phase, depends_on resolves ordering via topological sort.
- Circular depends_on = BOOT FAULT with clear diagnostic error.
- Default boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION if not declared.

