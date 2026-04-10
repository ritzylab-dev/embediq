/*
 * embediq_test.h — Test harness public API
 *
 * Declares bus_inject(), EMBEDIQ_TEST_SOURCE_ID, and the scenario runner.
 * All declarations are guarded by EMBEDIQ_PLATFORM_HOST — these symbols
 * must never appear in production (MCU / FreeRTOS) builds.
 *
 * bus_inject() is the only supported way to inject test messages onto the
 * bus without a registered FB handle. It stamps source_endpoint_id with
 * EMBEDIQ_TEST_SOURCE_ID (0xFEu) — a Reserved-range sentinel that is
 * distinct from all registered FB endpoint indices (0x00–0xBF) and from
 * the NULL/broadcast sentinel 0xFF.
 *
 * I-09: This header does not include bridge headers.
 * R-03: C11.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EMBEDIQ_TEST_H
#define EMBEDIQ_TEST_H

#include "embediq_msg.h"
#include <stddef.h>

#ifdef EMBEDIQ_PLATFORM_HOST

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Test source endpoint sentinel
 *
 * Value 0xFEu is in the 0xF0–0xFF Reserved range of the source_fb_id
 * namespace. It is distinct from 0xFF (NULL / broadcast sentinel) and
 * from all valid FB endpoint indices (0x00–0xBF).
 * ------------------------------------------------------------------------- */
#define EMBEDIQ_TEST_SOURCE_ID  ((uint8_t)0xFEu)

/* ---------------------------------------------------------------------------
 * bus_inject — inject a message onto the bus without a registered FB handle
 *
 * Sets msg->source_endpoint_id = EMBEDIQ_TEST_SOURCE_ID and routes the
 * message through the subscription table via route_message() — the same
 * routing path used by embediq_publish(), minus the source stamping step.
 *
 * msg is modified in place (source_endpoint_id is stamped) then copied
 * by value into the recipient's priority queue, consistent with the
 * copy-by-value contract of the bus.
 *
 * Preconditions: message_bus_boot() must have been called.
 * Thread safety: safe to call from test threads (POSIX host build only).
 * ------------------------------------------------------------------------- */
void bus_inject(EmbedIQ_Msg_t *msg);

/* ---------------------------------------------------------------------------
 * Scenario runner — deterministic replay of message sequences
 * ------------------------------------------------------------------------- */

/**
 * One step in a test scenario.
 * Extend this struct in future if per-step timing or assertions are needed.
 */
typedef struct {
    EmbedIQ_Msg_t msg; /**< Message to inject for this step. */
} embediq_test_scenario_step_t;

/**
 * Replay a sequence of scenario steps in order.
 * For each step, calls bus_inject(&step.msg) in array order.
 * steps must remain valid for the duration of the call.
 */
void embediq_test_run_scenario(const embediq_test_scenario_step_t *steps,
                               size_t count);

#ifdef __cplusplus
}
#endif

#endif /* EMBEDIQ_PLATFORM_HOST */

#endif /* EMBEDIQ_TEST_H */
