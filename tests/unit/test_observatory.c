#define _POSIX_C_SOURCE 200809L

/*
 * tests/unit/test_observatory.c — Unit tests for the Observatory
 *
 * Tests ring buffer correctness, sequence monotonicity, overflow recovery,
 * level filtering, transport discard, and stdout format.
 *
 * Run:      ./build/tests/unit/test_observatory
 * Expected: "All 7 tests passed. (0 failed)"
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_obs.h"
#include "embediq_config.h"

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Package-internal Observatory API (EMBEDIQ_PLATFORM_HOST)
 * ------------------------------------------------------------------------- */

extern void     obs__reset(void);
extern uint32_t obs__event_count(void);
extern uint32_t obs__ring_count(void);
extern bool     obs__ring_read(uint32_t idx, EmbedIQ_Event_t *out);
extern void     obs__set_level(uint8_t level);
extern void     obs__force_overflow(uint32_t count);
extern void     obs__format_event(const EmbedIQ_Event_t *evt, char *buf, size_t n);

/* ---------------------------------------------------------------------------
 * Minimal test harness
 * ------------------------------------------------------------------------- */

static int g_tests_run    = 0;
static int g_tests_failed = 0;

#define ASSERT(cond, msg)  do {                                                \
    g_tests_run++;                                                             \
    if (!(cond)) {                                                             \
        fprintf(stderr, "FAIL  %-56s  %s:%d\n", __func__, __FILE__, __LINE__);\
        g_tests_failed++;                                                      \
    } else {                                                                   \
        printf("PASS  %s\n", __func__);                                        \
    }                                                                          \
} while (0)

/* ---------------------------------------------------------------------------
 * Tests
 * ------------------------------------------------------------------------- */

static void test_emit_increments_sequence(void)
{
    obs__reset();

    embediq_obs_emit(EMBEDIQ_OBS_EVT_LIFECYCLE, 0u, 0xFFu, 2u, 0u);
    embediq_obs_emit(EMBEDIQ_OBS_EVT_LIFECYCLE, 1u, 0xFFu, 2u, 0u);

    EmbedIQ_Event_t e0 = {0}, e1 = {0};
    obs__ring_read(0u, &e0);
    obs__ring_read(1u, &e1);

    ASSERT(e0.sequence == 0u, "first event must have sequence 0");
    ASSERT(e1.sequence == 1u, "second event must have sequence 1");
}

static void test_sequence_monotonic(void)
{
    obs__reset();

    embediq_obs_emit(EMBEDIQ_OBS_EVT_LIFECYCLE, 0u, 0xFFu, 2u, 0u);
    embediq_obs_emit(EMBEDIQ_OBS_EVT_LIFECYCLE, 1u, 0xFFu, 2u, 0u);

    EmbedIQ_Event_t e0 = {0}, e1 = {0};
    obs__ring_read(0u, &e0);
    obs__ring_read(1u, &e1);

    ASSERT(e1.sequence == e0.sequence + 1u,
           "sequence must be monotonically increasing by 1");
}

static void test_ring_overflow_recovery(void)
{
    obs__reset();

    /* Inject a simulated overflow of 5 dropped events, then emit one real
     * event.  The emit call must first write an OVERFLOW recovery event
     * carrying the dropped count, then write the real event. */
    obs__force_overflow(5u);
    embediq_obs_emit(EMBEDIQ_OBS_EVT_LIFECYCLE, 0u, 0xFFu, 2u, 0u);

    EmbedIQ_Event_t ovf = {0}, lc = {0};
    obs__ring_read(0u, &ovf);
    obs__ring_read(1u, &lc);

    ASSERT(obs__ring_count() == 2u,
           "ring must hold exactly 2 events after overflow recovery");
    ASSERT(ovf.event_type == EMBEDIQ_OBS_EVT_OVERFLOW,
           "first event after overflow must be OVERFLOW type");
    ASSERT(ovf.msg_id == 5u,
           "OVERFLOW event must encode dropped count in msg_id");
    ASSERT(lc.event_type == EMBEDIQ_OBS_EVT_LIFECYCLE,
           "second event must be the original emitted event");
}

static void test_level_0_only_captures_lifecycle(void)
{
    obs__reset();
    obs__set_level(0u);

    embediq_obs_emit(EMBEDIQ_OBS_EVT_LIFECYCLE, 0u, 0xFFu, 2u, 0u);  /* pass  */
    embediq_obs_emit(EMBEDIQ_OBS_EVT_MSG_TX,    0u, 1u,    0u, 0x100u); /* drop */
    embediq_obs_emit(EMBEDIQ_OBS_EVT_FSM_TRANS, 0u, 1u,    1u, 0x100u); /* drop */

    ASSERT(obs__ring_count() == 1u,
           "level 0 must capture only LIFECYCLE and FAULT events");
}

static void test_level_1_captures_msg_tx(void)
{
    obs__reset();
    obs__set_level(1u);

    embediq_obs_emit(EMBEDIQ_OBS_EVT_LIFECYCLE, 0u, 0xFFu, 2u, 0u);    /* pass */
    embediq_obs_emit(EMBEDIQ_OBS_EVT_MSG_TX,    0u, 1u,    0u, 0x100u);  /* pass */
    embediq_obs_emit(EMBEDIQ_OBS_EVT_MSG_RX,    0u, 1u,    0u, 0x100u);  /* drop */
    embediq_obs_emit(EMBEDIQ_OBS_EVT_FSM_TRANS, 0u, 1u,    1u, 0x100u);  /* drop */

    ASSERT(obs__ring_count() == 2u,
           "level 1 must capture LIFECYCLE, FAULT, and MSG_TX only");
}

static void test_stdout_format_contains_seq(void)
{
    obs__reset();

    embediq_obs_emit(EMBEDIQ_OBS_EVT_LIFECYCLE, 0u, 0xFFu, 2u, 0u);

    EmbedIQ_Event_t evt = {0};
    obs__ring_read(0u, &evt);

    char buf[EMBEDIQ_OBS_RING_DEPTH];
    obs__format_event(&evt, buf, sizeof(buf));

    ASSERT(strstr(buf, "seq=") != NULL,
           "formatted event must contain 'seq=' field");
}

static void test_null_transport_discards_all(void)
{
    obs__reset();
    embediq_obs_set_transport(EMBEDIQ_OBS_TRANSPORT_NULL);

    embediq_obs_emit(EMBEDIQ_OBS_EVT_LIFECYCLE, 0u, 0xFFu, 2u, 0u);

    ASSERT(obs__ring_count() == 0u,
           "NULL transport must not write to ring");
    ASSERT(obs__event_count() == 0u,
           "NULL transport must not increment event counter");
}

/* ---------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */

int main(void)
{
    test_emit_increments_sequence();
    test_sequence_monotonic();
    test_ring_overflow_recovery();
    test_level_0_only_captures_lifecycle();
    test_level_1_captures_msg_tx();
    test_stdout_format_contains_seq();
    test_null_transport_discards_all();

    printf("\n");
    if (g_tests_failed == 0) {
        printf("All %d tests passed. (0 failed)\n", g_tests_run);
    } else {
        printf("%d of %d tests FAILED.\n", g_tests_failed, g_tests_run);
    }
    return (g_tests_failed > 0) ? 1 : 0;
}
