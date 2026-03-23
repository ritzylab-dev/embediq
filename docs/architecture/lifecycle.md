# FB Lifecycle Contract and OTA/Shutdown Protocol

> **Status:** Architecture decision locked — Phase 2 implementation target (fb_ota, P2-T5).
> This document describes the protocol that all FBs with persistent state must follow.
> FBs without persistent state require no changes — the engine handles them via timeout.

---

## Overview

When a firmware update (OTA) is requested, a controlled shutdown sequence must run
before the new firmware image is applied.  The sequence ensures that:

- In-flight work completes and is not lost
- NVM state is flushed to persistent storage
- No data corruption occurs at the storage layer

The protocol is message-driven (Principle 1).  `fb_ota` acts as the coordinator.
Individual FBs declare readiness by publishing a message — no direct calls.

---

## Message IDs (core namespace 0x0000–0x03FF)

| Message               | ID     | Direction        | Payload fields |
| --------------------- | ------ | ---------------- | -------------- |
| `MSG_SYS_OTA_REQUEST` | 0x0003 | fb_ota → all FBs | `reason : u8`  |
| `MSG_SYS_OTA_READY`   | 0x0004 | each FB → fb_ota | `fb_id : u8`   |
| `MSG_SYS_SHUTDOWN`    | 0x0005 | engine → all FBs | `reason : u8`  |

These IDs are reserved in the core namespace.  No other message may use them.
See `messages/core.iq` for the normative schema definitions.

---

## Sequence Diagram

```
  fb_ota              fb_nvm              fb_cloud             fb_logger
| ---------------------------------------------------------- | ------------------------------ | -------------------------- |
| -- MSG_SYS_OTA_REQUEST (0x0003) ------->                   | -------------------->          |                            |
| ---------------------------------------------------------- | ------------------------------ | -------------------------- |
| [flush NVM state]   [finish in-flight]    [drain ring buf] |                                |                            |
| ---------------------------------------------------------- | ------------------------------ | -------------------------- |
| <-- MSG_SYS_OTA_READY (0x0004) ---------                   |                                |                            |
|                                                            | MSG_SYS_OTA_READY (0x0004) --> |                            |
|                                                            |                                | MSG_SYS_OTA_READY (0x0004) |
| ---------------------------------------------------------- | ------------------------------ | -------------------------- |
| [all ready — or timeout 500 ms elapsed]                    |                                |                            |
| ---------------------------------------------------------- | ------------------------------ | -------------------------- |
| -- MSG_SYS_SHUTDOWN (0x0005) ---------->                   | -------------------->          |                            |
| ---------------------------------------------------------- | ------------------------------ | -------------------------- |
| [apply OTA image]                                          |                                |                            |
```

---

## FB Implementation Contract

### FBs that own persistent state or in-flight work

These FBs **must** implement the OTA contract:

```c
/* 1 — subscribe to the OTA request in your config */
static const EmbedIQ_MsgID_t subs[] = {
    MSG_TIMER_100MS,
    MSG_SYS_OTA_REQUEST,   /* ID 0x0003 */
};

/* 2 — handle it in your sub-fn */
static void on_ota_request(EmbedIQ_FB_Handle_t self, const EmbedIQ_Msg_t *msg)
{
    (void)msg;
    /* finish in-flight work, flush NVM, etc. */
    my_fb_flush_state();

    /* 3 — publish OTA_READY within 500 ms */
    EmbedIQ_Msg_t ready = {
        .msg_id  = MSG_SYS_OTA_READY,  /* 0x0004 */
        .payload = { .u8 = my_ep_id() },
    };
    embediq_publish(self, &ready);
}
```

**Deadline:** MSG_SYS_OTA_READY must be published within **500 ms** of receiving
MSG_SYS_OTA_REQUEST.  If the deadline expires, `fb_ota` forces shutdown anyway
and the FB's state may be lost.

### FBs with no persistent state

No subscription required.  The engine handles them via the 500 ms timeout.
Adding an unnecessary OTA subscription is a code smell.

---

## Timeout and Force-Shutdown Rules

| Condition                              | Action                                       |
| -------------------------------------- | -------------------------------------------- |
| All subscribed FBs reply within 500 ms | Clean shutdown, OTA proceeds                 |
| Timeout expires, some FBs not ready    | Force shutdown; fb_ota logs which FBs missed |
| FB publishes OTA_READY after timeout   | Message is ignored; already in shutdown      |

---

## Queue Semantics During Shutdown

- **Messages already in queues are NOT drained** before shutdown.
  Any unprocessed messages are lost.
- This is intentional: queues hold transient state, not persistent state.
  FBs that care about durability must flush to NVM before publishing OTA_READY.
- After `MSG_SYS_SHUTDOWN` is broadcast, the engine calls
  `embediq_engine_dispatch_shutdown()` to join all dispatch threads.

---

## Implementation Notes (Phase 2)

`fb_ota` (P2-T5) will implement the coordinator role:

1. On receiving an external OTA trigger (MQTT or local), publish MSG_SYS_OTA_REQUEST.
2. Track which FBs have an OTA subscription (static registry query at boot).
3. Collect MSG_SYS_OTA_READY responses; start 500 ms countdown timer.
4. On all-ready or timeout: broadcast MSG_SYS_SHUTDOWN, then call
   `embediq_engine_dispatch_shutdown()`, then hand off to bootloader.

`embediq_engine_dispatch_shutdown()` (implemented in P2-T0b) handles the thread
teardown.  `fb_ota` does not call it directly — it publishes MSG_SYS_SHUTDOWN and
the engine's shutdown hook fires.

---

## Related Files

| File                           | Purpose                                   |
| ------------------------------ | ----------------------------------------- |
| `AGENTS.md §7`                 | Rule summary (binding)                    |
| `messages/core.iq`             | Normative message schema                  |
| `core/include/embediq_fb.h`    | `embediq_engine_dispatch_shutdown()` decl |
| `components/fb_ota/` (Phase 2) | Coordinator FB implementation             |

---

*EmbedIQ — embediq.com — Apache 2.0*
