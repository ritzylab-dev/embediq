# AI-First Architecture — EmbedIQ Observatory

**Status:** Phase 1 (constants + TLV defined, implemented)
**Decision Reference:** Decision J (Final Decision Set v2.0)
**Depends on:** `core/include/embediq_obs.h`, `docs/observability/iqtrace_format.md`
**Author:** Ritesh Anand — embediq.com | ritzylab.com
**SPDX-License-Identifier:** Apache-2.0

---

## 1. Bounded AI Philosophy

EmbedIQ is not an AI runtime. EmbedIQ is an **audit substrate** that makes AI-enhanced embedded systems trustworthy, certifiable, and regulatorily legible.

The core thesis is simple: AI models already run on embedded devices. Regulators (EU AI Act, NITI Aayog AI Policy, FDA AI/ML SaMD) require that high-stakes AI decisions be logged. The Observatory's tamper-evident `.iqtrace` ring buffer is the natural carrier for those logs — it already runs on every device that carries EmbedIQ, it imposes zero additional RAM overhead per event, and it is structurally designed to surface audit gaps (overflows are gap records by design).

EmbedIQ therefore adds AI observability in two tightly scoped phases:

- **Phase 1 (now):** Four event type constants in the SYSTEM band (0x17–0x1A) and one TLV type (AI_CODER_SESSION, 0x0008) that covers AI-generated firmware provenance. These can be emitted today by any FB that wraps an inference call.
- **Phase 3 (planned):** A dedicated AI event family band (0x80–0x8F) with a richer taxonomy including policy enforcement and drift detection. This phase is gated on ≥2 production deployments using the Phase-1 constants.

What EmbedIQ AI observability is **not**:
- An inference engine or model execution runtime
- A replacement for dedicated MLOps tooling
- A cloud telemetry pipeline
- A mechanism that modifies or stops AI decisions at runtime (see §4 for the policy layer intent)

---

## 2. Phase 1 — Four AI Event Type Constants

All four constants live in the SYSTEM family band (0x10–0x1F), defined in `core/include/embediq_obs.h`.

### 2.1 Constant Table

| Constant | Value | Purpose |
|---|---|---|
| `EMBEDIQ_OBS_EVT_AI_INFERENCE_START` | `0x17` | AI model inference begins |
| `EMBEDIQ_OBS_EVT_AI_INFERENCE_END` | `0x18` | AI model inference completes |
| `EMBEDIQ_OBS_EVT_AI_MODEL_LOAD` | `0x19` | AI model loaded into memory |
| `EMBEDIQ_OBS_EVT_AI_CONFIDENCE_THRESHOLD` | `0x1A` | Confidence dropped below threshold |

These occupy the contiguous block 0x17–0x1A. This allocation is the resolution of **CON-05** (see Conflict Resolution Log v1.0): the AI block wins this slot; the three system-operation constants that previously occupied 0x17–0x1D were shifted to 0x1B–0x1D (`MAINTENANCE_MODE`, `FW_START`, `CONFIG_LOAD`).

### 2.2 Rationale for SYSTEM Band (not a dedicated AI band)

Placing Phase-1 constants in the SYSTEM band rather than creating a new AI family band was a deliberate conservative choice for three reasons:

1. **Binary compatibility**: Four constants do not justify a new band boundary. Adding a band boundary for four constants in an initial release would waste the 0x80 band on something that could grow arbitrarily.
2. **Regulatory primacy**: The EU AI Act requires that AI decision events be co-mingled with system lifecycle events for a complete audit picture. A SYSTEM-band placement naturally collocates an inference event with a preceding BOOT event and a following FAULT event in the same ring buffer.
3. **Phase-gate discipline**: Reserving the 0x80–0x8F band as a Phase-3 commitment (see §5) forces at least two production deployments before the taxonomy is finalised. This avoids premature taxonomy lock-in.

### 2.3 Field Usage per Constant

**`EMBEDIQ_OBS_EVT_AI_INFERENCE_START` / `EMBEDIQ_OBS_EVT_AI_INFERENCE_END`**

```
event_type      = 0x17 / 0x18
source_fb_id    = FB index of the inference wrapper FB
target_fb_id    = 0 (not addressed to a specific FB)
state_or_flag   = inference slot index (0–255; ties START to END if parallel inferences possible)
msg_id          = model ID (registered in messages_registry.json or 0x0000 if ad hoc)
timestamp_us    = inference wall-clock time (pair start–end for latency evidence)
sequence        = canonical ring buffer sequence (always monotonic — use this for ordering, not timestamp)
```

Pair `AI_INFERENCE_START`/`AI_INFERENCE_END` by matching `state_or_flag` (slot index) and `source_fb_id`. The delta of `timestamp_us` between the pair is the inference latency. This satisfies the EU AI Act Art.12 logging obligation for "input–output traceability" on high-risk AI systems.

**`EMBEDIQ_OBS_EVT_AI_MODEL_LOAD`**

```
event_type      = 0x19
source_fb_id    = FB index loading the model
state_or_flag   = model format: 0=TFLite, 1=ONNX, 2=custom, 0xFF=unknown
msg_id          = model version ID (application-defined)
```

The `session_id` in `EmbedIQ_Obs_Session_t` links all subsequent inference events back to the load event — the session boundary is the natural scope for "which model was running".

**`EMBEDIQ_OBS_EVT_AI_CONFIDENCE_THRESHOLD`**

```
event_type      = 0x1A
source_fb_id    = FB index of the inference wrapper
state_or_flag   = observed confidence value (0–255 maps to 0–100%)
msg_id          = model ID
```

Emitted when an inference result falls below the application-configured confidence threshold. This satisfies EU AI Act Art.13 (transparency/explainability): the audit trail shows the system explicitly detected and logged a low-confidence decision. The application layer decides what action to take (fall back to rule-based logic, alert operator, etc.) — the Observatory only records that the threshold event occurred.

---

## 3. AI_CODER_SESSION TLV — Firmware Provenance

TLV type `0x0008` (`AI_CODER_SESSION`) is defined in `docs/observability/iqtrace_format.md` §5.7.

This TLV answers the question: "Was this firmware (or a specific FB in it) written or modified by an AI coding assistant?" It is the `.iqtrace` equivalent of a code provenance attestation.

### 3.1 Field Layout (27 bytes, fixed)

| Offset | Size | Field | Description |
|---|---|---|---|
| 0 | 2 | `agent_id` | AI agent type: 0x0001=GitHub Copilot, 0x0002=Claude, 0x0003=Cursor, 0xFFFF=unknown |
| 2 | 16 | `model_version` | NUL-padded model version string, e.g. `"claude-sonnet-4-6"` |
| 18 | 8 | `prompt_hash` | FNV-1a 64-bit hash of the prompt used to generate the code change |
| 26 | 1 | `safety_class_reviewed` | 0=not reviewed, 1=reviewed-pass, 2=reviewed-fail, 3=not applicable |

Total: 27 bytes. TLV header adds 4 bytes (`type`=2B + `length`=2B), so 31 bytes on wire.

### 3.2 Relationship to AI Code Review Gate

When `safety_class_reviewed` is set to `1` (reviewed-pass), this field asserts that the AI Code Review Gate was executed for the FB(s) modified by this session (see §4 and AGENTS.md §AI-Code-Review-Gate). This creates a two-link chain:

```
AI_CODER_SESSION TLV
  └─ safety_class_reviewed=1
        └─ asserts: AI Code Review Gate passed
              └─ human reviewer signed off on safety_class field
                    └─ documented in: EmbedIQ_FB_Meta_t.safety_class
```

An offline compliance tool can traverse this chain and produce a deterministic "AI-touched, human-reviewed" provenance report for certification artefacts.

### 3.3 What This TLV Does NOT Prove

- It does **not** prove the code is correct or safe. That is the job of the review gate and the safety_class field.
- It does **not** prevent AI-generated code from shipping — EmbedIQ takes no position on whether AI-generated code is acceptable. The TLV records that AI was involved; the safety regime determines acceptability.
- It does **not** contain the actual prompt, code diff, or model weights. Only the hash. Reconstruction from the hash requires the original prompt to be retained in the CI system.

---

## 4. AI Code Review Gate

When an AI coding assistant (GitHub Copilot, Claude, Cursor, or any tool that populates `AI_CODER_SESSION`) modifies a Functional Block whose `EmbedIQ_FB_Meta_t.safety_class` field is not `"NONE"`, the following gate is mandatory before the commit is merged.

### 4.1 Gate Triggers

The gate is triggered when **both** of the following are true:

1. The commit was made by an AI assistant (determined by AI_CODER_SESSION TLV presence in the build session, or by CI metadata, or by explicit developer declaration in the commit message).
2. The modified FB has `safety_class != "NONE"` in its `EmbedIQ_FB_Meta_t` registration.

FBs with `safety_class == "NONE"` are not subject to this gate — AI modifications to non-safety-classified FBs follow the standard code review process.

### 4.2 Gate Actions (mandatory, in order)

1. **Identify scope.** List every FB modified by the AI session whose `safety_class != "NONE"`. The session boundary is defined by the `AI_CODER_SESSION` TLV emitted at the start of the AI coding session.
2. **Assign human reviewer.** The reviewer must be a person qualified at the safety level of the highest-rated FB modified in the session. For `ISO26262:ASIL-B` or higher, or `IEC61508:SIL-2` or higher, the reviewer must hold the relevant functional safety competency.
3. **Conduct review.** The reviewer confirms that:
   - The AI modification does not weaken the safety mechanisms described in the FB's safety analysis artefact.
   - All `_Static_assert` invariants referencing the modified types remain correct.
   - No new undefined behaviour has been introduced (review must include at minimum a static analysis pass with the strictness level appropriate to the `safety_class`).
4. **Record outcome.** Set `safety_class_reviewed` in `AI_CODER_SESSION` TLV to `1` (pass) or `2` (fail).
5. **Commit block.** If `safety_class_reviewed == 2`, the commit must not be merged to `main` until the issues identified in the review are resolved and the review re-run.

### 4.3 Rationale

The gate exists because:

- Safety-classified FBs have certification artefacts (FMEA, safety analysis, test evidence) that were produced by qualified humans. An AI assistant that modifies the implementation without updating those artefacts creates a compliance gap.
- Regulators (ISO 26262, IEC 61508) require traceability from requirement to implementation to test to certificate. An undocumented AI modification breaks the traceability chain.
- The `AI_CODER_SESSION` TLV provides the raw data; the gate provides the process that makes the data meaningful.

This gate does **not** prohibit AI-assisted development of safety-classified FBs. It requires that a qualified human reviews the AI's output before it enters the safety evidence trail.

---

## 5. Phase 3 AI Event Family Band (0x80–0x8F)

**Status:** RESERVED — not yet allocated. Do not assign constants in this range.

### 5.1 Reservation Rationale

The 0x80–0x8F range is explicitly reserved by a comment block in `embediq_obs.h`. Reservation accomplishes two things:

1. Prevents third-party BSP vendors from squatting on this range with proprietary constants that would collide with the future standard taxonomy.
2. Commits EmbedIQ to delivering a Phase-3 AI taxonomy without forcing premature lock-in.

### 5.2 Planned Phase-3 Constants (taxonomy TBD)

The following names are illustrative and subject to change before Phase 3 is finalised:

```
0x80  AI_POLICY_ALLOW          — AI policy engine approved an action
0x81  AI_POLICY_BLOCK          — AI policy engine blocked an action
0x82  AI_MODEL_UPDATE          — AI model version changed in-field
0x83  AI_DRIFT_DETECTED        — Statistical drift detected in model inputs or outputs
0x84  AI_EXPLAINABILITY_LOG    — Explainability record emitted (links to TLV payload)
0x85  AI_FEDERATED_ROUND       — Federated learning round participation event
0x86–0x8F  (reserved)
```

These names and values are illustrative. The Phase-3 specification document will be the authoritative source.

### 5.3 Phase-3 Gate Criteria

Phase 3 is not commenced until:

- At least 2 production deployments have used the Phase-1 constants (0x17–0x1A) for ≥6 months.
- Feedback from those deployments has been incorporated into the Phase-3 taxonomy draft.
- A Phase-3 specification document has been reviewed and accepted via the standard EmbedIQ decision process.

See `ROADMAP.md` for the Phase-3 entry.

---

## 6. `.iqtrace` as AI Training Data Moat

An often-overlooked property of the `.iqtrace` format is that it accumulates **labelled, timestamped, tamper-evident execution traces** from real embedded systems in the field. This data is structurally valuable as training data for AI models that target embedded system behaviour.

Specifically:

- Every `AI_INFERENCE_START`/`AI_INFERENCE_END` pair is a latency measurement on real hardware with real workloads.
- `AI_CONFIDENCE_THRESHOLD` events are natural labels for "model underperformed here."
- `FAULT` and `SECURITY_INCIDENT` events adjacent to inference events are natural labels for "model contributed to or failed to prevent this outcome."
- The 14-byte fixed record size means traces from different devices are structurally identical — trivially mergeable into a cross-device training corpus.

A device fleet running EmbedIQ accumulates a proprietary dataset of AI behaviour on embedded hardware that no cloud provider can replicate (the cloud does not have access to the ring buffer; the `.iqtrace` never leaves the device unless the application operator explicitly exports it).

This is a **strategic asymmetry** for EmbedIQ adopters: the longer a device fleet runs, the deeper the proprietary dataset, and the better the AI models that can be trained against it.

### 6.1 Multi-Device Correlation

The `session_id` and `device_id` fields in `EmbedIQ_Obs_Session_t` (I-14 frozen struct) are the correlation keys for fleet-level analysis:

```
device_id     — unique per physical device (provisioned at manufacture)
session_id    — unique per boot session
timestamp_base_us + sequence — reconstruct absolute event timeline per device
```

An offline analytics tool can join `.iqtrace` streams from N devices using `device_id` and align them on `timestamp_base_us` (wall-clock anchor for the session) to produce a fleet-level event timeline. No changes to the on-device format are required — the fields are already present in the frozen `EmbedIQ_Obs_Session_t` struct.

### 6.2 What `.iqtrace` Does NOT Store

To avoid privacy risk and regulatory exposure, the Observatory deliberately excludes:

- **User PII:** No name, email, identifier, or biometric. The `device_id` is a hardware serial number, not a user identifier.
- **Inference inputs or outputs:** The 14-byte event record stores the *fact* of an inference event, not the payload. A model processing a camera frame does not store the frame.
- **Model weights or architecture:** Model versioning is by ID, not by content.
- **Network addresses or connectivity state:** No IP/MAC addresses stored.
- **Application payload content:** `msg_id` stores the message type identifier, not the message content.

This exclusion is structural (the 14-byte fixed record physically cannot hold PII payloads), not policy-only. Structural exclusion is the most defensible position for GDPR Article 25 (data protection by design and by default).

---

## 7. India Market — AI Alignment

EmbedIQ's AI observability layer aligns directly with the IndiaAI Mission's Digital Public Infrastructure (DPI) positioning and the NITI Aayog Responsible AI for All framework:

- **IndiaAI Mission Computing Capacity:** Edge AI deployments that run EmbedIQ accumulate `.iqtrace` datasets that can feed back into IndiaAI compute infrastructure for model improvement — a natural closed loop.
- **NITI Aayog Responsible AI:** The AI Code Review Gate and `safety_class_reviewed` field directly implement the human-oversight requirements in the Responsible AI for All framework.
- **Missing Middle thesis:** Most Indian embedded system deployments are mid-tier (not hyperscale, not deeply resource-constrained). EmbedIQ's fixed 14-byte ring buffer and zero-RAM-overhead Observatory target exactly this segment — enough compute to run inference, not enough to run full MLOps toolchains.
- **PLI Scheme alignment:** Electronics PLI incentivises domestic hardware production. Domestic hardware running EmbedIQ Observatory accumulates domestic `.iqtrace` datasets — fulfilling data-locality expectations without requiring cloud infrastructure.

See `ARCHITECTURE.md §India Market` for the full market positioning section.

---

## 8. Open Items (Phase 2 / Phase 3)

| ID | Description | Phase |
|---|---|---|
| AI-07 | AI Policy Layer: runtime policy engine that can ALLOW/BLOCK AI actions based on safety_class and Observatory state. Architecture TBD. | Phase 3 |
| AI-08 | AI_CODER_SESSION TLV tooling: CI plugin that auto-emits TLV and invokes review gate for safety-classified FBs. | Phase 2 |
| AI-09 | Fleet analytics reference implementation: joins `.iqtrace` streams from N devices using device_id/session_id correlation keys. | Phase 3 |
| AI-10 | Phase-3 AI band taxonomy specification: replace illustrative names in §5.2 with a reviewed, finalised constant table. | Phase 3 |

---

*Document version 1.0 — EmbedIQ Final Decision Set v2.0 implementation.*
