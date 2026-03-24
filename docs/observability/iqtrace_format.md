# EmbedIQ Observability Format Specification

**Version:** 1.0
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

| Type (hex) | Name          | Payload size | Description |
|-----------|---------------|-------------|-------------|
| `0x0000`  | INVALID       | -           | Must never appear. Decoder: treat as corruption. |
| `0x0001`  | SESSION       | 40 bytes    | `EmbedIQ_Obs_Session_t` - must be first TLV in file |
| `0x0002`  | EVENT         | 14 bytes    | Single `EmbedIQ_Event_t` |
| `0x0003`  | EVENT_BATCH   | 2 + (Nx14)  | `uint16_t count` (LE) + count x `EmbedIQ_Event_t` |
| `0x0004`  | STREAM_END    | 0 bytes     | Optional clean-close marker |
| `0x00FF`  | PADDING       | any         | Ignored by decoder - alignment or future use |
| `0xFFFF`  | RESERVED      | -           | Reserved for future use; decoder must skip |

Unknown type values: **decoder must skip** using the `length` field.
This is the forward-compatibility contract - new record types can be
added without breaking old decoders.

### 5.2 EVENT_BATCH layout

```
Offset  Size     Field
------  ----     ----------------------------------------
0       2        count (uint16_t, little-endian)
2       14xcount events (count x EmbedIQ_Event_t, packed, in sequence order)
```

EVENT_BATCH is provided for transport efficiency. A decoder must treat
each event in the batch identically to an individual EVENT record.

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
| 1.0 | 2026-03 | Initial specification |

---

## 12. Related Files

| File | Description |
|------|-------------|
| `core/include/embediq_obs.h` | `EmbedIQ_Event_t`, `EmbedIQ_Obs_Session_t`, emit macros |
| `core/include/embediq_config.h` | `EMBEDIQ_TRACE_LEVEL`, per-family flags |
| `core/src/observatory/obs.c` | Ring buffer, transport selector, session state |
| `tools/embediq_obs/embediq_obs.py` | CLI decoder (Obs-6, forthcoming) |
