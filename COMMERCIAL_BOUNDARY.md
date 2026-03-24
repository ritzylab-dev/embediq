# COMMERCIAL_BOUNDARY.md

**What is Apache 2.0 forever. What is commercial. Said once, said clearly.**

This document defines the commercial boundary before the community grows, because changing it later damages trust permanently. See HashiCorp and Redis for what happens when this is left ambiguous.

---

## What is Apache 2.0 forever

No exceptions. No future owner can change this.

- All of `core/` — FB engine, message bus, FSM engine, Observatory runtime, OSAL contracts
- All OSAL implementations — FreeRTOS, Pi/Linux POSIX, Zephyr, bare-metal, RISC-V
- All Platform FBs — `fb_uart`, `fb_watchdog`, `fb_nvm`, `fb_i2c`, `fb_spi`, `fb_timer`, `fb_gpio`
- All Component FBs — `fb_cloud_mqtt`, `fb_ota`, `fb_telemetry`, `fb_provisioning`
- All tools — `messages.iq` generator, `embediq` CLI, FB registry protocol
- The `.iqtrace` binary file format — the open specification
  (`docs/observability/iqtrace_format.md`) is Apache 2.0 forever. Any
  decoder, any tool, any commercial product may read `.iqtrace` files
  without restriction.
- `tools/embediq_obs/` — the Observatory CLI (`decode`, `stats`, `filter`,
  `export`, `tail`) is Apache 2.0 forever. The data your firmware produces
  is permanently open.
- All examples — smart thermostat, industrial gateway, and all future examples added to this repo

**You can use EmbedIQ in a closed-source commercial product at zero cost, forever.**

This is the answer to QP/C's GPLv3: Apache 2.0 means your firmware source code is never at risk.

---

## What is commercial

Two products built on top of the free core. The framework never requires either of them.

**EmbedIQ Studio** — visual debugging and session replay tool. Turns the Observatory event stream into a visual timeline: FBs as swimlanes, messages as arrows, FSM states as coloured blocks. Live view, session capture, replay, FSM visualiser. Desktop/web application. Studio reads `.iqtrace` files — it does not own them. The same files are readable by `embediq obs` and any tool built on the open format.

**EmbedIQ Cloud** — fleet observability and OTA management platform. Every device's Observatory stream, aggregated. Staged OTA rollouts. Anomaly detection. Remote session capture.

These are products. The framework runs without them. Developers who want them pay for them.

---

## Why this boundary is correct

Embedded developers do not pay for frameworks. They pay for tools and managed infrastructure. FreeRTOS is free. Zephyr is free. EmbedIQ core is free.

The commercial products sit above the free core. They provide value on top of it — they do not restrict access to it. This is how Grafana, InfluxDB, and MongoDB (before the SSPL mistake) work. The community grows because the core is genuinely free. The business is funded by the developers and companies who need the tooling.

---

## The CLA requirement

Contributors must sign a Contributor License Agreement (CLA) before their first PR is merged.

The CLA gives Ritzy Lab the right to use your contribution in future commercial products (Studio, Cloud). It does not change your rights to your own code. It does not make the core less open.

**Why a CLA:**
- Without a CLA, Ritzy Lab cannot include community contributions in Studio or Cloud without individual relicensing negotiation
- With a CLA, community contributions flow into both the open core and the commercial products
- The CLA is the mechanism that makes "community contributions benefit everyone" actually work

The CLA is the standard Apache Individual CLA. It is not exotic or unusual.

---

## Questions

**Can I fork EmbedIQ and build a competing product?**
Yes. Apache 2.0 permits this. You can fork, modify, and redistribute — including in commercial products. You must include the Apache 2.0 license notice.

**Can I use EmbedIQ in a safety-certified product (IEC 61508, ISO 26262)?**
The framework itself does not carry a safety certification. Apache 2.0 permits use in safety-critical products. Certification of your product is your responsibility. EmbedIQ's architecture (deterministic init, no dynamic allocation in Shell 1, observable state) is designed to support — not prevent — safety certification work.

**Will the core ever change to a non-Apache license?**
No. This is a permanent, irrevocable commitment made in writing before the first external contributor joined the project.

---

*Apache 2.0 · embediq.com · © 2026 Ritzy Lab*
