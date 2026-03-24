#define _POSIX_C_SOURCE 200809L

/*
 * tests/unit/test_observatory.c — Unit tests for the Observatory
 *
 * Tests ring buffer correctness, sequence monotonicity, overflow recovery,
 * level filtering, transport discard, and stdout format.
 *
 * Run:      ./build/tests/unit/test_observatory
 * Expected: "All N tests passed. (0 failed)"
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

static void test_level_0_captures_fault_and_system(void)
{
    obs__reset();
    obs__set_level(0u);

    embediq_obs_emit(EMBEDIQ_OBS_EVT_FAULT,     0u, 0xFFu, 3u, 0u);    /* pass (FAULT family) */
    embediq_obs_emit(EMBEDIQ_OBS_EVT_OVERFLOW,   0u, 0xFFu, 0u, 1u);   /* pass (SYSTEM family) */
    embediq_obs_emit(EMBEDIQ_OBS_EVT_LIFECYCLE,  0u, 0xFFu, 2u, 0u);   /* drop (STATE family) */
    embediq_obs_emit(EMBEDIQ_OBS_EVT_MSG_TX,     0u, 1u,    0u, 0x100u); /* drop (MESSAGE family) */

    ASSERT(obs__ring_count() == 2u,
           "level 0 must capture only FAULT and SYSTEM family events");
}

static void test_level_1_captures_message_and_state(void)
{
    obs__reset();
    obs__set_level(1u);

    embediq_obs_emit(EMBEDIQ_OBS_EVT_FAULT,     0u, 0xFFu, 3u, 0u);    /* pass (FAULT) */
    embediq_obs_emit(EMBEDIQ_OBS_EVT_OVERFLOW,   0u, 0xFFu, 0u, 1u);   /* pass (SYSTEM) */
    embediq_obs_emit(EMBEDIQ_OBS_EVT_LIFECYCLE,  0u, 0xFFu, 2u, 0u);   /* pass (STATE) */
    embediq_obs_emit(EMBEDIQ_OBS_EVT_MSG_TX,     0u, 1u,    0u, 0x100u); /* pass (MESSAGE) */
    embediq_obs_emit(EMBEDIQ_OBS_EVT_FSM_TRANS,  0u, 1u,    1u, 0x100u); /* pass (STATE) */

    ASSERT(obs__ring_count() == 5u,
           "level 1 must capture FAULT, SYSTEM, MESSAGE, and STATE families");
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
 * Event family band tests
 * ------------------------------------------------------------------------- */

static void test_event_family_macro(void)
{
    ASSERT(EMBEDIQ_OBS_EVT_FAMILY(EMBEDIQ_OBS_EVT_LIFECYCLE)  == EMBEDIQ_OBS_FAMILY_STATE,
           "LIFECYCLE must be in STATE family");
    ASSERT(EMBEDIQ_OBS_EVT_FAMILY(EMBEDIQ_OBS_EVT_MSG_TX)     == EMBEDIQ_OBS_FAMILY_MESSAGE,
           "MSG_TX must be in MESSAGE family");
    ASSERT(EMBEDIQ_OBS_EVT_FAMILY(EMBEDIQ_OBS_EVT_MSG_RX)     == EMBEDIQ_OBS_FAMILY_MESSAGE,
           "MSG_RX must be in MESSAGE family");
    ASSERT(EMBEDIQ_OBS_EVT_FAMILY(EMBEDIQ_OBS_EVT_FSM_TRANS)  == EMBEDIQ_OBS_FAMILY_STATE,
           "FSM_TRANS must be in STATE family");
    ASSERT(EMBEDIQ_OBS_EVT_FAMILY(EMBEDIQ_OBS_EVT_FAULT)      == EMBEDIQ_OBS_FAMILY_FAULT,
           "FAULT must be in FAULT family");
    ASSERT(EMBEDIQ_OBS_EVT_FAMILY(EMBEDIQ_OBS_EVT_QUEUE_DROP) == EMBEDIQ_OBS_FAMILY_MESSAGE,
           "QUEUE_DROP must be in MESSAGE family");
    ASSERT(EMBEDIQ_OBS_EVT_FAMILY(EMBEDIQ_OBS_EVT_OVERFLOW)   == EMBEDIQ_OBS_FAMILY_SYSTEM,
           "OVERFLOW must be in SYSTEM family");
}

static void test_event_type_values_in_band(void)
{
    /* SYSTEM band: 0x10–0x1F */
    ASSERT(EMBEDIQ_OBS_EVT_OVERFLOW >= EMBEDIQ_OBS_BAND_SYSTEM_START &&
           EMBEDIQ_OBS_EVT_OVERFLOW <  EMBEDIQ_OBS_BAND_SYSTEM_START + EMBEDIQ_OBS_BAND_STRIDE,
           "OVERFLOW must be within SYSTEM band");

    /* MESSAGE band: 0x20–0x2F */
    ASSERT(EMBEDIQ_OBS_EVT_MSG_TX >= EMBEDIQ_OBS_BAND_MESSAGE_START &&
           EMBEDIQ_OBS_EVT_MSG_TX <  EMBEDIQ_OBS_BAND_MESSAGE_START + EMBEDIQ_OBS_BAND_STRIDE,
           "MSG_TX must be within MESSAGE band");
    ASSERT(EMBEDIQ_OBS_EVT_MSG_RX >= EMBEDIQ_OBS_BAND_MESSAGE_START &&
           EMBEDIQ_OBS_EVT_MSG_RX <  EMBEDIQ_OBS_BAND_MESSAGE_START + EMBEDIQ_OBS_BAND_STRIDE,
           "MSG_RX must be within MESSAGE band");
    ASSERT(EMBEDIQ_OBS_EVT_QUEUE_DROP >= EMBEDIQ_OBS_BAND_MESSAGE_START &&
           EMBEDIQ_OBS_EVT_QUEUE_DROP <  EMBEDIQ_OBS_BAND_MESSAGE_START + EMBEDIQ_OBS_BAND_STRIDE,
           "QUEUE_DROP must be within MESSAGE band");

    /* STATE band: 0x30–0x3F */
    ASSERT(EMBEDIQ_OBS_EVT_LIFECYCLE >= EMBEDIQ_OBS_BAND_STATE_START &&
           EMBEDIQ_OBS_EVT_LIFECYCLE <  EMBEDIQ_OBS_BAND_STATE_START + EMBEDIQ_OBS_BAND_STRIDE,
           "LIFECYCLE must be within STATE band");
    ASSERT(EMBEDIQ_OBS_EVT_FSM_TRANS >= EMBEDIQ_OBS_BAND_STATE_START &&
           EMBEDIQ_OBS_EVT_FSM_TRANS <  EMBEDIQ_OBS_BAND_STATE_START + EMBEDIQ_OBS_BAND_STRIDE,
           "FSM_TRANS must be within STATE band");

    /* FAULT band: 0x60–0x6F */
    ASSERT(EMBEDIQ_OBS_EVT_FAULT >= EMBEDIQ_OBS_BAND_FAULT_START &&
           EMBEDIQ_OBS_EVT_FAULT <  EMBEDIQ_OBS_BAND_FAULT_START + EMBEDIQ_OBS_BAND_STRIDE,
           "FAULT must be within FAULT band");
}

/* ---------------------------------------------------------------------------
 * Compile-time trace level tests
 * ------------------------------------------------------------------------- */

static void test_trace_level_defaults(void)
{
#ifdef EMBEDIQ_PLATFORM_HOST
    ASSERT(EMBEDIQ_TRACE_LEVEL == 1, "host default trace level must be 1");
#else
    ASSERT(EMBEDIQ_TRACE_LEVEL == 0, "MCU default trace level must be 0");
#endif
}

static void test_per_family_flags_from_level_1(void)
{
    ASSERT(EMBEDIQ_TRACE_MESSAGE  == 1, "MESSAGE enabled at level 1");
    ASSERT(EMBEDIQ_TRACE_STATE    == 1, "STATE enabled at level 1");
    ASSERT(EMBEDIQ_TRACE_RESOURCE == 0, "RESOURCE disabled at level 1");
    ASSERT(EMBEDIQ_TRACE_TIMING   == 0, "TIMING disabled at level 1");
    ASSERT(EMBEDIQ_TRACE_FUNCTION == 0, "FUNCTION disabled at level 1");
}

static void test_emit_macro_state_family(void)
{
    obs__reset();
    EMBEDIQ_OBS_EMIT_STATE(EMBEDIQ_OBS_EVT_LIFECYCLE, 0u, 0xFFu, 2u, 0u);
    ASSERT(obs__ring_count() == 1u,
           "EMBEDIQ_OBS_EMIT_STATE must emit when TRACE_STATE enabled");
}

static void test_emit_macro_message_family(void)
{
    obs__reset();
    EMBEDIQ_OBS_EMIT_MESSAGE(EMBEDIQ_OBS_EVT_MSG_TX, 0u, 1u, 0u, 0x100u);
    ASSERT(obs__ring_count() == 1u,
           "EMBEDIQ_OBS_EMIT_MESSAGE must emit when TRACE_MESSAGE enabled");
}

/* ---------------------------------------------------------------------------
 * Session metadata tests
 * ------------------------------------------------------------------------- */

static void test_session_begin_stores_fields(void)
{
    EmbedIQ_Obs_Session_t s = {
        .device_id         = 0xDEADBEEFu,
        .fw_version        = EMBEDIQ_OBS_FW_VERSION(1, 2, 3),
        .timestamp_base_us = 1234567890000000ULL,
        .session_id        = 7u,
        .platform_id       = EMBEDIQ_OBS_PLATFORM_POSIX,
        .trace_level       = 1u,
        ._pad              = {0, 0},
        .build_id          = "deadbeef",
    };
    obs__reset();
    embediq_obs_session_begin(&s);
    const EmbedIQ_Obs_Session_t *got = embediq_obs_session_get();
    ASSERT(got != NULL,                              "session_get must not be NULL after begin");
    ASSERT(got->device_id == 0xDEADBEEFu,           "device_id must match");
    ASSERT(got->fw_version == EMBEDIQ_OBS_FW_VERSION(1,2,3), "fw_version must match");
    ASSERT(got->timestamp_base_us == 1234567890000000ULL, "timestamp_base_us must match");
    ASSERT(got->session_id == 7u,                   "session_id must match");
    ASSERT(got->platform_id == EMBEDIQ_OBS_PLATFORM_POSIX, "platform_id must match");
    ASSERT(got->trace_level == 1u,                  "trace_level must match");
    ASSERT(got->build_id[0] == 'd',                 "build_id first char must match");
    ASSERT(got->build_id[7] == 'f',                 "build_id last char must match");
}

static void test_session_get_null_before_begin(void)
{
    obs__reset();
    ASSERT(embediq_obs_session_get() == NULL,
           "session_get must return NULL before session_begin is called");
}

static void test_session_begin_overwrites_previous(void)
{
    EmbedIQ_Obs_Session_t s1 = { .device_id = 1u, .session_id = 1u };
    EmbedIQ_Obs_Session_t s2 = { .device_id = 2u, .session_id = 2u };
    obs__reset();
    embediq_obs_session_begin(&s1);
    embediq_obs_session_begin(&s2);
    const EmbedIQ_Obs_Session_t *got = embediq_obs_session_get();
    ASSERT(got->device_id  == 2u, "second begin must overwrite device_id");
    ASSERT(got->session_id == 2u, "second begin must overwrite session_id");
}

static void test_fw_version_macro(void)
{
    uint32_t v = EMBEDIQ_OBS_FW_VERSION(2, 5, 11);
    ASSERT((v >> 16) == 2u,        "major must be in bits 31:16");
    ASSERT(((v >> 8) & 0xFFu) == 5u,  "minor must be in bits 15:8");
    ASSERT((v & 0xFFu) == 11u,     "patch must be in bits 7:0");
}

/* ---------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */

int main(void)
{
    test_emit_increments_sequence();
    test_sequence_monotonic();
    test_ring_overflow_recovery();
    test_level_0_captures_fault_and_system();
    test_level_1_captures_message_and_state();
    test_stdout_format_contains_seq();
    test_null_transport_discards_all();
    test_event_family_macro();
    test_event_type_values_in_band();
    test_trace_level_defaults();
    test_per_family_flags_from_level_1();
    test_emit_macro_state_family();
    test_emit_macro_message_family();
    test_session_get_null_before_begin();
    test_session_begin_stores_fields();
    test_session_begin_overwrites_previous();
    test_fw_version_macro();

    printf("\n");
    if (g_tests_failed == 0) {
        printf("All %d tests passed. (0 failed)\n", g_tests_run);
    } else {
        printf("%d of %d tests FAILED.\n", g_tests_failed, g_tests_run);
    }
    return (g_tests_failed > 0) ? 1 : 0;
}
