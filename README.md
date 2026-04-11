# EmbedIQ

**The AI-first application framework for embedded and edge.**

Apache 2.0 · AI-first · RTOS-agnostic · Linux gateway · Host simulation first · Zero-instrumentation observability · Open .iqtrace format

---

## The problem

Every RTOS gives you threads, queues, and semaphores. Every Linux process gives you sockets and file descriptors. Nothing above that — not for firmware, not for Linux gateways. Every team reinvents the same infrastructure — OTA, cloud connectivity, watchdog management, structured state — on every project, under delivery pressure, differently each time.

The result is firmware that only its original author can safely touch, field failures debugged with printf, and AI tools that generate inconsistent code faster.

The problem is structural. The solution has to be structural too.

---

## What EmbedIQ is

A message-driven, layered application framework that sits above your substrate — RTOS, bare-metal, or Linux — and enforces structure at the architecture level, not through code reviews or guidelines.

The structural fix: every firmware concern — a UART driver, an OTA updater, a cloud publisher — is isolated into an independent unit. That unit owns its state privately. It communicates with every other unit only through typed messages on a shared bus. It never calls another unit directly. No shared state across units. No hidden dependencies. The wrong patterns cannot compile.

**That unit is the Functional Block (FB).** An FB owns its state exclusively. It communicates only via typed messages. It is observable by default — every lifecycle transition and message boundary is captured automatically with zero developer code. The wrong patterns — direct cross-FB calls, hardware headers in application code, dynamic allocation in the core engine — are structurally prevented. They fail CI, not code review.

**Four layers. Clear contracts between them.**

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
│  Layer 2 · Driver FBs (Apache 2.0)                                           │
│  fb_uart · fb_timer · fb_gpio · fb_watchdog · fb_nvm                         │
│  Pro FBs (commercial): fb_ota · fb_telemetry · fb_cloud_mqtt → embediq.com/pro │
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

All tests pass on Linux and macOS without hardware. The thermostat example runs a full FB application — sensor, controller, cloud publish — with Observatory output on your terminal.

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

## What exists today

**Phase 1 — complete.** Core engine, message bus, FSM engine, full observability system, POSIX OSAL, and the thermostat demo run clean. All contracts frozen. `ctest` passes 100%.

**HAL refactor — complete.** All three Phase 1 Driver FBs (`fb_timer`, `fb_nvm`, `fb_watchdog`) live in `fbs/drivers/` as portable, POSIX-independent source files. POSIX implementations live in `hal/posix/`. `platform/posix/` is retired. The pattern is established for every future hardware target.

**Observability system — complete.** The Observatory is not a printf replacement — it is a structured, machine-readable record of exactly what the firmware did. Every state transition, every message boundary, every fault is automatically captured with zero developer code. The full system includes:

- 7-family event taxonomy (SYSTEM, MESSAGE, STATE, RESOURCE, TIMING, FAULT, FUNCTION) encoded as bands in the event type byte — zero extra runtime overhead, family derived in the decoder
- Compile-time `EMBEDIQ_TRACE_LEVEL` (0–3) with per-family zero-overhead emit macros — events compile to `(void)0` on constrained MCU builds
- `EmbedIQ_Obs_Session_t` — 40-byte session identity record (device ID, firmware version, build ID, platform, trace level) prepended to every capture
- `.iqtrace` open binary format — TLV-framed, little-endian, forward-compatible, fully specified in `docs/observability/iqtrace_format.md` (Apache 2.0)
- `tools/embediq_obs/` CLI — `embediq obs decode / stats / filter / export` — read any `.iqtrace` file from a laptop, no Studio required

**Phase 2 — active.** FreeRTOS OSAL, ESP32 target, hardware Driver FBs. Pro FBs (fb_ota, fb_telemetry, fb_cloud_mqtt) available at [embediq.com/pro](https://embediq.com/pro). See [ROADMAP.md](ROADMAP.md).

See [ROADMAP.md](ROADMAP.md) for the full timeline.

---

## Why the architecture matters for AI

AI generates reliable React because the framework is well-described — every component, prop, and hook has a type. Embedded firmware has always been the opposite: globals, callbacks, platform ifdefs, no structure an agent can reason about.

EmbedIQ makes every FB, message schema, and FSM transition a typed, machine-readable descriptor. The boundary checker tells an AI agent exactly what it can and cannot include. The contracts tell it exactly what the interfaces are. The Observatory tells it exactly what happened at runtime.

This is not a marketing claim. It is a structural consequence of the architecture.

**AI observability — built in, not bolted on.** EmbedIQ ships four event type constants for AI inference (EU AI Act Art.12 logging: `AI_INFERENCE_START` / `AI_INFERENCE_END`; EU AI Act Art.13 explainability: `AI_CONFIDENCE_THRESHOLD`). Every AI inference event lands in the same tamper-evident 14-byte ring buffer as every fault event, every boot event, and every message bus event. There is no separate AI logging pipeline — the Observatory is the logging pipeline.

**`.iqtrace` as AI training data moat.** A device fleet running EmbedIQ accumulates a proprietary, tamper-evident dataset of edge AI behaviour on real hardware with real workloads. The 14-byte fixed record size means traces from any device are structurally identical — trivially mergeable into a cross-device training corpus. This dataset is structurally private: the `.iqtrace` never leaves the device unless the application operator explicitly exports it. No cloud provider has access to it. The longer the fleet runs, the deeper the dataset. See `docs/architecture/AI_FIRST_ARCHITECTURE.md §6`.

**AI Code Review Gate.** When an AI coding assistant modifies a safety-classified Functional Block, EmbedIQ's AI Code Review Gate requires a qualified human to review the change before it can be merged. The gate outcome is recorded in the `AI_CODER_SESSION` TLV — a machine-readable provenance record in every `.iqtrace` session. See `AGENTS.md §14` and `docs/architecture/AI_FIRST_ARCHITECTURE.md §4`.

---

## Why Apache 2.0

Use EmbedIQ in closed-source commercial products at zero cost. No source disclosure. No license fee.

QP/C — the closest prior art, 60,000+ downloads per year for 19 years — ships under GPLv3. Commercial use requires either full source disclosure or a paid commercial license. EmbedIQ removes that constraint structurally.

The `.iqtrace` binary format and `embediq obs` CLI are Apache 2.0 forever — the data your firmware produces is permanently open. EmbedIQ Studio (future commercial) adds visual analysis on top; it never owns or restricts the underlying event data.

The core framework, base Driver FBs, and infrastructure FBs are Apache 2.0
forever. Production accelerators (fb_ota, fb_telemetry, fb_cloud_mqtt) are
available as EmbedIQ Pro with commercial licensing. See
[COMMERCIAL_BOUNDARY.md](COMMERCIAL_BOUNDARY.md) for the exact line.

---

## Who it is for

Embedded engineers and IoT leads who have shipped production RTOS firmware and rebuilt the same infrastructure one too many times. Specifically:

- Teams building connected products in C on FreeRTOS, Zephyr, or Linux who want reusable, testable FBs instead of project-specific glue
- Developers who cannot run firmware tests today without hardware — and need that to change for CI to be real
- Mbed OS users who need a new architecture home before ARM archives it in July 2026
- Engineers building on RISC-V (SHAKTI, VEGA) — EmbedIQ runs on RISC-V without modification

**India is a priority geography.** EmbedIQ is designed for the missing middle of the Indian embedded market: systems with enough compute to run inference and connectivity, but not enough to run full cloud agent stacks. India-specific alignment: AIS-190 (automotive, aligned ISO 26262 ASIL A/B), CDSCO/MDR 2017 (medical devices, aligned IEC 62304 Class A/B), TEC IoT security guidelines, and IndiaAI Mission DPI objectives. The open `.iqtrace` format and zero-cost Apache 2.0 license remove vendor lock-in for PLI-recipient electronics manufacturers. See `COMPLIANCE.md` and `ARCHITECTURE.md §India Market`.

---

## Documentation

| Document                                                | Purpose                                                    |
| ------------------------------------------------------- | ---------------------------------------------------------- |
| [ARCHITECTURE.md](ARCHITECTURE.md)                      | Complete technical reference — Layer Model, contracts, HAL |
| [AGENTS.md](AGENTS.md)                                  | Read before generating any EmbedIQ code with an AI agent   |
| [CONTRIBUTING.md](CONTRIBUTING.md)                      | How to contribute — boundaries, process, CLA               |
| [CODING_RULES.md](CODING_RULES.md)                      | Rules enforced on every PR                                 |
| [ROADMAP.md](ROADMAP.md)                                | Phase 1→4 public roadmap                                   |
| [COMMERCIAL_BOUNDARY.md](COMMERCIAL_BOUNDARY.md)        | What is free forever vs future commercial                  |
| [docs/observability/iqtrace_format.md](docs/observability/iqtrace_format.md) | Open `.iqtrace` binary format specification v1.1 |
| [docs/architecture/AI_FIRST_ARCHITECTURE.md](docs/architecture/AI_FIRST_ARCHITECTURE.md) | AI-first architecture: constants, Code Review Gate, training data moat |
| [docs/MIGRATION.md](docs/MIGRATION.md)                 | Four migration patterns: Greenfield, Add-Observatory, Strangler Fig, Module-Only |
| [COMPLIANCE.md](COMPLIANCE.md)                          | Industry coverage table, MISRA stance, tamper evidence tiers, SBOM formats |

---

## Get involved

The engine is running. Phase 2 is active. GitHub is where the work happens.

Star the repo if you care about the problem. Open a Discussion if you have a hardware target or Driver FB you need. File an issue if something breaks. Read [CONTRIBUTING.md](CONTRIBUTING.md) if you want to contribute code — Phase 2 needs engineers who have shipped production RTOS firmware.

---

*Apache 2.0 · [embediq.com](https://embediq.com) · Built by [Ritesh Anand](https://www.linkedin.com/in/riteshanand101) · [Ritzy Lab](https://ritzylab.com)*
