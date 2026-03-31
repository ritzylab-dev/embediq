# EmbedIQ Migration Guide

**Decision I (Final Decision Set v2.0) — Four Migration Entry Points**
**Author:** Ritesh Anand — embediq.com | ritzylab.com
**SPDX-License-Identifier:** Apache-2.0

---

## Overview

EmbedIQ defines four migration patterns for bringing existing embedded systems under the framework. The correct pattern is determined by the state of the existing codebase and the target compliance level.

| Pattern | Starting point | Scope | IEC 62304 restriction |
|---|---|---|---|
| **Greenfield** | New project, no existing code | Full framework from day one | None |
| **Add-Observatory** | Existing code, keep architecture | Observatory layer only — no FB refactor | None |
| **Strangler Fig** | Existing code, evolve to FB model | Incremental FB-by-FB migration | Class A and Class B only |
| **Module-Only** | Component reuse, no framework | Individual components as libraries | None |

---

## Pattern 1: Greenfield

**Use when:** Starting a new embedded system from scratch with no legacy constraints.

**What you get from day one:** Full Functional Block model, message bus, Observatory, all compliance tooling.

**Steps:**

1. Clone the EmbedIQ repository and run the Phase-1 build to confirm your host environment builds cleanly.
   ```
   cmake -B build -DEMBEDIQ_PLATFORM=host
   cmake --build build
   ctest --test-dir build --output-on-failure
   ```
2. Copy `examples/thermostat/` as the structural template for your first application FBs. The thermostat example is the canonical style reference — it demonstrates boot phases, message-driven actor model, FSM engine, and Observatory integration in a realistic scenario.
3. Define your message schema in `messages.iq` before writing any FB code. Follow the namespace rules in `AGENTS.md §2A` — reserve a community range in `messages_registry.json` if you are writing a community FB.
4. Declare every FB's `boot_phase` explicitly in its `EmbedIQ_FB_Config_t`. See `AGENTS.md §2C`.
5. Register FB metadata with `embediq_meta_register()` from the FB's `init_fn`. Set `safety_class` to the appropriate `"STD:LEVEL"` string, or `"NONE"` for non-safety FBs.

**When to call Observatory:** Emit `EMBEDIQ_OBS_EVT_BOOT` from your top-level init sequence. From that point, every message bus event, FSM transition, and fault is automatically captured — no additional instrumentation is needed for those paths.

---

## Pattern 2: Add-Observatory

**Use when:** The existing firmware architecture is sound and does not need to change, but you need a tamper-evident audit trail — typically to satisfy IEC 62443 Section 3-3, EU CRA, NERC CIP-007, or a customer audit requirement.

**What changes:** Only the Observatory layer is added. Existing code structure, threading model, and message passing mechanism are untouched.

**What does NOT change:** Existing FB boundaries (if any), existing message formats, existing RTOS configuration.

**Steps:**

1. Add `core/include/` to your build system's include path.
2. Add `core/src/observatory/obs.c` to your build. This is the only EmbedIQ source file required for Observatory-only deployment.
3. Implement the HAL contract `hal/embediq_hal_obs_stream.h` for your target platform. On POSIX/Linux, `hal/posix/hal_obs_stream_posix.c` is provided as a reference. On bare-metal targets, implement `hal_obs_stream_write()` to write to flash, UART, or a circular buffer in NVM.
4. Call `embediq_obs_session_begin()` at boot to initialise the Observatory session header. Populate `EmbedIQ_Obs_Session_t` with your device ID, firmware version, and build ID.
5. Instrument your existing code with `embediq_obs_emit()` calls at the points that matter for your audit requirement. Use the event type constants from `core/include/embediq_obs.h`. You do not need to instrument everything — start with the events your auditor requires (faults, security incidents, configuration changes, boot/shutdown).

**Zero-dependency option:** Link only `libembediq_obs` (the INTERFACE CMake target defined in `CMakeLists.txt`). This target exposes only `core/include/` — no OSAL, no FB engine, no message bus. Suitable for existing products being retrofitted with Observatory without any EmbedIQ framework dependency.

```cmake
# In your existing CMakeLists.txt:
find_package(embediq REQUIRED)
target_link_libraries(my_existing_firmware PRIVATE embediq_obs)
```

**Memory footprint:** The Observatory ring buffer size is controlled by `EMBEDIQ_OBS_RING_BUFFER_SIZE` in `embediq_config.h`. Each event record is exactly 14 bytes (I-02, `_Static_assert` enforced). A ring buffer of 128 events occupies 1792 bytes of RAM. The session header (`EmbedIQ_Obs_Session_t`, 40 bytes, I-14) is a one-time fixed allocation.

---

## Pattern 3: Strangler Fig

**Use when:** The existing firmware is a monolithic or loosely-structured codebase that needs to be progressively migrated to the Functional Block model over multiple releases, without a flag-day rewrite.

**IEC 62304 restriction — mandatory:** The Strangler Fig pattern is permitted only for systems classified as **IEC 62304 Class A or Class B**. It is NOT permitted for IEC 62304 Class C systems. The restriction exists because the Strangler Fig transition period creates a hybrid architecture in which some code paths are under the EmbedIQ contract and some are not — an auditable boundary that Class C requirements cannot accommodate without disproportionate certification effort.

For Class C systems, use Pattern 1 (Greenfield rewrite of the Class C module) or Pattern 2 (Add-Observatory only, no architectural migration).

**What the pattern looks like:**

```
Release N:   [Legacy monolith] + Observatory instrumentation only (Pattern 2 applied)
Release N+1: [FB: sensor driver] + [Legacy monolith minus sensor driver]
Release N+2: [FB: sensor driver] + [FB: control loop] + [Legacy monolith minus both]
Release N+k: [Full FB model] — migration complete
```

Each release migrates one logically cohesive subsystem into an FB, adds it to the EmbedIQ message bus, and removes it from the legacy codebase. The legacy codebase shrinks; the EmbedIQ FB graph grows.

**Steps:**

1. Apply Pattern 2 first. Add Observatory instrumentation to the legacy codebase. This is the foundation — the audit trail must be continuous from the first migration release.
2. Identify the first FB candidate. Choose a subsystem that: (a) has a clean input/output boundary, (b) does not share mutable state with many other subsystems, and (c) is currently a source of bugs or technical debt (motivates the migration). Sensor drivers and watchdog handlers are typical first candidates.
3. Write the FB following the standard `EmbedIQ_FB_Config_t` contract. Define its messages in `messages.iq`. Register metadata with `embediq_meta_register()`.
4. Replace the legacy subsystem with the FB in the build. The legacy code path for that subsystem is deleted (not commented out — deleted). This is the "strangle" step.
5. Verify that Observatory output is continuous across the transition. The `sequence` field in `EmbedIQ_Event_t` (I-13) is the monotonic ordering key — gaps in sequence numbers indicate an audit gap and must be investigated.
6. Repeat from step 2 for the next FB candidate.

**Gateway FB pattern (architectural note):** When the legacy code and the emerging FB model need to co-exist on the same message bus (e.g., a legacy interrupt handler needs to publish events to the bus), an External FB wrapping the legacy subsystem can serve as a gateway. The Gateway FB is a thin adapter: it calls the legacy code via a function pointer and translates its outputs into typed EmbedIQ messages. This pattern isolates legacy code behind a clean FB boundary without requiring it to be migrated first. Full specification of the Gateway FB pattern is an open architectural item for a future design note.

---

## Pattern 4: Module-Only

**Use when:** The project does not want the full EmbedIQ framework but wants to reuse specific components (the FSM engine, the Observatory, the message bus in isolation, etc.).

**What changes:** Only the specific component is integrated — no `embediq_engine_boot()`, no FB registry, no message bus unless specifically chosen.

**Applicable components:**

- **Observatory standalone:** See Pattern 2 — the `libembediq_obs` INTERFACE target is the canonical standalone deployment.
- **FSM engine standalone:** `core/src/fsm/fsm_engine.c` + `core/include/embediq_sm.h`. Table-driven state machine with guard/action callbacks. No OSAL dependency if `embediq_obs_emit()` stubs are provided.
- **Message bus standalone:** `core/src/bus/message_bus.c` + `core/include/embediq_bus.h`. Requires the OSAL layer (`osal/<target>/`) for mutex and semaphore primitives.

**Steps:**

1. Identify the component you need and its minimal dependency set. Check `CMakeLists.txt` for the `target_link_libraries` chain — every component's dependencies are explicit.
2. Add the component source and its transitive dependencies to your build system.
3. Provide stub implementations for any HAL contracts the component uses but your target does not need at full fidelity (e.g., if you are using the FSM engine but not the Observatory, provide a no-op `embediq_obs_emit()` stub).
4. Ensure `core/include/` is on your include path. All EmbedIQ public headers are in this directory and are self-contained.

---

## IEC 62304 Restriction Summary

| IEC 62304 Class | Greenfield | Add-Observatory | Strangler Fig | Module-Only |
|---|---|---|---|---|
| Class A | Permitted | Permitted | **Permitted** | Permitted |
| Class B | Permitted | Permitted | **Permitted** | Permitted |
| Class C | Permitted | Permitted | **NOT permitted** | Permitted |

The Class C restriction on Strangler Fig is non-negotiable. Do not attempt to apply Strangler Fig to a Class C system without first consulting your notified body and obtaining explicit sign-off on the hybrid architecture risk analysis.

---

## Phase-2 Migration Items (Tracked)

The following migration tooling is planned for Phase 2 and is not yet available:

- **AI_CODER_SESSION TLV CI plugin:** Automatically emits `AI_CODER_SESSION` TLV and triggers the AI Code Review Gate for safety-classified FBs. (AI-08, `AI_FIRST_ARCHITECTURE.md §4`.)
- **Observatory-only package:** A standalone npm/pip/CMake fetchable package for `libembediq_obs` that does not require cloning the full EmbedIQ repository.
- **Migration health check script:** A Python script that analyses an existing C codebase and produces a Strangler Fig migration roadmap with FB candidate ranking.

---

*Document version 1.0 — EmbedIQ Final Decision Set v2.0 implementation.*
