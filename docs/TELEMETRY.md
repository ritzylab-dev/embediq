# Using fb_telemetry — Metric Reporting

fb_telemetry is an Apache 2.0 Service FB that provides OTel-aligned metric
collection. Any application FB publishes a typed metric message to the bus.
fb_telemetry aggregates all reports within a 30-second window and publishes
a single MSG_TELEMETRY_BATCH at the window boundary. A downstream FB
(fb_cloud_mqtt, a logger, or your own handler) subscribes to the batch.

fb_telemetry owns no hardware and calls no HAL. It runs on any target
unchanged — POSIX, FreeRTOS, or ESP32.

---

## 1. Wire fb_telemetry in main.c

Register `fb_telemetry` before `embediq_engine_boot()`. Optionally attach
static tags (device ID, firmware version) that will be associated with
every batch your backend consumes.

```c
#include "embediq_fb.h"
#include "fb_telemetry.h"

/* Driver FB registrations declared elsewhere */
extern EmbedIQ_FB_Handle_t fb_timer_register(void);
extern EmbedIQ_FB_Handle_t fb_nvm_register(void);

/* Your application FBs */
extern void app_motor_ctrl_register(void);
extern void app_comms_register(void);

int main(void)
{
    /* Phase 1 — platform */
    fb_timer_register();

    /* Phase 2 — infrastructure services */
    fb_nvm_register();

    /* Telemetry collector — must be registered before any FB that
     * publishes MSG_TELEMETRY_* so its sub-functions are in place
     * by the time the first metric arrives on the bus. */
    fb_telemetry_register();
    fb_telemetry_set_tag("device.id",  "machine-0042");
    fb_telemetry_set_tag("fw.version", "1.4.2");

    /* Phase 3 — application */
    app_motor_ctrl_register();
    app_comms_register();

    if (embediq_engine_boot() != 0) return 1;
    embediq_engine_dispatch_boot();

    /* ... run loop ... */
    return 0;
}
```

`fb_telemetry_set_tag()` accepts up to `EMBEDIQ_TELEMETRY_MAX_TAGS` tags
(default 4). Both `key` and `value` are copied — the caller does not need
to keep them alive.

---

## 2. Publishing a gauge from your FB

A **gauge** is an instantaneous value: temperature, pressure, RPM, queue
depth. Each publish is one observation; fb_telemetry tracks
count/min/max/avg/last within the window.

Below, a motor-controller sub-function reports motor temperature every
100 ms. Define named constants for your `metric_id` values — never use
raw integers at call sites.

```c
#include "embediq_fb.h"
#include "embediq_bus.h"
#include "embediq_msg.h"
#include "embediq_telemetry.h"          /* brings in message IDs + unit enum */

#include <string.h>

/* Application-defined metric IDs. Keep them in a shared project header
 * so two FBs never assign the same ID to two different metrics. */
#define METRIC_MOTOR_TEMP   1u
#define METRIC_MOTOR_RPM    2u

static void on_sample_100ms(EmbedIQ_FB_Handle_t fb, const void *msg,
                            void *fb_data, void *subfn_data)
{
    (void)msg; (void)fb_data; (void)subfn_data;

    float temp_c = read_motor_temperature();   /* your HAL call */

    MSG_TELEMETRY_GAUGE_Payload_t p = {
        .metric_id = METRIC_MOTOR_TEMP,
        .value     = temp_c,
        .unit_id   = EMBEDIQ_UNIT_CELSIUS,
    };

    EmbedIQ_Msg_t m;
    memset(&m, 0, sizeof(m));
    m.msg_id      = MSG_TELEMETRY_GAUGE;
    m.priority    = EMBEDIQ_MSG_PRIORITY_NORMAL;
    memcpy(m.payload, &p, sizeof(p));
    m.payload_len = (uint16_t)sizeof(p);

    embediq_publish(fb, &m);
}
```

Unit identifiers are defined in `core/include/embediq_telemetry.h` as the
`EmbedIQ_Telemetry_Unit_t` enum: `EMBEDIQ_UNIT_PERCENT`, `_CELSIUS`, `_RPM`,
`_G`, `_DB`, `_KW`, `_BAR`, `_MS`, `_COUNT`, `_BYTES`, `_MV`, `_AMPERE`,
`_MPM`, `_LPM`, and `_UNKNOWN` (255). Pick the closest match — if nothing
fits, use `EMBEDIQ_UNIT_UNKNOWN` and clarify via the metric name on your
consumer side.

---

## 3. Publishing a counter

A **counter** reports a monotonic increment — bytes transmitted since the
last report, CRC errors since last report, frames dropped. Use counter (not
gauge) when the meaningful quantity is a rate or total, not an instantaneous
value.

```c
#define METRIC_UART_CRC_ERRORS  10u

static void on_frame_crc_fail(EmbedIQ_FB_Handle_t fb)
{
    MSG_TELEMETRY_COUNTER_Payload_t p = {
        .metric_id = METRIC_UART_CRC_ERRORS,
        .delta     = 1u,
        .unit_id   = EMBEDIQ_UNIT_COUNT,
    };

    EmbedIQ_Msg_t m;
    memset(&m, 0, sizeof(m));
    m.msg_id      = MSG_TELEMETRY_COUNTER;
    m.priority    = EMBEDIQ_MSG_PRIORITY_NORMAL;
    memcpy(m.payload, &p, sizeof(p));
    m.payload_len = (uint16_t)sizeof(p);

    embediq_publish(fb, &m);
}
```

fb_telemetry sums every `delta` seen within the window and reports the
running total in the batch. Use a gauge for "current depth"; use a counter
for "events since last report".

---

## 4. Publishing a histogram

A **histogram** records a single sample and lets fb_telemetry build a
distribution over the window (min, max, mean, count). Use it for latency
measurements, payload sizes, or anything where the distribution matters
more than the instant value.

```c
#define METRIC_DISPATCH_LATENCY_US  20u

static void on_msg_processed(EmbedIQ_FB_Handle_t fb, uint32_t latency_us)
{
    MSG_TELEMETRY_HISTOGRAM_Payload_t p = {
        .metric_id   = METRIC_DISPATCH_LATENCY_US,
        .observation = (float)latency_us,
        .unit_id     = EMBEDIQ_UNIT_MS,   /* or _UNKNOWN for µs; pick the closest */
    };

    EmbedIQ_Msg_t m;
    memset(&m, 0, sizeof(m));
    m.msg_id      = MSG_TELEMETRY_HISTOGRAM;
    m.priority    = EMBEDIQ_MSG_PRIORITY_NORMAL;
    memcpy(m.payload, &p, sizeof(p));
    m.payload_len = (uint16_t)sizeof(p);

    embediq_publish(fb, &m);
}
```

At the window boundary, fb_telemetry emits the distribution: min, max,
mean (sum / count), and the observation count for each histogram
`metric_id`. Individual samples are not retained.

---

## 5. What happens to the batch

fb_telemetry publishes `MSG_TELEMETRY_BATCH` at `EMBEDIQ_TELEMETRY_WINDOW_SEC`
(default 30 s) boundaries, driven by `MSG_TIMER_1SEC` ticks from `fb_timer`.
A batch message carries an 8-byte header (`MSG_TELEMETRY_BATCH_Payload_t`:
`window_start_s`, `window_dur_s`, `entry_count`, `flags`) followed by the
per-metric summary entries packed in the remaining payload bytes.

You choose what subscribes to `MSG_TELEMETRY_BATCH`:

- **`fb_cloud_mqtt`** (Phase 3, Apache 2.0) is the production cloud consumer.
- A **local logger FB** can persist batches to SD card or serial for
  long-running bench tests.
- In **dev/test builds** you can subscribe in your own FB and print the
  batch header — this confirms metrics are actually flowing to the
  aggregator before you integrate a real transport.

A minimal dev-mode subscriber that prints each batch as it arrives:

```c
#include "embediq_fb.h"
#include "embediq_subfn.h"
#include "embediq_msg.h"
#include "embediq_telemetry.h"          /* MSG_TELEMETRY_BATCH + header type */

#include <stdio.h>
#include <string.h>

static void on_telemetry_batch(EmbedIQ_FB_Handle_t fb, const void *msg,
                               void *fb_data, void *subfn_data)
{
    (void)fb; (void)fb_data; (void)subfn_data;
    const EmbedIQ_Msg_t *m = (const EmbedIQ_Msg_t *)msg;

    MSG_TELEMETRY_BATCH_Payload_t hdr;
    memcpy(&hdr, m->payload, sizeof(hdr));

    printf("[telemetry] window=%us dur=%us entries=%u flags=0x%02x\n",
           (unsigned)hdr.window_start_s, (unsigned)hdr.window_dur_s,
           (unsigned)hdr.entry_count,    (unsigned)hdr.flags);
}

/* Register in your dev-mode FB's init sub-function:
 *
 *   static const uint16_t subs[] = { MSG_TELEMETRY_BATCH };
 *   static EmbedIQ_SubFn_Config_t cfg = {
 *       .name = "dev_dump_batch",
 *       .run_fn = on_telemetry_batch,
 *       .subscriptions = subs, .subscription_count = 1u,
 *   };
 *   embediq_subfn_register(fb, &cfg);
 */
```

To decode the per-entry summaries after the header, cast the bytes after
the batch header to `EmbedIQ_Telemetry_Batch_Entry_t[hdr.entry_count]` —
the public packed 18-byte type in `core/include/embediq_telemetry.h`.
Each entry carries `metric_id`, `type` (use the `EmbedIQ_Telemetry_Entry_Type_t`
enum constants), `unit_id`, three float fields (`value_a` / `_b` / `_c` —
meaning depends on `type`; see the struct's doc comment), and the in-window
sample `count`.

---

## 6. Common mistakes

### Wrong boot phase assumption

fb_telemetry registers at `EMBEDIQ_BOOT_PHASE_INFRASTRUCTURE` (phase 2).
All application FBs run at `EMBEDIQ_BOOT_PHASE_APPLICATION` (phase 3). This
means fb_telemetry is always fully initialized before any application FB's
init sub-function runs. No registration-order precautions are required —
the boot phase ordering guarantees delivery.

### metric_id collisions — use named constants, not raw literals

`metric_id` is a `uint16_t` chosen by the application. Two FBs that both
publish with `metric_id = 1` will blend their observations in the batch.
Define your metric IDs as `#define METRIC_MOTOR_TEMP 1u` in a shared header
and reference the symbol at every call site. Never use raw integers inline,
and never recycle a `metric_id` for a different metric.

### Missing batch subscriber — `MSG_TELEMETRY_BATCH` isn't free-standing

The batch is published to the bus. If no FB subscribes to
`MSG_TELEMETRY_BATCH`, it is delivered nowhere and effectively dropped.
Before you have `fb_cloud_mqtt` or a persistent logger wired up, add a
dev-mode subscriber (see §5) to confirm your publishers are actually
reaching fb_telemetry and batches are being emitted.
