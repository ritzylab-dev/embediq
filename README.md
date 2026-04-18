# EmbedIQ

**The AI-first application framework for embedded and edge.**

Apache 2.0 · RTOS-agnostic · Linux gateway · Host simulation first · Zero-instrumentation observability · Open .iqtrace format

---

## The problem

Every RTOS gives you threads, queues, and semaphores. Every Linux process gives you sockets and file descriptors. Nothing above that — not for firmware, not for Linux gateways. Every team reinvents the same infrastructure — OTA, cloud connectivity, watchdog management, structured state — on every project, under delivery pressure, differently each time.

The result is firmware that only its original author can safely touch, field failures debugged with printf, and AI tools that generate inconsistent code faster.

The problem is structural. The solution has to be structural too.

---

## Run it now

**Prerequisites:** CMake 3.18+, C11 compiler, Git.

```bash
git clone https://github.com/ritzylab-dev/embediq.git
cd embediq
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DEMBEDIQ_PLATFORM=host
cmake --build build -j$(nproc)
cd build && ctest --output-on-failure
```

Generated C headers (`generated/*.h`) are committed — no extra tooling needed after clone. If you modify a `.iq` message schema, see [CONTRIBUTING.md](CONTRIBUTING.md) for the two-step workflow.

All tests pass on Linux and macOS without hardware. The thermostat example runs a full FB application — sensor, controller, HVAC actuator — with Observatory output on your terminal.

```bash
./build/examples/thermostat/embediq_thermostat
```

No printf. No instrumentation. Every message and FSM transition captured automatically by the Observatory. Write a `.iqtrace` session file and analyse it from your laptop:

```bash
EMBEDIQ_OBS_PATH=/tmp/thermostat.iqtrace ./build/examples/thermostat/embediq_thermostat
python3 tools/embediq_obs/embediq_obs.py obs stats /tmp/thermostat.iqtrace
```

```
File:         /tmp/thermostat.iqtrace
Device:       0x00000001
Firmware:     0.1.0  build=dev
Platform:     POSIX
Total events: 47
Seq range:    1 – 47

Events by family:
  FAULT          2
  MESSAGE       18
  STATE         12
  SYSTEM        15

Events by type:
  FAULT          2
  FSM_TRANS     12
  LIFECYCLE     15
  MSG_RX         9
  MSG_TX         9
```

The event record format, TLV framing, and session structure are open and specified in [`docs/observability/iqtrace_format.md`](docs/observability/iqtrace_format.md). The CLI is Apache 2.0. EmbedIQ Studio adds visual analysis on top — it never owns or restricts the underlying data.

---

## What you get

### Structure that eliminates a category of failure

Firmware debt is not a quality problem — it is a structural problem. When any function can call any other function, any thread can touch any variable, the result is firmware only the original author can safely touch. That is a structural problem. It requires a structural fix.

The fix: every firmware concern — a UART driver, an OTA updater, a cloud publisher — is isolated into an independent unit. That unit owns its state privately. It communicates with every other unit only through typed messages on a shared bus. It never calls another unit directly. No shared state across units. No hidden dependencies. The wrong patterns cannot compile.

**That unit is the Functional Block (FB).** When you design a product, you naturally draw boxes — "the sensor reader," "the OTA updater," "the cloud publisher." Each box has a job. Each box has state it owns. FBs are those boxes. The architecture enforces what the whiteboard already shows.

FBs can have state machines. When an FB's behaviour is non-trivial — a provisioning flow, an OTA sequence, a connection lifecycle — you attach a table-driven FSM to it. The FSM engine handles transitions, guards, and actions. Every transition is automatically captured by the Observatory with zero developer code.

The wrong patterns — direct cross-FB calls, hardware headers in application code, dynamic allocation in the core engine — are structurally prevented. They fail CI, not code review.

See [docs/FIRST_FB.md](docs/FIRST_FB.md) for a complete walkthrough. See [examples/thermostat/](examples/thermostat/) for a 5-FB application with a full FSM cycle and Observatory output.

### Observability that is also your compliance record

Most firmware observability is printf — useful for development, useless for compliance, absent in production. EmbedIQ's Observatory is different in kind, not degree.

Every state transition, every message boundary, every fault is captured as a structured 14-byte binary record in a tamper-evident ring buffer. No developer code. No instrumentation. The Observatory is inside the engine — it cannot be bypassed.

The `.iqtrace` binary format is open and specified (`docs/observability/iqtrace_format.md`). The CLI reads any trace file from a laptop with no Studio required.

For compliance, this matters concretely:

- **EU AI Act Art. 12 (logging):** `AI_INFERENCE_START` and `AI_INFERENCE_END` event constants are built into the Observatory event taxonomy. Every AI inference lands in the same tamper-evident record as every fault and boot event. One audit trail, one format.
- **EU AI Act Art. 13 (explainability):** `AI_CONFIDENCE_THRESHOLD` event type is available. When your model's confidence drops below a policy threshold, the event is recorded automatically.
- **IEC 62304 Class A/B, ISO 26262 ASIL A/B, AIS-190:** The Observatory's tamper-evident structure provides the machine-readable audit trail these standards require.

The `.iqtrace` data never leaves the device unless the application operator explicitly exports it. A fleet running EmbedIQ accumulates a proprietary dataset of edge AI behaviour on real hardware — structurally inaccessible to any third party.

See [COMPLIANCE.md](COMPLIANCE.md) for the full industry coverage table.

### Configuration as infrastructure

You should not write an NVM read/write abstraction on every project. `embediq_cfg` gives you typed configuration backed by `fb_nvm`. Declare your schema once. Read it anywhere. Persistence, defaults, and corruption recovery are handled.

The same `fb_nvm` source file runs on Linux (file-backed POSIX HAL) and on embedded targets (flash HAL) — same API, same behaviour, different backend. No code change between development and production.

See [docs/CONFIG.md](docs/CONFIG.md) for the complete walkthrough.

### The metrics pipeline is already built

`fb_telemetry` gives you gauge, counter, and histogram metrics on any FB. Declare a metric. Emit values. The telemetry FB batches, timestamps, and publishes. You do not wire a metrics pipeline — you declare what you want to measure.

See [docs/TELEMETRY.md](docs/TELEMETRY.md) for the complete walkthrough.

### Run on your laptop today, run on hardware without rewriting

Every FB that does not touch hardware is platform-agnostic by construction — it cannot include HAL headers, and the boundary checker enforces this in CI on every PR. The same source file runs on POSIX, FreeRTOS, and ESP32. The platform difference lives in `hal/posix/` or `hal/esp32/` — never in the FB.

This is not a porting story. FBs that are platform-agnostic run on any target without modification, because the architecture made that the only possible outcome.

---

## What exists today

**Phase 1 — complete.** Core engine, message bus, FSM engine, full observability system, POSIX OSAL, and the thermostat demo run clean. All contracts frozen. `ctest` passes 100%.

**Phase 2 — active.** Service FBs and tooling, Linux-first. No hardware required for any current work item.

Complete: `fb_telemetry` (OTel-aligned metric collection), `embediq_cfg` (typed configuration), industrial gateway example (6 FBs, edge pipeline, offline resilience), full cross-layer observability (OSAL + HAL fault hooks, queue depth events, watchdog heartbeat), test harness with bus injection and scenario replay.

Active: `fb_cloud_mqtt` (MQTT 3.1.1), `embediq` CLI, Factory Gateway Demo capstone.

See [ROADMAP.md](ROADMAP.md) for the full timeline.

---

## The architecture

```
┌────────────────────────────────────────────────────────────────────┐
│  Layer 4 · Commercial tools (future)                               │
│  Visual tooling · Cloud connectivity · IDE and AI integrations     │
├────────────────────────────────────────────────────────────────────┤
│  Layer 3 · Ecosystem                                               │
│  Bridge daemon · bridge/websocket · bridge/unix_socket             │
│  Registry · Community BSPs · 3rd-party FB wrappers                 │
│  External FBs (Python · Node.js · Java · any language)            │
├────────────────────────────────────────────────────────────────────┤
│  Layer 2 · Driver FBs + Service FBs (Apache 2.0)                   │
│  fb_uart · fb_timer · fb_gpio · fb_watchdog · fb_nvm               │
│  fb_telemetry · embediq_cfg                                        │
│  fb_cloud_mqtt · fb_ota · fb_provisioning (Phase 2/3)              │
│                 ▼  typed messages only  ▼                          │
├────────────────────────────────────────────────────────────────────┤
│  Layer 1 · Framework Engine                 ← running on POSIX     │
│  FB Engine · Message Bus · FSM Engine · Observatory                │
├────────────────────────────────────────────────────────────────────┤
│  CONTRACTS  (core/include/ — frozen at v1, CI-enforced)            │
├────────────────────────────────────────────────────────────────────┤
│  HAL  (core/include/hal/ contracts · hal/posix/ · hal/esp32/)      │
├────────────────────────────────────────────────────────────────────┤
│  OSAL  (osal/posix/ · osal/freertos/)                              │
├────────────────────────────────────────────────────────────────────┤
│  Substrate · FreeRTOS · Linux POSIX · Zephyr · bare-metal          │
└────────────────────────────────────────────────────────────────────┘
```

HAL (Hardware Abstraction Layer) abstracts hardware peripherals — UART, GPIO, flash. OSAL (OS Abstraction Layer) abstracts the RTOS — threads, queues, semaphores. These two layers are the portability seam: the same Driver FB source file runs on POSIX, FreeRTOS, and ESP32 because all platform differences live in `hal/posix/` or `hal/esp32/`, never in the FB.

**A Driver FB wraps one hardware peripheral via the HAL contract.** `fbs/drivers/fb_timer.c` is the same source file whether it runs on POSIX, FreeRTOS, or ESP32. The platform difference lives in `hal/posix/` or `hal/esp32/` — never in the FB.

**A Service FB is platform-agnostic.** `fb_cloud_mqtt` does not know what chip it runs on. It cannot include HAL headers — the boundary checker enforces this in CI on every PR.

---

## Why the architecture matters for AI

AI generates reliable React because the framework is well-described — every component, prop, and hook has a type. Embedded firmware has always been the opposite: globals, callbacks, platform ifdefs, no structure an agent can reason about.

EmbedIQ makes every FB, message schema, and FSM transition a typed, machine-readable descriptor. The boundary checker tells an AI agent exactly what it can and cannot include. The contracts tell it exactly what the interfaces are. The Observatory tells it exactly what happened at runtime.

This is not a marketing claim. It is a structural consequence of the architecture.

**AI observability — built in, not bolted on.** EmbedIQ ships four event type constants for AI inference (EU AI Act Art.12 logging: `AI_INFERENCE_START` / `AI_INFERENCE_END`; EU AI Act Art.13 explainability: `AI_CONFIDENCE_THRESHOLD`). Every AI inference event lands in the same tamper-evident 14-byte ring buffer as every fault event, every boot event, and every message bus event. There is no separate AI logging pipeline — the Observatory is the logging pipeline.

**`.iqtrace` as AI training data moat.** A device fleet running EmbedIQ accumulates a proprietary, tamper-evident dataset of edge AI behaviour on real hardware with real workloads. The 14-byte fixed record size means traces from any device are structurally identical — trivially mergeable into a cross-device training corpus. This dataset is structurally private: the `.iqtrace` never leaves the device unless the application operator explicitly exports it. No cloud provider has access to it. The longer the fleet runs, the deeper the dataset. See `docs/architecture/AI_FIRST_ARCHITECTURE.md §6`.

**AI Code Review Gate.** When an AI coding assistant modifies a safety-classified Functional Block, EmbedIQ's AI Code Review Gate requires a qualified human to review the change before it can be merged. The gate outcome is recorded in the `AI_CODER_SESSION` TLV — a machine-readable provenance record in every `.iqtrace` session. See `AGENTS.md §14` and `docs/architecture/AI_FIRST_ARCHITECTURE.md §4`.

---

## Who it is for

Embedded engineers and IoT leads who have shipped production RTOS firmware and rebuilt the same infrastructure one too many times. Specifically:

- Teams building connected products in C on FreeRTOS, Zephyr, or Linux who want reusable, testable FBs instead of project-specific glue
- Developers who cannot run firmware tests today without hardware — and need that to change for CI to be real
- Mbed OS users who need a new architecture home before ARM archives it in July 2026
- Engineers building on RISC-V (SHAKTI, VEGA) — EmbedIQ runs on RISC-V without modification

**India is a priority geography.** EmbedIQ is designed for the missing middle of the Indian embedded market: systems with enough compute to run inference and connectivity, but not enough to run full cloud agent stacks. India-specific alignment: AIS-190 (automotive, aligned ISO 26262 ASIL A/B), CDSCO/MDR 2017 (medical devices, aligned IEC 62304 Class A/B), TEC IoT security guidelines, and IndiaAI Mission DPI objectives. The open `.iqtrace` format and zero-cost Apache 2.0 license remove vendor lock-in for PLI-recipient electronics manufacturers. See `COMPLIANCE.md` and `ARCHITECTURE.md §India Market`.

---

## Start here

| Step | Guide | What you will do |
|------|-------|-----------------|
| 1 | [Run it now](#run-it-now) | Clone, build, run tests in under 5 minutes |
| 2 | [docs/FIRST_FB.md](docs/FIRST_FB.md) | Write and register your first Functional Block |
| 3 | [docs/TELEMETRY.md](docs/TELEMETRY.md) | Add gauge, counter, and histogram metric reporting to your FB |
| 4 | [docs/CONFIG.md](docs/CONFIG.md) | Persist and read typed device configuration |
| 5 | [examples/thermostat/](examples/thermostat/) | Study a complete 5-FB application with FSM and Observatory output |
| 6 | [examples/gateway/](examples/gateway/) | Edge-to-cloud pipeline with offline resilience |

---

## Reference

| Document | Purpose |
|----------|---------|
| [ARCHITECTURE.md](ARCHITECTURE.md) | Complete technical reference — Layer Model, contracts, HAL/OSAL |
| [AGENTS.md](AGENTS.md) | Read before generating any EmbedIQ code with an AI agent |
| [CONTRIBUTING.md](CONTRIBUTING.md) | How to contribute — boundaries, process, CLA |
| [CODING_RULES.md](CODING_RULES.md) | Rules enforced on every PR |
| [ROADMAP.md](ROADMAP.md) | Phase 1→4 public roadmap |
| [COMMERCIAL_BOUNDARY.md](COMMERCIAL_BOUNDARY.md) | What is Apache 2.0 forever vs commercial tools |
| [docs/observability/iqtrace_format.md](docs/observability/iqtrace_format.md) | Open `.iqtrace` binary format spec v1.1 |
| [docs/architecture/AI_FIRST_ARCHITECTURE.md](docs/architecture/AI_FIRST_ARCHITECTURE.md) | AI-first architecture: event constants, Code Review Gate, training data moat |
| [docs/MIGRATION.md](docs/MIGRATION.md) | Four migration patterns: Greenfield, Add-Observatory, Strangler Fig, Module-Only |
| [COMPLIANCE.md](COMPLIANCE.md) | Industry coverage — MISRA, tamper evidence, SBOM, safety_class encoding |

---

## Why Apache 2.0

Use EmbedIQ in closed-source commercial products at zero cost. No source disclosure. No license fee.

QP/C — the closest prior art, 60,000+ downloads per year for 19 years — ships under GPLv3. Commercial use requires either full source disclosure or a paid commercial license. EmbedIQ removes that constraint structurally.

The `.iqtrace` binary format and `embediq obs` CLI are Apache 2.0 forever — the data your firmware produces is permanently open. EmbedIQ Studio (future commercial) adds visual analysis on top; it never owns or restricts the underlying event data.

The entire framework — core engine, all OSAL implementations, all Driver FBs, all Service FBs, all tools, all examples — is Apache 2.0. See [COMMERCIAL_BOUNDARY.md](COMMERCIAL_BOUNDARY.md) for the exact line.

---

## Get involved

The engine is running. Phase 2 is active. GitHub is where the work happens.

Star the repo if you care about the problem. Open a Discussion if you have a hardware target or Driver FB you need. File an issue if something breaks. Read [CONTRIBUTING.md](CONTRIBUTING.md) if you want to contribute code.

---

*Apache 2.0 · [embediq.com](https://embediq.com) · Built by [Ritesh Anand](https://www.linkedin.com/in/riteshanand101) · [Ritzy Lab](https://ritzylab.com)*
