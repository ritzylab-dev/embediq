/*
 * fbs/services/fb_telemetry.h — Telemetry aggregation Service FB
 *
 * Subscribes to MSG_TELEMETRY_GAUGE / _COUNTER / _HISTOGRAM published by
 * any application FB; aggregates into a static table; publishes
 * MSG_TELEMETRY_BATCH at window boundaries (EMBEDIQ_TELEMETRY_WINDOW_SEC).
 *
 * Layer 2 Service FB — no HAL, no OSAL, no dynamic allocation.
 * Backend abstraction is the message bus itself: MQTT / logger / SD card
 * FBs subscribe to MSG_TELEMETRY_BATCH.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FB_TELEMETRY_H
#define FB_TELEMETRY_H

#include "embediq_fb.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Register fb_telemetry with the framework.
 * Call before embediq_engine_boot().
 */
EmbedIQ_FB_Handle_t fb_telemetry_register(void);

/**
 * Set a static tag attached to every MSG_TELEMETRY_BATCH published.
 * Call after fb_telemetry_register(), before embediq_engine_boot().
 *
 * Maximum EMBEDIQ_TELEMETRY_MAX_TAGS tags; silently ignored if full.
 * Both key and value are copied (truncated at EMBEDIQ_TELEMETRY_TAG_LEN-1,
 * NUL-terminated); the caller does not need to keep them alive.
 */
void fb_telemetry_set_tag(const char *key, const char *value);

#ifdef __cplusplus
}
#endif

#endif /* FB_TELEMETRY_H */
