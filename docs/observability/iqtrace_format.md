# EmbedIQ Observability Format Specification

**Version:** 1.1
**Status:** Stable
**License:** Apache 2.0
**Authors:** Ritesh Anand

---

## 1. Scope

This document specifies three separate observability data formats used
by EmbedIQ:

| Concept | Description |
|---------|-------------|
| **Runtime Event Record** | Fixed 14-byte in-memory struct (`EmbedIQ_Event_t`) |
| **Live Stream Protocol** | Real-time framing for UART / TCP / RTT transports |
| **`.iqtrace` File Format** | Captured session file for offline analysis |

These are distinct. The runtime record is the unit of observation. The
live stream and `.iqtrace` file are different carriers of that record.
Do not conflate them.

---

## 2. Conventions

- All multi-byte integers are **little-endian**.
- Reserved fields must be written as zero and ignored on read.
- A decoder encountering an unknown TLV record type must skip it
  using the length field. This is the forward-compatibility guarantee.
- String fields are null-terminated. Unused bytes must be zero.

---

## 3. Runtime Event Record

The runtime event record is the atomic unit of EmbedIQ observability.
It is defined in `core/include/embediq_obs.h` as `EmbedIQ_Event_t`.

**This struct is frozen at v1. It must never change.** (Invariant I-02)
```c
typedef struct __attribute__((packed)) {
    uint8_t   event_type;      // EMBEDIQ_OBS_EVT_* — encodes family via band
    uint8_t   source_fb_id;    // originating FB endpoint index
    uint8_t   target_fb_id;    // destination FB endpoint index (0xFF = N/A)
    uint8_t   state_or_flag;   // FB lifecycle state or context-specific flag
    uint16_t  msg_id;          // associated message ID (0 = not message-related)
    uint32_t  timestamp_us;    // INFORMATIONAL — wraps ~71 min (see I-13)
    uint32_t  sequence;        // CANONICAL ORDER — monotonic, never resets
} EmbedIQ_Event_t;             // sizeof == 14
```

**Event family** is derived from `event_type` by the decoder, not stored
explicitly. The band map is:

| Band (hex) | Family   | Notes |
|------------|----------|-------|
| 0x10-0x1F  | SYSTEM   | Observatory internal (overflow, session start) |
| 0x20-0x2F  | MESSAGE  | Message send/receive/drop |
| 0x30-0x3F  | STATE    | FB lifecycle and FSM transitions |
| 0x40-0x4F  | RESOURCE | CPU, memory, stack (reserved, not yet emitted) |
| 0x50-0x5F  | TIMING   | Latency, jitter, handler duration (reserved) |
| 0x60-0x6F  | FAULT    | Watchdog, assertion, error entry |
| 0x70-0x7F  | FUNCTION | Function enter/exit tracing (reserved) |

**Ordering invariant (I-13):** Always use `sequence` for ordering and gap
detection. `timestamp_us` is informational only and wraps at ~71 minutes.

**Ring buffer size guarantee (Decision G):** The ring buffer record size is
**always exactly 14 bytes per record on every device, regardless of compliance
level, platform, or trace verbosity setting.** A Cortex-M0 thermostat and a
medical infusion pump produce identical 14-byte records. This guarantee is
enforced by `_Static_assert(sizeof(EmbedIQ_Event_t) == 14)` in
`core/include/embediq_obs.h` and must never be broken.

**Overflow events as tamper-evident gap records:** When the ring buffer
overflows, an `EMBEDIQ_OBS_EVT_OVERFLOW` event (0x10) is emitted. This event
IS the audit gap record — by design. An overflow event tells the auditor:
"audit data was lost here, and this loss is itself recorded." Decoders and
compliance tools must surface overflow events, not suppress them.

---

## 4. Session Metadata Record

Every observability session is identified by a session metadata struct,
defined in `core/include/embediq_obs.h` as `EmbedIQ_Obs_Session_t`.

**This struct is frozen. sizeof == 40.** (Invariant I-14)
```c
typedef struct {
    uint32_t device_id;          // application-assigned device identifier
    uint32_t fw_version;         // packed: bits[31:16]=major [15:8]=minor [7:0]=patch
    uint64_t timestamp_base_us;  // Unix time us at session start (0 = unavailable)
    uint32_t session_id;         // monotonic per-boot counter (0 = not tracked)
    uint8_t  platform_id;        // embediq_obs_platform_t
    uint8_t  trace_level;        // EMBEDIQ_TRACE_LEVEL at session start
    uint8_t  _pad[2];            // reserved — must be zero
    char     build_id[16];       // null-terminated build tag (e.g. git short hash)
} EmbedIQ_Obs_Session_t;        // sizeof == 40
```

`fw_version` helper: `EMBEDIQ_OBS_FW_VERSION(major, minor, patch)`.

---

## 5. TLV Record Envelope

Both the live stream and the `.iqtrace` file use the same TLV envelope
to carry records. The envelope is **4 bytes** followed by the payload.

```
Offset  Size  Field
------  ----  ----------------------------------------
0       2     type    (uint16_t, little-endian)
2       2     length  (uint16_t, little-endian — payload bytes only)
4       N     payload (N = length bytes)
```

**Total record size = 4 + length bytes.**

### 5.1 Record Type Registry

| Type (hex) | Name                | Payload size | Status      | Description |
|-----------|---------------------|-------------|-------------|-------------|
| `0x0000`  | INVALID             | -           | Existing    | Must never appear. Decoder: treat as corruption. |
| `0x0001`  | SESSION             | 40 bytes    | **FROZEN**  | `EmbedIQ_Obs_Session_t` — must be first TLV. (I-14) |
| `0x0002`  | EVENT               | 14 bytes    | **FROZEN**  | Single `EmbedIQ_Event_t`. (I-02) |
| `0x0003`  | EVENT_BATCH         | 2 + (Nx14)  | **FROZEN**  | `uint16_t count` (LE) + N × `EmbedIQ_Event_t` |
| `0x0004`  | STREAM_END          | 0 bytes     | **FROZEN**  | Optional clean-close marker |
| `0x0005`  | HASH_CHAIN          | 44 bytes    | New — Dec C | SHA-256 hash chain for tamper evidence. See §5.3. |
| `0x0006`  | COMPLIANCE_EVENT    | 12 bytes    | New — Dec C | Compliance context record. See §5.4. |
| `0x0007`  | DEVICE_CERT         | variable    | New — Dec C | Device certificate. See §5.5. |
| `0x0008`  | AI_CODER_SESSION    | 27 bytes    | New — Dec J | AI agent code authorship record. See §5.6. |
| `0x0009`  | TRACE_SIGNATURE     | variable    | **Phase 2** | Optional device signing for SL-3+. See §5.7. |
| `0x00FF`  | PADDING             | any         | Existing    | Ignored by decoder — alignment or future use |
| `0xFFFF`  | RESERVED            | -           | Existing    | Reserved; decoder must skip |

Unknown type values: **decoder must skip** using the `length` field.
This is the forward-compatibility contract — new record types can be added
without breaking old decoders. Types `0x0009` and above that a decoder does
not recognise must be skipped via `length`, not treated as errors.

### 5.2 EVENT_BATCH layout

```
Offset  Size     Field
------  ----     ----------------------------------------
0       2        count (uint16_t, little-endian)
2       14xcount events (count x EmbedIQ_Event_t, packed, in sequence order)
```

EVENT_BATCH is provided for transport efficiency. A decoder must treat
each event in the batch identically to an individual EVENT record.

### 5.3 HASH_CHAIN TLV (0x0005) — Decision C

Provides SHA-256 hash chain tamper evidence over a batch of events.
Written by the producer after each EVENT_BATCH. Baseline for all
compliance levels. (CON-03 resolution.)

```
Offset  Size  Field
------  ----  ----------------------------------------
0       4     batch_seq       (uint32_t LE) — sequence number of the first
                               event in the corresponding batch
4       32    batch_hash      SHA-256 hash of all EmbedIQ_Event_t records
                               in the batch, concatenated in sequence order.
                               MUST be 32 bytes. (E1-01 critical fix: was 4 bytes.)
36      4     operator_id     (uint32_t LE) — identity of the human operator
                               who initiated the session or the triggering action.
                               0x00000000 = automated / no operator.
                               MUST be 4 bytes to accommodate GDPR-scope UUIDs.
                               (E1-02 fix: was 2 bytes.)
40      1     hash_algorithm  0x01 = SHA-256 (current). Unknown value = decoder
                               skips hash validation but does not reject the TLV.
                               (Algorithm agility — E1-03.)
41      3     _pad            Reserved — must be zero.
```

**Total payload: 44 bytes.**

A decoder verifying tamper evidence:
1. Reads all EVENT or EVENT_BATCH records between this HASH_CHAIN and the
   previous HASH_CHAIN (or SESSION, if first batch).
2. Concatenates the raw `EmbedIQ_Event_t` bytes in sequence order.
3. Computes SHA-256 over the concatenation.
4. Compares to `batch_hash`. Mismatch = tamper detected.

### 5.4 COMPLIANCE_EVENT TLV (0x0006) — Decision C

Records compliance-relevant context for a specific event in the batch.

```
Offset  Size  Field
------  ----  ----------------------------------------
0       2     compliance_type   (uint16_t LE) — compliance event category.
                                 0x0001 = safety class transition
                                 0x0002 = operator action
                                 0x0003 = configuration change
                                 0x0004 = security boundary crossing
                                 Unknown values: decoder skips gracefully.
2       4     standard_ref      (uint32_t LE) — packed standard identifier.
                                 Bits[31:16] = standard code, bits[15:0] = clause.
4       4     event_timestamp_us (uint32_t LE) — timestamp_us of the associated
                                 EmbedIQ_Event_t record.
8       2     signature_ref     (uint16_t LE) — byte offset from start of this
                                 TLV to a TRACE_SIGNATURE TLV (0x0009) in the
                                 same stream. 0x0000 = no signature present
                                 (Phase 1 default). Non-zero = optional SL-3+
                                 device signature. (CON-03 Phase 2 hook.)
```

**Total payload: 12 bytes.**

### 5.5 DEVICE_CERT TLV (0x0007) — Decision C

Carries a device certificate for identity verification.

```
Offset  Size      Field
------  ----      ----------------------------------------
0       1         cert_type   0x01 = X.509 DER, 0x02 = manufacturer cert,
                               0x03 = custom. Unknown = skip.
1       2         cert_length (uint16_t LE) — byte length of cert_data.
3       cert_len  cert_data   Raw certificate bytes.
```

**Total payload: 3 + cert_length bytes (variable).**

### 5.6 AI_CODER_SESSION TLV (0x0008) — Decision J

Records AI agent authorship when AI-generated or AI-assisted code modifies
a Functional Block. Written by the CI/CD pipeline or AI agent tooling when
the AI Code Review Gate is triggered. See `docs/architecture/AI_FIRST_ARCHITECTURE.md`.

```
Offset  Size  Field
------  ----  ----------------------------------------
0       2     agent_id              (uint16_t LE) — AI agent identifier.
                                     0x0001 = GitHub Copilot
                                     0x0002 = Claude (Anthropic)
                                     0x0003 = ChatGPT (OpenAI)
                                     0x00FF = unknown / custom agent
2       16    model_version         UTF-8 model identifier string, null-padded
                                     to 16 bytes. Example: "claude-sonnet-4-6\0..."
18      8     prompt_hash           First 8 bytes of the SHA-256 hash of the
                                     prompt used to generate the code change.
                                     All-zeros if prompt is unavailable.
26      1     safety_class_reviewed 0x00 = no safety_class-tagged FBs were
                                     modified, or review not yet complete.
                                     0x01 = safety_class-tagged FBs were modified
                                     AND a human reviewer has confirmed review.
                                     CI/CD gate: block merge if 0x00 and any
                                     safety-tagged FB was changed.
```

**Total payload: 27 bytes.**

### 5.7 TRACE_SIGNATURE TLV (0x0009) — Phase 2 Placeholder

> **Phase 2 — not implemented in Phase 1.**
>
> This TLV type is defined here so that decoders encountering it (in future
> Phase 2 deployments) can skip it gracefully via the `length` field.
> Do not emit this TLV in Phase 1 producers.

Optional per-device cryptographic signature for SL-3+ and high-assurance
regulated environments. Referenced by `signature_ref` in COMPLIANCE_EVENT
(§5.4) when non-zero.

```
Offset  Size      Field
------  ----      ----------------------------------------
0       4         device_key_id   (uint32_t LE) — identifies the signing key.
4       1         sig_algorithm   0x01 = ECDSA-P256. Unknown = skip.
5       2         sig_length      (uint16_t LE) — byte length of sig_data.
7       sig_len   sig_data        Raw signature bytes.
```

**Total payload: 7 + sig_length bytes (variable).**

---

## 6. `.iqtrace` File Format

A `.iqtrace` file is an append-only binary capture of an observability
session. It begins with an 8-byte file header followed by TLV records.

### 6.1 File Header

```
Offset  Size  Field
------  ----  ----------------------------------------
0       4     magic    = { 0x49, 0x51, 0x54, 0x52 }  ("IQTR")
4       2     version  (uint16_t LE) = 0x0001
6       2     reserved = { 0x00, 0x00 }
```

Total file header size: **8 bytes**.

### 6.2 File Structure

```
[File Header]           8 bytes
[SESSION TLV]           4 + 40 = 44 bytes  <-- must be first TLV
[EVENT TLV] ...         4 + 14 bytes each
or
[EVENT_BATCH TLV] ...   4 + 2 + (Nx14) bytes
[STREAM_END TLV]        4 + 0 = 4 bytes    <-- optional
```

### 6.3 Rules

1. The SESSION record must immediately follow the file header.
   A decoder that does not find `0x0001` as the first TLV type
   must reject the file as malformed.
2. Records appear in ascending `sequence` order within the file.
3. The file is **append-only**. Existing bytes must never be modified
   after being written.
4. An absent `STREAM_END` record means the session ended uncleanly
   (power loss, crash). This is valid - decoders must accept files
   without `STREAM_END`.
5. `timestamp_base_us` in the session record anchors all event
   `timestamp_us` values: `wall_time = timestamp_base_us + timestamp_us`.
   If `timestamp_base_us == 0`, absolute wall time is unavailable.

### 6.4 File Extension

The canonical extension is `.iqtrace`.

---

## 7. Live Stream Protocol

The live stream is used for real-time observation over UART, TCP, or RTT.
It is **not** a `.iqtrace` file and has no file header.

### 7.1 Frame Structure

Each live stream frame is:

```
Offset  Size  Field
------  ----  ----------------------------------------
0       4     sync  = { 0x49, 0x51, 0x4C, 0x56 }  ("IQLV")
4       2+N   TLV record (type + length + payload)
```

The sync word `IQLV` differs from the `.iqtrace` file magic `IQTR`
intentionally. A decoder receiving `IQTR` on a live stream must reject
it; a decoder opening a file starting with `IQLV` must reject it.

### 7.2 Resync

On byte loss or corruption, a decoder rescans for the sync word
`{ 0x49, 0x51, 0x4C, 0x56 }`. Once found, it reads the next TLV.

### 7.3 Session on Live Stream

On connect (or reconnect), the producer sends a SESSION TLV as the
first frame. This allows late-connecting decoders to identify the device
without replaying the full trace.

### 7.4 Record Types Allowed in Live Stream

All record types from section 5.1 are valid in the live stream with the
exception of `STREAM_END` - live streams have no logical end marker
(the connection close signals end-of-stream).

---

## 8. Decoder Requirements

A conforming decoder must:

1. Verify the 4-byte file magic before processing a `.iqtrace` file.
2. Verify `version == 0x0001`; reject files with higher versions unless
   the decoder has been updated to support them.
3. Require `SESSION` as the first TLV record type.
4. Accept `EVENT` and `EVENT_BATCH` records interchangeably.
5. Skip unknown record types using the `length` field.
6. Accept files without a `STREAM_END` record.
7. Use `sequence` for ordering, not `timestamp_us`.
8. Derive event family from `event_type` band - it is not stored.

---

## 9. Producer Requirements

A conforming producer must:

1. Write the file header before any TLV records.
2. Write the `SESSION` TLV as the first record.
3. Write events in ascending `sequence` order.
4. Zero all `_pad` and `reserved` fields.
5. Null-terminate and zero-pad `build_id` to its full 16-byte length.
6. On clean shutdown, write `STREAM_END` before closing the file.
7. Never modify bytes already written (append-only).

---

## 10. Studio File Layer (Optional Enhancement)

Studio may generate a derived indexed session file with the extension
`.iqsession`. This file is Studio-specific and not part of the open
format. It may include graph indexing, causal chain computation, and
AI reasoning context for faster replay.

The `.iqtrace` file is always the authoritative open record.
`.iqsession` files are derived artifacts - regeneratable from the
`.iqtrace` source.

---

## 11. Version History

| Version | Date | Changes |
|---------|------|---------|
| 1.1 | 2026-03 | Decision C: Add HASH_CHAIN (0x0005), COMPLIANCE_EVENT (0x0006), DEVICE_CERT (0x0007). Decision J: Add AI_CODER_SESSION (0x0008), TRACE_SIGNATURE placeholder (0x0009). Decision G: explicit 14B ring buffer guarantee and overflow-as-gap-record documentation. |
| 1.0 | 2026-03 | Initial specification |

---

## 12. Related Files

| File | Description |
|------|-------------|
| `core/include/embediq_obs.h` | `EmbedIQ_Event_t`, `EmbedIQ_Obs_Session_t`, emit macros |
| `core/include/embediq_config.h` | `EMBEDIQ_TRACE_LEVEL`, per-family flags |
| `core/src/observatory/obs.c` | Ring buffer, transport selector, session state |
| `tools/embediq_obs/embediq_obs.py` | CLI decoder (Obs-6, forthcoming) |
