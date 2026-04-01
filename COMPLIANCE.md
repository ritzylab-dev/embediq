# EmbedIQ Compliance Reference

**Final Decision Set v2.0 — Decisions A through K**
**Author:** Ritesh Anand — embediq.com | ritzylab.com
**SPDX-License-Identifier:** Apache-2.0

---

## 1. What EmbedIQ Claims (and Does Not Claim)

EmbedIQ is a framework that **enables** compliance — it is not a certified product. No certification body has assessed EmbedIQ itself. What EmbedIQ provides is:

- Structural properties (frozen contracts, tamper-evident audit trail, fixed-layout records) that are directly useful as compliance evidence.
- Documentation of which standard requirements each framework feature is designed to satisfy.
- An honest account of what is supported, what is not, and what the boundaries are.

If you are building a system that requires certification under ISO 26262, IEC 61508, DO-178C, or any other standard listed below, EmbedIQ's role in that certification is as a component whose traceability, test evidence, and observable behaviour contribute to your safety case. The certification of your system is your responsibility.

---

## 2. Industry Coverage Table

This table reflects the requirements of the Final Decision Set v2.0 (Decision H corrections applied). Read the "Support Level" column carefully — it is conservative by design.

| Standard | Domain | EmbedIQ Support Level | Notes |
|---|---|---|---|
| ISO 26262 | Automotive (road vehicles) | ASIL A, ASIL B | ASIL C/D: not targeted. Requires additional AUTOSAR binding not in scope for v1. |
| IEC 61508 | Functional Safety (general) | SIL 1, SIL 2 | SIL 3/4: not targeted. SIL 3/4 requires formal verification toolchain beyond EmbedIQ's scope. |
| IEC 62443 | Industrial / OT Cybersecurity | SL 1, SL 2 | SL 3/4: not targeted. CSMS and zone/conduit architecture are application-level, not framework-level. |
| IEC 62304 | Medical Device Software | Class A, Class B | Class C: not targeted. See Migration Guide §3 (Strangler Fig restriction). |
| DO-178C | Airborne Software | DAL C, DAL D | DAL A/B: not targeted. Formal methods and modified condition/decision coverage (MC/DC) are not provided. |
| EN 50128 | Railway Software | SIL 1, SIL 2 | SIL 3/4: not targeted. Same rationale as IEC 61508 SIL 3/4. |
| Space (ECSS-E-ST-40C) | Space Software | Not supported | Class A/B: not targeted. Decision H correction — previous matrix incorrectly claimed Class A/B support. |
| Nuclear (IEC 60880 / IEEE 603) | Nuclear I&C | Not supported | Nuclear Class 1/2 require deterministic execution guarantees and formal proofs that EmbedIQ does not provide. |
| MISRA C:2023 | C coding standard (cross-domain) | Advisory compliance | See §3. Not enforced by default. Enforcement is application-level. |
| UNECE R155 / R156 | Automotive Cybersecurity / OTA | Supported | CSMS evidence: SECURITY_INCIDENT (0x61), AUTH_EVENT (0x62) event types. OTA evidence: OTA_UPDATE (0x15). |
| EU AI Act | AI systems (EU) | Phase 1 | AI_INFERENCE_START/END (0x17/0x18), AI_CONFIDENCE_THRESHOLD (0x1A). Art.12 logging, Art.13 explainability. |
| EU CRA | Cyber Resilience (EU) | Phase 1 | SECURITY_INCIDENT, AUTH_EVENT event types. Tamper-evident audit trail. |
| PCI DSS | Payment security | Req. 10 (logging) | AUTH_EVENT (0x62) satisfies audit log requirement. Full PCI DSS scope is application-level. |
| ISO 27001 | Information Security | Logging controls | AUTH_EVENT, SECURITY_INCIDENT provide the audit trail. Not a full ISO 27001 compliance solution. |
| NERC CIP | Power grid cybersecurity | CIP-007, CIP-010 | CONFIG_CHANGE (0x16) satisfies CIP-010 configuration tracking. |
| CDSCO / MDR 2017 | Medical devices (India) | Class A, Class B | Aligned with IEC 62304 Class A/B. India-specific: CDSCO medical device rules 2017 (MDSR). |
| AIS-190 | Automotive (India) | Phase 1 | Aligned with ISO 26262 ASIL A/B. AIS-190 is India's road vehicle cybersecurity standard. |
| TEC IoT Security | IoT (India) | Supported | Tamper-evident audit trail + SECURITY_INCIDENT satisfy TEC IoT security logging requirements. |
| IndiaAI Mission | AI / DPI (India) | Phase 1 | AI event observability supports responsible AI logging requirements. See ARCHITECTURE.md §India. |

**Key:** "Not targeted" means EmbedIQ makes no claim and provides no tooling for that level. Do not use EmbedIQ as a compliance argument for a not-targeted level.

---

## 3. MISRA C:2023 Stance

EmbedIQ targets MISRA C:2023 advisory compliance in the core headers (`core/include/*.h`) and in `core/src/`. EmbedIQ does NOT enforce MISRA C by default because:

1. MISRA C enforcement requires a specific static analysis tool (PC-lint Plus, LDRA, Polyspace) that EmbedIQ cannot assume is available in every developer environment.
2. Some MISRA C:2023 rules conflict with C11 standard library usage that is required for portability across POSIX, FreeRTOS, and Zephyr.
3. MISRA C compliance is a per-project decision — a project targeting ASIL B may require full MISRA C compliance; a project targeting SIL 1 may apply it selectively.

**What EmbedIQ does:** Core headers are written to avoid the most common MISRA C violations (no implicit integer conversions, `_Static_assert` for all struct size invariants, no VLAs, no dynamic allocation in framework code, all switch statements have default cases). This is not a claim of full MISRA C:2023 compliance.

**What you must do:** If your safety standard requires MISRA C:2023 compliance, run your chosen static analysis tool against the EmbedIQ core source files and resolve any findings against the MISRA C:2023 rule set. EmbedIQ's architectural properties (frozen contracts, no dynamic allocation, no recursion in the framework engine) mean the number of findings should be low.

---

## 4. Tamper Evidence — Two-Tier Architecture

The Observatory provides two tiers of tamper evidence. The tier you use depends on your threat model and compliance requirement.

### Tier 1 — SHA-256 Hash Chain (all devices, zero RAM overhead)

Every batch of Observatory events has a SHA-256 hash computed over the batch content and chained to the hash of the previous batch. This is implemented via the `HASH_CHAIN` TLV (type `0x0005`, defined in `docs/observability/iqtrace_format.md §5.3`).

**Fields:**
- `batch_seq` (4B): monotonic batch sequence counter
- `batch_hash` (32B SHA-256): hash of this batch's event records
- `operator_id` (4B): operator identifier (provisioned at manufacture)
- `hash_algorithm` (1B): algorithm identifier (0x01 = SHA-256)

**What this provides:** Any offline tool can verify that the `.iqtrace` file has not been modified after capture. A gap in the hash chain (mismatched `prev_hash`) is unambiguous evidence of tampering. This satisfies the "tamper-evident log" requirement of IEC 62443 SL 1/2, EU CRA, and NERC CIP-007 without any per-device key material.

**RAM overhead:** Zero. The hash is computed in the export path, not in the ring buffer write path. Ring buffer records are always 14 bytes (see §6). Hash computation uses a SHA-256 streaming implementation that processes records one at a time.

### Tier 2 — TRACE_SIGNATURE TLV (Phase 2, optional, SL-3+ environments)

For environments requiring cryptographic non-repudiation (IEC 62443 SL-3, nuclear audit systems, high-value financial systems), the `TRACE_SIGNATURE` TLV (type `0x0009`) provides a device-key-signed signature over the session trace.

**Fields:**
- `device_key_id` (4B): key identifier (provisioned per-device)
- `sig_algorithm` (1B): signature algorithm (0x01=ECDSA-P256, 0x02=Ed25519)
- `sig_length` (2B): length of `sig_data`
- `sig_data` (variable): cryptographic signature

**Status:** Phase 2 placeholder. The TLV type and field layout are defined; the signing infrastructure (device key provisioning, key management, HSM integration) is not yet implemented. The `signature_ref` field in `COMPLIANCE_EVENT` TLV (type `0x0006`) already provides the hook for linking a compliance event to a `TRACE_SIGNATURE` — no format change will be needed when Phase 2 implements this.

### FAQ: Why not per-event HMAC?

Per-event HMAC was considered and rejected (decision MY-05). The rationale:

A per-event HMAC requires a keyed hash operation for every `embediq_obs_emit()` call. On a Cortex-M4 running at 120 MHz with a hardware AES block, an HMAC-SHA-256 computation costs approximately 2–4 microseconds. On a Cortex-M0 without hardware crypto, the same operation costs 40–80 microseconds. The Observatory is designed to be safe to call from ISR context — a 40–80 µs blocking operation in an ISR is a timing violation for most real-time systems.

The batch SHA-256 hash chain provides equivalent tamper-evidence at the file/session level (the auditable unit) at zero ISR cost. The trade-off is that tampering within a batch (between hash checkpoints) is not individually attributed to a specific event. For the compliance requirements EmbedIQ targets (SL 1/2, ASIL A/B, SIL 1/2), batch-level hash chains are accepted practice. SL-3+ environments that require per-event attribution can use the TRACE_SIGNATURE TLV (Tier 2) at the cost of needing hardware crypto support.

---

## 5. safety_class Field Encoding

The `safety_class` field in `EmbedIQ_FB_Meta_t` (Decision D, `core/include/embediq_meta.h`) records the safety standard and level for a Functional Block.

**Canonical format:** `"STD:LEVEL"` — a single string, maximum 15 characters plus NUL terminator (`safety_class[16]`).

**Examples:**

| safety_class value | Meaning |
|---|---|
| `"NONE"` | Not safety-classified. Default for non-safety FBs. |
| `"ISO26262:ASIL-B"` | ISO 26262 Automotive Safety Integrity Level B |
| `"IEC61508:SIL-2"` | IEC 61508 Safety Integrity Level 2 |
| `"IEC62443:SL-2"` | IEC 62443 Security Level 2 |
| `"IEC62304:CLASS-B"` | IEC 62304 Software Safety Class B |
| `"DO178C:DAL-C"` | DO-178C Design Assurance Level C |
| `"EN50128:SIL-1"` | EN 50128 Safety Integrity Level 1 |
| `"AIS190:ASIL-B"` | India AIS-190, mapped to ASIL B |

**Multi-standard FBs:** Place the primary standard in `safety_class`. List all applicable standards in the compliance manifest's `safety_standards[]` array (SBOM tooling, external to the framework). The `safety_class` field is read by offline tools and manifest generators — it is not queried at runtime.

**Why a single string (CON-01 resolution):** Two competing proposals were evaluated — a split structure (`standard` field + `level` field) versus a single canonical string. The single string was chosen because: (a) all target tools (CycloneDX, SPDX, manifest generators) consume it as a single opaque tag; (b) the field is read offline, not parsed at runtime, so string-split cost is zero on-device; (c) adding a new standard does not require changing the struct layout — the string format is self-describing.

---

## 6. Ring Buffer Record Size — 14-Byte Guarantee

Every Observatory ring buffer record is exactly **14 bytes** on every platform, every target, every compliance level.

This is enforced by `_Static_assert(sizeof(EmbedIQ_Event_t) == 14, ...)` in `core/include/embediq_obs.h` (Invariant I-02). The assert fires at compile time if any compiler, alignment, or padding change would violate the guarantee.

**Field breakdown:**

| Field | Type | Size | Notes |
|---|---|---|---|
| `event_type` | `uint8_t` | 1 | Event type constant (0x10–0x8F band) |
| `source_fb_id` | `uint8_t` | 1 | Source FB endpoint index |
| `target_fb_id` | `uint8_t` | 1 | Target FB endpoint index (0 = not addressed) |
| `state_or_flag` | `uint8_t` | 1 | Event-specific payload |
| `msg_id` | `uint16_t` | 2 | Message type ID |
| `timestamp_us` | `uint32_t` | 4 | Microseconds since session start (wraps ~71 min) |
| `sequence` | `uint32_t` | 4 | Monotonic sequence counter — canonical ordering field |
| **Total** | | **14** | Enforced by `_Static_assert` |

**Why fixed-size matters for compliance:** Certification tools, forensic analysis tools, and offline audit systems can be written once and applied to `.iqtrace` files from any device. There is no version-negotiation, no per-device schema, no padding variability. A file produced on a Cortex-M0 is byte-identical in structure to a file produced on an x86-64 Linux gateway. This is a deliberate design choice (Decision G).

**Overflow is a gap record, not data loss:** When the ring buffer overflows, the `EMBEDIQ_OBS_EVT_OVERFLOW` (0x10) event is emitted as the next record. An overflow event IS an audit gap record — it records that some events were not captured. The sequence number gap between the pre-overflow record and the post-overflow record quantifies the gap. This is tamper-evident by design: an attacker who removes events from the ring buffer creates a sequence number gap that is indistinguishable from an overflow gap — both are visible to the offline audit tool.

---

## 7. SBOM Format — CycloneDX + SPDX (CON-02 Resolution)

EmbedIQ supports two SBOM formats (Decision F, CON-02 resolution):

**CycloneDX (primary):** EmbedIQ's compliance manifest generator produces CycloneDX v1.5 BOM. CycloneDX is the primary format because it natively supports: VEX (vulnerability exploitability exchange), `safety_class` custom properties, hardware component entries (MCU, RTOS), and AI/ML BOM extensions (ML-BOM). CycloneDX is required by the EU CRA and recommended by NIST SSDF.

**SPDX (secondary, via Protobom):** EmbedIQ supports SPDX 2.3 output via the [Protobom](https://github.com/protobom/protobom) translation layer. SPDX is required by some government procurement and open source license compliance workflows. Protobom provides a lossless CycloneDX→SPDX translation that preserves all custom fields.

**Why both (CON-02 rationale):** A proposal to use SPDX exclusively was rejected because SPDX lacks native support for `safety_class`, VEX, and ML-BOM fields that are required for safety-critical compliance manifests. A proposal to use CycloneDX exclusively was rejected because several enterprise customers and government procurement systems require SPDX. The Protobom bridge satisfies both without maintaining two separate generators.

**ML-BOM:** For systems deploying AI models, EmbedIQ's CycloneDX BOM generator supports the ML-BOM extension — model component entries with accuracy metrics, training data lineage, and intended use declarations. This satisfies EU AI Act Annex IV (technical documentation) requirements.

---

## 8. TRACE_SIGNATURE Phase 2 Tracking

The following items are tracked as open Phase 2 work for the TRACE_SIGNATURE TLV:

| Item | Description | Owner | Status |
|---|---|---|---|
| P2-SIGN-01 | Device key provisioning specification | TBD | PLANNED |
| P2-SIGN-02 | HSM integration contract (hal_hsm.h) | TBD | PLANNED |
| P2-SIGN-03 | ECDSA-P256 signing implementation for posix target | TBD | PLANNED |
| P2-SIGN-04 | Key management guide (rotation, revocation) | TBD | PLANNED |
| P2-SIGN-05 | Offline signature verification in embediq_obs CLI | TBD | PLANNED |

These items are tracked in `ROADMAP.md`. None of them affect the Phase 1 format — the `TRACE_SIGNATURE` TLV slot (0x0009) and the `signature_ref` hook in `COMPLIANCE_EVENT` TLV are already reserved and defined.

---

*Document version 1.0 — EmbedIQ Final Decision Set v2.0 implementation.*
