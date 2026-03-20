# ROADMAP.md — EmbedIQ Public Roadmap

**Honest about what is built, what is next, and what is planned.**

This roadmap is updated when phases complete or plans change. Dates are estimates based on solo development. Community contributions accelerate everything.

---

## Phase 0 — Foundations  ← active now

**Goal:** Repo scaffold, all header contracts, CMake structure, AI agent framework. No working firmware yet — but everything a developer needs to understand the architecture and start contributing.

**Deliverables:**
- [ ] All 13 Core headers (`embediq_fb.h`, `embediq_subfn.h`, `embediq_bus.h`, `embediq_msg.h`, `embediq_sm.h`, `embediq_obs.h`, `embediq_osal.h`, `embediq_time.h`, `embediq_bridge.h`, `embediq_meta.h`, `embediq_endpoint.h`, `embediq_msg_catalog.h`, `hal/`)
- [ ] `embediq_config.h` + `validator.py`
- [ ] CMake structure: Core-only build, host build, target build
- [ ] CI: compile Core headers in isolation, license scan
- [ ] `messages.iq` generator (Python/Node) — produces typed C structs from schema
- [ ] AGENTS.md, CODING_RULES.md, CONTRIBUTING.md, ARCHITECTURE.md
- [ ] Smart thermostat example skeleton (headers only, no implementation)

**Success criterion:** `cmake -B build -DEMBEDIQ_PLATFORM=host && cmake --build build` completes clean. Core headers compile standalone. CI green.

**Estimated completion:** ~2 weeks from repo creation.

---

## Phase 1 — Core Engine

**Goal:** The framework runs. The smart thermostat example runs on Pi/Linux with Observatory CLI output. Zero hardware required.

**Deliverables:**
- [ ] OSAL — Pi/Linux POSIX implementation (primary) + FreeRTOS host mock
- [ ] Core FB engine — FB create/destroy, init sequence, lifecycle
- [ ] Message bus — publish/subscribe, three-queue priority, routing table
- [ ] FSM engine — table-driven dispatch, auto-guard evaluation
- [ ] Observatory — bus tap, event capture, CLI transport
- [ ] fb_uart — two-zone ISR model, ring buffer, thread delivery
- [ ] fb_watchdog — health-token model, miss detection, reset reason logging
- [ ] fb_nvm — key-value store, journalled write, host volatile backend
- [ ] fb_cloud_mqtt — MQTT connect/publish/subscribe/reconnect, exponential backoff
- [ ] fb_ota — firmware download, signature verification hook, dual-bank atomic write, rollback
- [ ] Smart thermostat example — running end-to-end with Observatory output
- [ ] Test harness — `embediq_test.h`, bus_inject, scenario runner
- [ ] Full host test suite — CI green on every commit
- [ ] `messages_registry.json` — initial namespace allocations for all official FBs
- [ ] v1 compatibility shim (`tests/compat/fb_v1_compat.c`) — CI enforcement of core contract freeze
- [ ] Boot phase model implemented — `EmbedIQ_BootPhase_t` enum, phase-gated startup

**Success criterion:** Run `./build/examples/thermostat/embediq_thermostat`. See Observatory output. Every FB state transition, every message, timestamped. Zero printf in application FBs. `ctest --test-dir build` passes 100%.

**Estimated completion:** ~6 weeks after Phase 0.

---

## Phase 2 — Platform FBs and Second Target

**Goal:** EmbedIQ runs on real hardware. FreeRTOS bare-metal target confirmed. Platform FB library expanded.

**Deliverables:**
- [ ] FreeRTOS bare-metal OSAL (ESP32 or STM32 target)
- [ ] fb_uart on FreeRTOS target — UART loopback confirmed on hardware
- [ ] fb_i2c, fb_spi, fb_timer Platform FBs
- [ ] fb_watchdog + fb_nvm on FreeRTOS Flash target
- [ ] Industrial sensor gateway example (Modbus reader)
- [ ] `messages.iq` integration — schema_id verified at runtime in debug builds
- [ ] EmbedIQ Registry v0 — package manifest format, local registry, `embediq add fb_cloud_mqtt`
- [ ] `embediq` CLI — project init, FB add, build

**Success criterion:** Smart thermostat running on ESP32 or STM32 with same code as Pi/Linux host. Only the BSP changes.

**Estimated completion:** ~4 weeks after Phase 1.

---

## Phase 3 — Ecosystem and Second OSAL

**Goal:** Zephyr OSAL. Bridge and External FB support. Studio CLI preview.

**Deliverables:**
- [ ] Zephyr OSAL — "EmbedIQ + Zephyr" for Nordic/NXP/ST developer community
- [ ] Bridge daemon — External FB support (Python, Node.js)
- [ ] fb_telemetry, fb_provisioning
- [ ] `embediq-python` client SDK
- [ ] Observatory Studio CLI preview — terminal-based session viewer
- [ ] Mbed OS migration guide — complete and published

**Success criterion:** External FB (Python) communicating with Native FBs via Bridge. Zephyr OSAL — same thermostat example on nRF52840.

**Estimated completion:** ~4 weeks after Phase 2.

---

## Phase 4 — Commercial (EmbedIQ Studio)

**Goal:** EmbedIQ Studio v1. The Observatory stream becomes visual.

**Deliverables:**
- [ ] Studio live view — FBs as swimlanes, messages as arrows, FSM states as coloured blocks
- [ ] Session capture and replay — `.embediq` session file, offline analysis
- [ ] FSM visualiser — click any FB, see its FSM diagram with current state highlighted
- [ ] Studio pricing launch (individual / team / enterprise / India pricing)

**EmbedIQ Cloud** planning begins during Phase 4. Fleet observability, OTA management, and anomaly detection are Cloud-tier features.

---

## Completed

Nothing yet — Phase 0 is active.

---

## What is not on this roadmap

Things that are real limitations, not omissions. See [ARCHITECTURE.md](ARCHITECTURE.md) non-goals section.

- Hard real-time scheduling guarantees — the RTOS provides these, not EmbedIQ
- Safety certifications (IEC 61508, ISO 26262) — EmbedIQ supports certified products but does not carry certification itself
- Ultra-constrained targets (< 64KB flash) — not v1 scope
- Worker pool executor — Phase 2+ non-goal
- Bridge authentication — Phase 3 non-goal

---

## How to influence the roadmap

Open a GitHub Discussion with the `roadmap` label. Describe your use case and what you need. Community demand directly influences phase priorities.

---

*Apache 2.0 · embediq.com · © 2026 Ritzy Lab*


## Future Work (declared, not scheduled)

These are real capabilities that the architecture is designed to support. Implementation is Phase 3+.

- **Observatory filter rules** — subscribe to events from a specific FB only. Architecture does not prevent this; v1 captures all events.
- **Resource diagnostics as Observatory events** — FB CPU slice, task switch latency, per-queue throughput. Available at `EMBEDIQ_OBS_LEVEL 3` in v1, automated reporting is Phase 2+.
- **messages.iq static size assertions** — generator emits `static_assert` for every schema. Phase 1 foundation is in place.
- **Runtime EMBEDIQ_OBS_LEVEL switching** — compile-time only in v1. Dynamic switching is Phase 3.
- **Zero-copy message pool** — copy-by-value in v1. Pool is a Phase 2+ non-goal.
- **fb_power_manager** — FB suspend/resume lifecycle hooks exist in v1. Power policy manager is Phase 2+.

