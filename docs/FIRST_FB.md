# Your First Function Block

This guide walks you from zero to a working, observable FB on Linux in
about 20 minutes. No hardware required.

You will build `fb_sensor_temp` — a temperature-sensor FB that publishes
`MSG_TEMP_READING` every second. By the end you will have:
- A working FB that compiles and runs against the existing thermostat demo
- Observatory output showing every message and lifecycle event
- A clear mental model of the sub-function pattern
- An understanding of what CI enforces and why

**Prerequisites:** C11, CMake 3.16+, Python 3.8+, a Linux or macOS machine.
Clone the repo and confirm `bash scripts/check.sh` passes before starting.

---

## 1. What is a Function Block?

A Function Block (FB) is the unit of everything in EmbedIQ. An FB:
- **Owns its state privately** — no other FB touches it directly
- **Communicates only via typed messages** on the shared bus
- **Is observable by default** — every lifecycle event is captured
  automatically with zero developer code

The wrong patterns — direct cross-FB calls, shared global state, blocking
inside handlers — fail CI, not code review.

---

## 2. How an FB receives messages: the sub-function pattern

An FB does not have a single `update()` function. Instead it registers
**sub-functions** — one per message type it handles. The framework calls
the matching sub-function when a subscribed message arrives.

```c
/* Tell the framework which messages this FB handles */
static const uint16_t k_subs[] = { MSG_TIMER_1SEC };

/* The handler — called by the framework when MSG_TIMER_1SEC arrives */
static void on_tick(EmbedIQ_FB_Handle_t fb, const void *msg,
                    void *fb_data, void *subfn_data)
{
    /* read sensor, publish result */
}
```

This matters because:
- Each sub-function runs only when its message arrives — no polling loop
- The framework owns threading, queuing, and priority — you never touch them
- CI (`check_fb_calls.py`) rejects any FB that calls OSAL, RTOS, or OS
  threading functions directly (I-07, R-02, R-sub-03, R-sub-03a)

---

## 3. Where the file goes

Domain-specific application FBs live in `examples/<your_app>/`. The portable
Driver FBs in `fbs/drivers/` (fb_timer, fb_nvm, fb_watchdog) are reserved for
hardware-adjacent code that calls the HAL contract — they do not import
application message schemas. This mirrors how `examples/thermostat/`
organises its sensor and controller FBs.

For this tutorial you will add two files under `examples/my_first_fb/`.

---

## 4. Build fb_sensor_temp step by step

### 4.1 The message

`MSG_TEMP_READING` and its payload type are already generated from
`messages/thermostat.iq`. Inspect them:

```
cat generated/thermostat_msg_catalog.h
```

You will see `MSG_TEMP_READING = 0x1400u` and a struct
`MSG_TEMP_READING_Payload_t` with a `float temperature_c` field. The
generator produces this struct from the `.iq` schema — you never write
it by hand (I-04).

### 4.2 Create examples/my_first_fb/fb_sensor_temp.h

```c
/*
 * fb_sensor_temp.h — Temperature sensor FB interface
 *
 * Publishes MSG_TEMP_READING once per second in response to MSG_TIMER_1SEC.
 * Simulates a sensor reading on host builds.
 *
 * @author  <your name>
 * @company <your company>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FB_SENSOR_TEMP_H
#define FB_SENSOR_TEMP_H

#include "embediq_fb.h"

/**
 * Register fb_sensor_temp with the framework.
 * Call once at application startup, before embediq_engine_boot().
 */
EmbedIQ_FB_Handle_t fb_sensor_temp_register(void);

#endif /* FB_SENSOR_TEMP_H */
```

**Gate rule:** Every `.h` and `.c` file must have `@author`, `@company`,
and `SPDX-License-Identifier`. CI (`licence_check.py`) enforces SPDX.

### 4.3 Create examples/my_first_fb/fb_sensor_temp.c

```c
/*
 * fb_sensor_temp.c — Temperature sensor FB
 *
 * Subscribes to MSG_TIMER_1SEC. On each tick, reads a simulated
 * temperature and publishes MSG_TEMP_READING.
 *
 * @author  <your name>
 * @company <your company>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "fb_sensor_temp.h"
#include "embediq_subfn.h"
#include "embediq_bus.h"
#include "embediq_msg.h"
#include "embediq_platform_msgs.h"
#include "thermostat_msg_catalog.h"

#include <stdint.h>
#include <string.h>

/* ── Private FB state ───────────────────────────────────────────── */
static float g_temp_c = 20.0f;  /* simulated temperature, °C */

/* ── Sub-function handler: one-second tick ──────────────────────── */
static void on_tick(EmbedIQ_FB_Handle_t fb, const void *msg,
                    void *fb_data, void *subfn_data)
{
    (void)msg; (void)fb_data; (void)subfn_data;

    /* Simulate a reading (replace with a real HAL call on hardware) */
    g_temp_c += 0.1f;
    if (g_temp_c > 30.0f) g_temp_c = 20.0f;

    /* Build the message — payload is memcpy'd into the envelope's
     * inline byte array (EmbedIQ_Msg_t.payload is uint8_t[], not a pointer). */
    EmbedIQ_Msg_t m;
    memset(&m, 0, sizeof(m));
    m.msg_id   = MSG_TEMP_READING;
    m.priority = EMBEDIQ_MSG_PRIORITY_NORMAL;

    MSG_TEMP_READING_Payload_t reading;
    reading.temperature_c = g_temp_c;
    memcpy(m.payload, &reading, sizeof(reading));
    m.payload_len = (uint16_t)sizeof(reading);

    embediq_publish(fb, &m);
}

/* ── FB init: register the sub-function (R-sub-08) ──────────────── */
static void sensor_init(EmbedIQ_FB_Handle_t fb, void *fb_data)
{
    (void)fb_data;

    static const uint16_t k_subfn_subs[] = { MSG_TIMER_1SEC };
    static EmbedIQ_SubFn_Config_t k_subfn = {
        .name               = "sensor_tick",
        .init_order         = 1u,
        .init_fn            = NULL,
        .run_fn             = on_tick,
        .exit_fn            = NULL,
        .subscriptions      = k_subfn_subs,
        .subscription_count = 1u,
        .subfn_data         = NULL,
        .fsm                = NULL,
        .osal_signal        = NULL,   /* Application FBs: always NULL (R-sub-11) */
    };
    embediq_subfn_register(fb, &k_subfn);
}

/* ── FB registration ────────────────────────────────────────────── */
EMBEDIQ_SUBS(g_sensor_subs, MSG_TIMER_1SEC);
EMBEDIQ_PUBS(g_sensor_pubs, MSG_TEMP_READING);

EmbedIQ_FB_Handle_t fb_sensor_temp_register(void)
{
    static const EmbedIQ_FB_Config_t k_cfg = {
        .name               = "fb_sensor_temp",
        .boot_phase         = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .init_fn            = sensor_init,
        .subscriptions      = g_sensor_subs,
        .subscription_count = 1u,
        .publications       = g_sensor_pubs,
        .publication_count  = 1u,
    };
    return embediq_fb_register(&k_cfg);
}
```

**What CI will check on this file:**

| Rule        | What it catches                          | Script              |
|-------------|------------------------------------------|---------------------|
| I-07 / R-02 | `malloc`, `free`, `calloc`, `realloc`    | `check_fb_calls.py` |
| R-sub-03    | Direct OSAL / RTOS / POSIX threading     | `check_fb_calls.py` |
| R-sub-03a   | `sleep`, `usleep`, `embediq_osal_delay_*`| `check_fb_calls.py` |
| Layer       | Out-of-layer `#include`                  | `boundary_checker.py` |
| I-08        | Hardcoded sizing constants               | `validator.py`      |
| SPDX        | Missing licence header                   | `licence_check.py`  |

### 4.4 Wire it into main

In your application's main (use `examples/thermostat/main.c` as reference),
call `fb_sensor_temp_register()` alongside the other Phase-3 registrations,
then boot the framework and start the dispatch threads:

```c
#include "fb_sensor_temp.h"

fb_timer_register();              /* Phase 1 — platform */
/* ...other Phase-2 / Phase-3 FBs... */
fb_sensor_temp_register();        /* Phase 3 — application */

if (embediq_engine_boot() != 0) return 1;
embediq_engine_dispatch_boot();   /* spawns one thread per FB with subs */
```

### 4.5 Build and run

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DEMBEDIQ_PLATFORM=host
cmake --build build
./build/examples/thermostat/embediq_thermostat
```

Observatory output is emitted on stdout by default. You will see lifecycle
events for every FB, every `MSG_TIMER_1SEC` delivered to your FB, and every
`MSG_TEMP_READING` your FB publishes — with zero developer instrumentation
code.

---

## 5. Common mistakes and what catches them

### Mistake: blocking inside a handler

```c
/* ✗ WRONG — blocks the dispatch thread for this FB */
static void on_tick(EmbedIQ_FB_Handle_t fb, const void *msg,
                    void *fb_data, void *subfn_data) {
    usleep(100 * 1000);   /* never do this */
}
```

**What happens:** the dispatch thread sleeps for 100 ms. All queued messages
for this FB pile up. If you subscribe to `MSG_TIMER_1MS`, the NORMAL queue
fills in ~32 ms and the framework starts dropping oldest (observable event
emitted — silent data loss to the consumer).

**What CI does:** `check_fb_calls.py` exits non-zero. PR is blocked.

**Right pattern:** subscribe to the timer message at the rate you need.
For a 100 ms action, subscribe to `MSG_TIMER_100MS` — not sleep.

### Mistake: calling malloc in a handler

```c
/* ✗ WRONG — dynamic allocation forbidden in Layer 1/2 (I-07, R-02) */
static void on_tick(...) {
    uint8_t *buf = malloc(256);
}
```

**What CI does:** `check_fb_calls.py` exits non-zero.

**Right pattern:** static module state, an `fb_data` struct, or a bounded
stack buffer (small, known size).

### Mistake: direct OSAL or RTOS call

```c
/* ✗ WRONG — framework owns all task lifecycle (R-sub-03) */
static void sensor_init(EmbedIQ_FB_Handle_t fb, void *fb_data) {
    embediq_osal_task_create(...);
    xTaskCreate(...);
}
```

**What CI does:** `check_fb_calls.py` exits non-zero.

**Right pattern:** register all FBs before `embediq_engine_boot()`; let
`embediq_engine_dispatch_boot()` create the dispatch threads.

### Mistake: treating `EmbedIQ_Msg_t.payload` as a pointer

```c
/* ✗ WRONG — payload is uint8_t[EMBEDIQ_MSG_MAX_PAYLOAD], not a pointer */
EmbedIQ_Msg_t m;
m.payload = &my_struct;    /* will not compile */
```

**Right pattern:** `memcpy(m.payload, &my_struct, sizeof(my_struct));`
and set `m.payload_len = (uint16_t)sizeof(my_struct);`.

### Mistake: missing SPDX header

**What CI does:** `licence_check.py` exits non-zero.

**Right pattern:** every `.h` and `.c` file starts with the standard
header block including `@author`, `@company`, and
`SPDX-License-Identifier: Apache-2.0`.

---

## 6. Run all checks locally before pushing

```bash
bash scripts/check.sh
```

This runs the identical check suite that GitHub Actions runs — build,
the full ctest suite, boundary checker, FB call enforcement, SPDX check,
and the rest. If it passes locally, CI passes.

---

## 7. Next steps

- Read `CODING_RULES.md` for the complete rule set with rationale
- Read `AGENTS.md` for the layer model and what each layer may include
- See `examples/thermostat/` for a complete multi-FB application with
  FSM-driven state transitions and a watchdog service
- See `examples/gateway/` for a Service FB with external connectivity
