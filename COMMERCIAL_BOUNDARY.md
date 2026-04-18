# COMMERCIAL_BOUNDARY.md

**What is Apache 2.0 forever. What is commercial. Said once, said clearly.**

This document defines the commercial boundary before the community grows, because changing it later damages trust permanently. See HashiCorp and Redis for what happens when this is left ambiguous.

---

## What is Apache 2.0 forever

No exceptions. No future owner can change this.

- All of `core/` — FB engine, message bus, FSM engine, Observatory runtime,
  OSAL contracts
- All OSAL implementations — FreeRTOS, Pi/Linux POSIX, Zephyr, bare-metal
- Base Driver FBs — `fb_uart`, `fb_i2c`, `fb_spi`, `fb_timer`, `fb_gpio`
- Infrastructure FBs — `fb_watchdog` (basic watchdog), `fb_nvm` (basic
  persistent storage with integrity check). These keep your device running
  correctly and are free unconditionally.
- All tools — `messages.iq` generator, `embediq` CLI, FB registry protocol
- The `.iqtrace` binary file format — the open specification
  (`docs/observability/iqtrace_format.md`) is Apache 2.0 forever. Any
  decoder, any tool, any commercial product may read `.iqtrace` files
  without restriction.
- `tools/embediq_obs/` — the Observatory CLI (`decode`, `stats`, `filter`,
  `export`, `tail`) is Apache 2.0 forever. The data your firmware produces
  is permanently open.
- `fb_telemetry` — the metric collection Service FB (`fbs/services/fb_telemetry.c`).
  Provides OTel-aligned gauge, counter, and histogram reporting. Data your firmware
  produces is permanently open. A cloud transport layer (`fb_cloud_mqtt`) may extend
  it commercially — the collection layer is free unconditionally.
- `embediq_cfg` — typed configuration helper (`core/include/embediq_cfg.h` +
  `core/src/cfg/embediq_cfg.c`). Typed get/set wrapper over `fb_nvm`.
  A device that cannot read its own configuration is not operating correctly.
  Same rationale as `fb_watchdog` and `fb_nvm`.
- All examples — thermostat, gateway, and all future examples in this repo

**You can use EmbedIQ in a closed-source commercial product at zero cost, forever.**

This is the answer to QP/C's GPLv3: Apache 2.0 means your firmware source code is never at risk.

---

## Service FBs — production accelerators (Apache 2.0)

All Service FBs are Apache 2.0. Production-grade cloud connectivity, OTA,
provisioning, and diagnostics are open source and free unconditionally.

| FB                  | What it does                                                |
|---------------------|-------------------------------------------------------------|
| `fb_cloud_mqtt`     | Production MQTT — auto-reconnect, QoS, AWS/Azure/GCP        |
| `fb_ota`            | OTA with rollback, A/B partitions, integrity verification   |
| `fb_provisioning`   | Factory, field, and cloud device provisioning               |
| `fb_secure_boot`    | Verified boot chain, HSM, firmware signing, anti-rollback   |
| `fb_fleet_diag`     | Fleet diagnostics, remote debug, device health              |
| `fb_compliance_log` | EU AI Act compliant logging, tamper-evident audit trails    |

These FBs plug into the framework via the standard FB interface. The framework
does not require any of them to function. See [ROADMAP.md](ROADMAP.md) for
current development status of each.

---

## EmbedIQ Studio (commercial)

Visual debugging and session replay built on top of the free core. Turns the
Observatory event stream into a visual timeline. Desktop/web application.
Reads `.iqtrace` files — it does not own them. The framework never requires it.

---

## Why this boundary is correct

Embedded developers do not pay for frameworks. They pay for tools and managed infrastructure. FreeRTOS is free. Zephyr is free. EmbedIQ core is free.

The commercial products sit above the free core. They provide value on top of it — they do not restrict access to it. This is how Grafana, InfluxDB, and MongoDB (before the SSPL mistake) work. The community grows because the core is genuinely free. The business is funded by the developers and companies who need the tooling.

---

## The CLA requirement

Contributors must sign a Contributor License Agreement (CLA) before their first PR is merged.

The CLA gives Ritzy Lab the right to use your contribution in future commercial tools (Studio and Layer 4 tooling). It does not change your rights to your own code. It does not make the core less open.

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
The framework itself does not carry a safety certification. Apache 2.0 permits use in safety-critical products. Certification of your product is your responsibility. EmbedIQ's architecture (deterministic init, no dynamic allocation in Layer 1, observable state) is designed to support — not prevent — safety certification work.

**Will the core ever change to a non-Apache license?**
No. This is a permanent, irrevocable commitment made in writing before the first external contributor joined the project.

---

*Apache 2.0 core · embediq.com · © 2026 Ritzy Lab*
