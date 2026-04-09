# ROADMAP.md — EmbedIQ Public Roadmap

**Honest about what is built, what is next, and what is planned.**

This roadmap is updated when phases complete or when a PR confirms a new deliverable.
Dates are estimates based on solo development. Community contributions accelerate everything.

---

## Phase 0 — Foundations ← COMPLETE

**Goal:** Repo scaffold, all header contracts, CMake structure, AI agent framework.
No working firmware yet — but everything a developer needs to understand the
architecture and start contributing.

**Deliverables:**
- [x] All 13 core headers (`embediq_fb.h`, `embediq_subfn.h`, `embediq_bus.h`,
      `embediq_msg.h`, `embediq_sm.h`, `embediq_obs.h`, `embediq_osal.h`,
      `embediq_time.h`, `embediq_bridge.h`, `embediq_meta.h`, `embediq_endpoint.h`,
      `embediq_msg_catalog.h`, `hal/`)
- [x] `embediq_config.h` + `validator.py`
- [x] CMake structure: Core-only build, host build, target build
- [x] CI: compile Core headers in isolation, licence scan
- [x] `messages.iq` generator — produces typed C structs from schema
- [x] AGENTS.md, CODING_RULES.md, CONTRIBUTING.md, ARCHITECTURE.md
- [x] Smart thermostat example skeleton (headers only, no implementation)

**Success criterion:** `cmake -B build -DEMBEDIQ_PLATFORM=host && cmake --build build`
completes clean. Core headers compile standalone. CI green.

---

## Phase 1 — Core Engine ← COMPLETE

**Goal:** The framework runs. The smart thermostat example runs on Linux/Pi with
Observatory CLI output. Zero hardware required.

**Deliverables:**
- [x] OSAL — POSIX/Linux implementation (`osal/posix/`)
- [x] Core FB engine — FB create/destroy, init sequence, lifecycle, boot phase ordering
      (`EmbedIQ_BootPhase_t` enum: PLATFORM_INIT → APP_INIT → RUNNING → SHUTDOWN)
- [x] Message bus — publish/subscribe, three-queue priority (HIGH/NORMAL/LOW),
      routing table, overflow policies
- [x] FSM engine — table-driven dispatch, auto-guard evaluation, Observatory event
      on every transition
- [x] Observatory — complete observability system:
    - 7-family event taxonomy with band encoding (0x10–0x7F), zero runtime overhead
    - Compile-time `EMBEDIQ_TRACE_LEVEL` (0–3) + per-family zero-overhead emit macros
    - `EmbedIQ_Obs_Session_t` 40B session identity record + `session_begin/get` API
    - 14-byte event record, `_Static_assert` enforced
    - `.iqtrace` open binary format — TLV-framed, Apache 2.0 spec
      (`docs/observability/iqtrace_format.md`)
    - `tools/embediq_obs/` CLI — `decode / stats / filter / export / tail`
- [x] HAL refactor — fb_timer, fb_nvm, fb_watchdog split into portable Driver FBs
      (`fbs/drivers/`) + POSIX HAL implementations (`hal/posix/peripherals/`).
      `platform/posix/` retired. Pattern established for all future targets.
- [x] fb_watchdog — health-token model, miss detection, reset reason logging
- [x] fb_nvm — key-value store, journalled write, host volatile backend
- [x] fb_timer — POSIX timer backend
- [x] Smart thermostat example — running end-to-end with Observatory output
- [x] Full host test suite — 20/20 tests passing, CI green on every commit
- [x] v1 compatibility shim (`tests/compat/fb_v1_compat.c`) — CI enforcement
      of core contract freeze
- [x] Boot phase model implemented — `EmbedIQ_BootPhase_t` enum, phase-gated startup
- [x] contrib_sim gate — `tests/contrib_sim/run.sh` fresh-clone simulation gate
      (Gate 14, used for dev→main promotion)

**Library Architecture phase — COMPLETE (PRs #79–#87)**

Between Phase 1 and Phase 2, a library integration architecture was designed and
fully implemented. This work is required before any Phase 2 Service FB can be built.

- [x] LIB-1 (PR #79): ARCHITECTURE.md + CODING_RULES.md — library rules R-lib-1
      through R-lib-4 documented
- [x] LIB-2 (PR #80): boundary_checker.py extended + `check_lib_obs.py` CI script
      (--strict, enforced on every PR)
- [x] LIB-3 (PR #82): `components/` directory + `embediq_crc` reference component
      (C1–C6 rules). `third_party/` directory established.
- [x] LIB-4A (PR #83): `core/include/ops/` subdirectory. `embediq_platform.h` +
      `platform_lib_declare()` API. `embediq_bus_token_t`. Ops table contracts
      (`embediq_mqtt.h`, `embediq_ota.h`, `embediq_tls.h`). Observatory source ID
      namespace (OSAL 0xC0–0xCF, HAL 0xD0–0xDF, LIB 0xE0–0xEF).
      Community band reservation (0x90–0xFF).
- [x] LIB-4B (PR #84): OSAL `bool` → `embediq_err_t` refactor.
      `EMBEDIQ_SUCCEEDED(err)` / `EMBEDIQ_FAILED(err)` helper macros.
- [x] LIB-5 (PR #85): `hal/posix/` restructured into `peripherals/` + `ops/`.
      TLS ops table stub (`hal_tls_posix.c`). mbedTLS `third_party/mbedtls/`
      with 7-field `VENDORING.md`. `test_lib_tls_stub.c`.
- [x] CI-1 (PR #86): 7 CI enforcement scripts in `tools/ci/` — wired into pipeline
      warning-only: `component_globals_check.py`, `component_state_check.py`,
      `vendoring_check.py`, `vendoring_date_check.py`, `ops_version_check.py`,
      `licence_check.py`, `binary_blob_check.py`
- [x] Cleanup (PR #87): boundary_checker.py dead entries removed. N1 migration
      notice added to `docs/observability/iqtrace_format.md`. `EMBEDIQ_SUCCEEDED`
      / `EMBEDIQ_FAILED` macros added to `embediq_osal.h`.

**Success criterion:** `./build/examples/thermostat/embediq_thermostat` runs.
Observatory output visible. Every FB state transition and message timestamped.
Zero printf in application FBs. 20/20 tests pass. All CI scripts green.

---

## Phase 2 — Service FBs and Tooling (Linux-first)

**Goal:** EmbedIQ's service layer is complete and verified on Linux. No hardware
required for any item in this phase. A developer with a Linux or macOS machine
can contribute to every deliverable.

**Cross-layer observability — OSAL and HAL hooks:**
- [ ] XOBS-1: OSAL observation — `EMBEDIQ_OBS_EVT_OSAL_FAULT = 0x66` added to
      `embediq_obs.h`. `EMBEDIQ_OSAL_OBS_*` obligation macros added to
      `embediq_osal.h`. POSIX OSAL updated to call macros on all failure paths.
      `tools/ci/check_osal_obs.py` CI script wired into pipeline (script exists in
      `tools/ci/` — wiring and source macros are the remaining work).
- [ ] XOBS-2 (POSIX side): HAL observation — `EMBEDIQ_OBS_EVT_HAL_FAULT = 0x67`
      added to `embediq_obs.h`. `HAL_OBS_EMIT_ERROR` macro added to `hal_defs.h`.
      All `hal/posix/` implementations updated to call macro on non-HAL_OK returns.
      `tools/ci/check_hal_obs.py` CI script wired into pipeline (script exists in
      `tools/ci/` — wiring and source macros are the remaining work).
- [ ] XOBS-3 (queue depth): `EMBEDIQ_OBS_EVT_BUS_QUEUE_DEPTH` (RESOURCE band 0x41)
      — emitted when any FB queue exceeds `EMBEDIQ_QUEUE_WARN_THRESHOLD` (default
      75%). Configurable in `embediq_config.h`. Linux-compatible.
- [ ] XOBS-4: Watchdog heartbeat — `EMBEDIQ_OBS_EVT_WDG_CHECKIN` (TIMING band
      0x50) added to `fb_watchdog`. Periodic heartbeat on successful checkin.
      Gated by `EMBEDIQ_TRACE_TIMING` compile-time flag (off by default on
      constrained profiles).

**Service FBs (platform-agnostic, run on Linux):**
- [ ] fb_cloud_mqtt — MQTT 3.1.1 over TCP/IP. Connect, publish, subscribe,
      reconnect with exponential backoff. Ops table pattern (`embediq_mqtt_ops_t`).
      Linux host backend. Full test suite.
- [ ] fb_telemetry — periodic metric reporting over MQTT or local log. No hardware
      dependency. Configurable interval, metric selection, payload schema.
- [ ] fb_provisioning — device identity and configuration provisioning flow.
      Linux-backed for development. Ops table pattern.
- [ ] fb_ota (logic + Linux mock) — OTA FSM: download, signature verification hook,
      state machine (IDLE → DOWNLOADING → VERIFYING → APPLYING → CONFIRMED/ROLLBACK).
      Linux file-based mock backend. Real dual-bank flash write is Phase 3.

**Test infrastructure:**
- [ ] Test harness — `embediq_test.h`, `bus_inject()`, scenario runner.
      Allows deterministic replay of message sequences in unit tests.

**Schema and registry:**
- [x] `messages_registry.json` — initial namespace allocations for all official FBs
      (fb_cloud_mqtt, fb_telemetry, fb_provisioning, fb_ota, fb_uart, fb_i2c,
      fb_spi, fb_timer, fb_watchdog, fb_nvm, fb_bridge, fb_logger, thermostat,
      gateway). All official ranges allocated.
- [ ] `messages.iq` runtime integration — schema_id verified at runtime in debug
      builds. Generator emits validation table alongside typed structs.

**Tooling:**
- [ ] `embediq` project-management CLI — `init`, `add`, `build` sub-commands.
      (`embediq obs` sub-commands already shipped in `tools/embediq_obs/`.)
- [ ] EmbedIQ Registry v0 — package manifest format, local registry,
      `embediq add fb_cloud_mqtt` installs and registers an FB.
- [ ] AI-08: AI_CODER_SESSION TLV CI plugin — CI script that detects AI-authored
      commits to safety-classified FBs and enforces the AI Code Review Gate.
      TLV format (`0x0008`) and gate process defined in
      `docs/architecture/AI_FIRST_ARCHITECTURE.md §4`. Implementation: CI script
      in `tools/ci/`, wired into pipeline alongside existing enforcement scripts.

**Bridge and ecosystem (Linux):**
- [ ] Bridge daemon — External FB support. Python, Node.js External FBs
      communicating with Native FBs over the bridge protocol.
- [ ] `embediq-python` client SDK — Python External FB base class, message
      send/receive, Observatory stream consumer.

**Examples:**
- [x] Industrial sensor gateway example — `examples/gateway/` complete: three
      application FBs (`fb_field_receiver`, `fb_policy_engine`, `fb_north_bridge`)
      forming a full edge-to-cloud pipeline. Runs on Linux, simulated sensor stream.
      No hardware required.

**Documentation:**
- [ ] Mbed OS migration guide — complete and published.
- [ ] "How to instrument" contributor playbook — 5 canonical patterns:
      FB fault, OSAL timeout, HAL peripheral error, library init, ISR event.
      Annotated code examples. (E1-i from expert review.)
- [ ] Observability failure modes specification — init failure behavior, slot
      exhaustion policy, observer task death recovery. Required before XOBS-1
      ships to production. (E1-h from expert review.)
- [ ] Security and privacy posture document — formal statement of what event
      fields must not contain (credentials, keys, PII). Required before public
      SDK release. (E1-g from expert review.)

**Success criterion:** Smart thermostat + fb_cloud_mqtt publishing to a broker +
fb_telemetry reporting, all running on Linux. Bridge daemon with a Python External
FB communicating with native FBs. Observatory capturing everything. 100% of
deliverables verified with zero hardware.

---

## Phase 3 — Hardware Targets

**Goal:** EmbedIQ runs on real hardware. FreeRTOS bare-metal target confirmed.
Same source code as Phase 2 — only the BSP and OSAL change.

**RTOS abstraction:**
- [ ] FreeRTOS OSAL — `osal/freertos/embediq_osal_freertos.c`. Full OSAL contract
      implemented for FreeRTOS. Must pass `check_osal_obs.py` from day one.
      Pattern: mirror `osal/posix/`.

**Cross-layer observability — hardware side:**
- [ ] XOBS-2 (ESP32/STM32 side): `hal/esp32/` implementations call
      `HAL_OBS_EMIT_ERROR` on all non-HAL_OK returns. Must pass `check_hal_obs.py`.
- [ ] XOBS-3 (FreeRTOS side): `EMBEDIQ_OBS_EVT_OSAL_STACK_HIGH` (RESOURCE band
      0x42) — stack high-water mark per FB thread via
      `uxTaskGetStackHighWaterMark()`. Emitted at session end.
- [ ] XOBS-5: Boot phase per-peripheral init ordering — one Observatory event per
      HAL peripheral initialised during PLATFORM_INIT. Causal chain from power-on
      to running.

**Driver FBs on hardware:**
- [ ] fb_uart — two-zone ISR model: byte receipt in ISR, ring buffer, thread
      delivery. UART loopback confirmed on hardware target.
- [ ] fb_i2c — I2C master. Read/write transactions. Hardware confirmed.
- [ ] fb_spi — SPI master. Hardware confirmed.
- [ ] fb_watchdog on FreeRTOS — hardware TWDT backend.
- [ ] fb_nvm on FreeRTOS — ESP-IDF NVS or STM32 Flash backend.

**Service FBs on hardware:**
- [ ] fb_ota (hardware) — real dual-bank atomic flash write. Signature verification
      with ECDSA-P256. Rollback on boot failure. Hardware confirmed.

**Signing infrastructure (TRACE_SIGNATURE TLV — `0x0009`):**
TLV slot and `signature_ref` hook in `COMPLIANCE_EVENT` are already reserved
(see `COMPLIANCE.md §8` and `docs/observability/iqtrace_format.md §5.7`).
No format changes needed when these items ship.
- [ ] P2-SIGN-01: Device key provisioning specification — formal document covering
      key lifecycle, provisioning flow, and device identity anchoring.
- [ ] P2-SIGN-02: HSM integration contract — `hal_hsm.h` HAL ops table for hardware
      security module. Pattern mirrors `hal_tls.h`.
- [ ] P2-SIGN-03: ECDSA-P256 signing implementation — `hal/posix/` software signing
      backend for development and CI. Real HSM backend is P2-SIGN-02.
- [ ] P2-SIGN-04: Key management guide — rotation, revocation, and factory re-key
      procedures for production deployments.
- [ ] P2-SIGN-05: Offline signature verification in `embediq obs` CLI — verify a
      `.iqtrace` file against a known device public key without hardware.

**Examples:**
- [ ] Smart thermostat on ESP32 or STM32 — same source as Linux demo.
      Only BSP changes.
- [ ] Industrial gateway with real Modbus RTU over serial.

**Success criterion:** Smart thermostat running on ESP32 or STM32 with identical
source code to the Linux build. Only the BSP changes. Hardware UART loopback
passes. OTA update applied and confirmed on device.

---

## Phase 4 — Ecosystem and Second RTOS

**Goal:** Zephyr OSAL. Community extension framework. AI event band.

**Second RTOS:**
- [ ] Zephyr OSAL — `osal/zephyr/`. Same contract as POSIX and FreeRTOS.
      Target: Nordic nRF52840, NXP i.MX RT, or ST STM32WB.
      Same thermostat example on Zephyr target.

**Community extension framework:**
- [ ] `vendor_event_registry.json` — central self-assignment registry for vendor
      sub-bands (0x90–0xFF) to prevent collisions at ecosystem scale. Required
      before first external plugin is published. (D3 from expert review.)
- [ ] `events.iq` — machine-readable event schema parallel to `messages.iq`.
      Enables AI coding agents and Studio-compatible decoders to understand
      any event without hardcoded knowledge. (F1 from expert review.)
- [ ] Canonical event schema contract — formal document specifying field meanings,
      legal values, and decoder behavior. (E1-a from expert review.)
- [ ] Compatibility and evolution rules — which changes to event constants are
      breaking vs. additive; semver implications. (E1-b from expert review.)
- [ ] Vendor extension decoder minimum spec — minimum fields and registration for
      a compliant vendor event decoder. (E1-j from expert review.)
- [ ] Observatory Studio CLI preview — terminal-based session viewer. Live FB
      swimlane view, FSM state display, message trace. Apache 2.0.

**AI event family band (gated):**
- [ ] Phase-4 AI band specification document — replaces illustrative names in
      `docs/architecture/AI_FIRST_ARCHITECTURE.md §5.2` with production-validated
      taxonomy. Constants in 0x80–0x8F range.
- [ ] AI policy layer — runtime policy engine ALLOW/BLOCK based on `safety_class`
      and Observatory state.
- [ ] Fleet analytics reference — `.iqtrace` stream join across N devices using
      `device_id`/`session_id` correlation keys.

**Gate criteria for AI band (must be met before AI band work begins):**
- ≥2 production deployments using Phase-1 AI constants (0x17–0x1A) for ≥6 months
- Phase-4 specification reviewed and accepted via the EmbedIQ decision process
- Feedback from production deployments incorporated into the taxonomy draft

**Success criterion:** External FB (Python) communicating with Native FBs via
Bridge on Zephyr target. Vendor plugin registers in the community event band
without collision. AI band constants finalised from production data.

---

## Completed

| What | When |
|------|------|
| Phase 0: Foundations | March 2026 |
| Phase 1: Core Engine + thermostat demo | March 2026 |
| Phase 1: HAL refactor (PRs #24–#29) | March 2026 |
| Phase 1: Observability track (Obs-0 through Obs-6) | March 2026 |
| Phase 1: Library Architecture (PRs #79–#87) | April 2026 |
| Phase 2: `messages_registry.json` — all official FB namespaces allocated | April 2026 |
| Phase 2: Industrial gateway example (`examples/gateway/`) | April 2026 |
| v0.1.2 tagged on main | March 2026 |

---

## What is not on this roadmap

Real limitations, not omissions. See [ARCHITECTURE.md](ARCHITECTURE.md) non-goals.

- Hard real-time scheduling guarantees — the RTOS provides these, not EmbedIQ
- Safety certifications (IEC 61508, ISO 26262) — EmbedIQ supports certified
  products but does not carry certification itself
- Ultra-constrained targets (< 64KB flash) — not v1 scope
- Worker pool executor — non-goal
- Bridge authentication — Phase 4 non-goal
- DMA controller internal state — below HAL abstraction, vendor BSP territory
- Context switch timing — below OSAL contract, inside RTOS scheduler
- Interrupt latency — hardware + RTOS, below our abstraction

---

## Future work (declared, not scheduled)

Capabilities the architecture is designed to support. Implementation is Phase 4+.

- **Observatory filter rules** — subscribe to events from a specific FB only.
  Architecture does not prevent this; v1 captures all events.
- **Runtime `EMBEDIQ_TRACE_LEVEL` switching** — compile-time only in v1.
  Dynamic switching is future work.
- **Zero-copy message pool** — copy-by-value in v1. Pool is a future non-goal.
- **fb_power_manager** — FB suspend/resume lifecycle hooks exist in v1. Power
  policy manager is future work.
- **`messages.iq` static size assertions** — generator emits `static_assert`
  for every schema. Phase 1 foundation is in place.
- **Resource diagnostics as Observatory events** — FB CPU slice, task switch
  latency, per-queue throughput. Automated reporting is future work.
- **Overflow semantics hardening** — per-ring vs. global drop count, OVERFLOW
  event collapse behavior, audit gap guarantees. Needed before XOBS-3
  ships to production. (E1-c from expert review.)
- **Resource identity stability** — behavior when an OSAL resource is destroyed
  and recreated (instance index reuse, event continuity). Needed before XOBS-1
  ships to production. (E1-e from expert review.)
- **Sampling policy for high-frequency events** — required before XOBS-3
  (stack high-water) and message latency events. (E1-f from expert review.)
- **TOPOLOGY TLV in `.iqtrace`** — a future TLV type carrying the full FB
  registry snapshot (names, safety classes, message subscriptions, boot phases)
  at session start. Enables any decoder — CLI, community tools — to reconstruct
  the FB topology without the build system. Architecture does not prevent this;
  SESSION TLV carries firmware version today and is the natural anchor for a
  topology block.

---

## How to influence the roadmap

Open a GitHub Discussion with the `roadmap` label. Describe your use case and
what you need. Community demand directly influences phase priorities.

---

*Apache 2.0 · embediq.com*
