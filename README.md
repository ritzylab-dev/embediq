# EmbedIQ

**The application framework above the RTOS that nobody has built yet.**

Apache 2.0 · RTOS-agnostic · Host simulation first-class · Zero-instrumentation observability

---

## The problem

FreeRTOS, Zephyr, and every other RTOS give you threads, queues, and semaphores. They give you nothing above that. Every team reinvents the same infrastructure — cloud connectivity, OTA updates, structured state management, observability — on every project. Every firmware codebase looks different. Every field failure requires a printf that changes timing and makes the bug disappear.

The one framework that comes closest (QP/C) ships under GPLv3. Commercial firmware must disclose source or buy a paid license.

EmbedIQ fills the gap. [See the full problem statement →](docs/PROBLEM_STATEMENT.md)

---

## What EmbedIQ is

A structured, message-driven application framework that sits above your RTOS and gives you:

- **Functional Blocks (FBs)** — the unit of everything. Owns its state. Communicates only via typed messages. Observable by default.
- **Message Bus** — all cross-FB communication through one typed, priority-queued channel. No direct calls between FBs.
- **Table-driven FSM engine** — every state, every event, every transition declared explicitly. Readable from the table alone.
- **Zero-instrumentation Observatory** — every FB state transition and message captured automatically. No printf. No Percepio.
- **Host simulation first-class** — run and test complete firmware on Mac or Linux. No hardware required for development or CI.
- **Reusable component library** — `fb_cloud_mqtt`, `fb_ota`, `fb_watchdog`, `fb_nvm`, `fb_telemetry`. Import and use.
- **RTOS-agnostic OSAL** — same application code on FreeRTOS, Pi/Linux, Zephyr, and RISC-V targets.

**The wrong patterns are structurally prevented.** Cross-FB coupling fails CI rule R-01. Hardware headers in application code fail compilation (Core headers compile standalone). Dynamic allocation in Shell 1 fails binary analysis. Structural discipline is enforced by the architecture, not hoped for.

---

## Architecture at a glance

```
┌─────────────────────────────────────────────────────────────────────┐
│  TOOLS                                                              │
│  messages.iq generator  ·  embediq CLI  ·  EmbedIQ Studio           │
├─────────────────────────────────────────────────────────────────────┤
│  COMPONENT FBs                            (all Apache 2.0)          │
│  fb_cloud_mqtt  ·  fb_ota  ·  fb_telemetry  ·  fb_provisioning      │
│                  ·  your_fb                                         │
│                  ▼  messages only  ▼                                │
├─────────────────────────────────────────────────────────────────────┤
│  PLATFORM FBs                                                       │
│  fb_uart  ·  fb_watchdog  ·  fb_nvm  ·  fb_timer  ·  fb_gpio        │
│                  ▼  subscriptions · publications  ▼                 │
├─────────────────────────────────────────────────────────────────────┤
│  CORE ENGINE                                                        │
│  FB Engine  ·  Message Bus (3-queue)  ·  FSM Engine  ·  Observatory │
├─────────────────────────────────────────────────────────────────────┤
│  OSAL  ──────────  EmbedIQ above · your stack below  ──────────     │
├─────────────────────────────────────────────────────────────────────┤
│  RTOS / OS                                                          │
│  FreeRTOS  ·  Pi/Linux POSIX  ·  Zephyr  ·  RISC-V (SHAKTI/VEGA)    │
├─────────────────────────────────────────────────────────────────────┤
│  HARDWARE                                                           │
│  RP2040  ·  STM32  ·  Pi/Linux SBC  ·  ESP32  ·  any target         │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Get started in 5 minutes

**Prerequisites:** CMake 3.18+, a C11 compiler, Git.

```bash
git clone https://github.com/embediq/embediq.git
cd embediq
cmake -B build -DEMBEDIQ_PLATFORM=host
cmake --build build
ctest --test-dir build
```

The host platform runs without hardware. Every test exercises real framework code. The smart thermostat example demonstrates a complete FB application with Observable output on your terminal.

```bash
./build/examples/thermostat/embediq_thermostat
# Observatory output: every message, every FSM transition, timestamped
```

---

## Why Apache 2.0 matters

Use EmbedIQ in closed-source commercial products at zero cost. No source disclosure requirement. No license fee. This is a structural advantage over QP/C (GPLv3 — your entire firmware must be open source or you pay for a commercial license).

See [COMMERCIAL_BOUNDARY.md](COMMERCIAL_BOUNDARY.md) for the full commitment — what is Apache 2.0 forever and what is commercial.

---

## Who it is for

- Embedded and IoT engineers building connected products in C/C++ on RTOS or Linux
- Teams tired of reinventing cloud connectivity, OTA, and watchdog management on every project
- Developers who cannot test firmware without hardware today and want that to change
- The 60,000+ Mbed OS developers who need a home after ARM archives it in July 2026
- Engineers building on India's indigenous RISC-V processors (SHAKTI, VEGA) — EmbedIQ runs on RISC-V without modification

---

## Documentation

| Document | Purpose |
|----------|---------|
| [ARCHITECTURE.md](ARCHITECTURE.md) | Complete technical reference — read before writing any code |
| [CONTRIBUTING.md](CONTRIBUTING.md) | How to contribute — five-layer process, boundaries, CLA |
| [ROADMAP.md](ROADMAP.md) | Phase 0→3 public roadmap |
| [COMMERCIAL_BOUNDARY.md](COMMERCIAL_BOUNDARY.md) | What is free forever vs commercial |
| [AGENTS.md](AGENTS.md) | AI agent primer — read before generating any EmbedIQ code |
| [CODING_RULES.md](CODING_RULES.md) | Coding rules — verify before every PR |

---

## Project status

**Phase 0 — active.** Repo scaffold, all header contracts, CMake structure, AI agent framework.

Phase 1 (Core Engine + Pi/Linux BSP + Observatory) begins when Phase 0 is complete.

See [ROADMAP.md](ROADMAP.md) for the full timeline.

---

## Built by

[Ritesh Anand](https://www.linkedin.com/in/riteshanand101) · [Ritzy Lab](https://ritzylab.com) · embediq.com

20 years shipping embedded and IoT products. EmbedIQ is the framework I needed on every one of them.

---

*Apache 2.0 · embediq.com · © 2026 Ritzy Lab*
