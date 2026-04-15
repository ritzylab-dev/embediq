#define _POSIX_C_SOURCE 200809L

/*
 * tests/unit/test_hal_obs.c — Behavioral tests for HAL Observatory fault emission
 *
 * Verifies that HAL functions on error paths emit EMBEDIQ_OBS_EVT_HAL_FAULT
 * to the Observatory ring (XOBS-2, PR #93), and that the obs_stream write-error
 * latch suppresses repeat emissions while allowing recovery on open/close (PR #94).
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_obs.h"
#include "hal_defs.h"
#include "hal_flash.h"
#include "hal_timer.h"
#include "hal_wdg.h"
#include "hal_obs_stream.h"

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* Observatory internal API (host builds only — EMBEDIQ_PLATFORM_HOST) */
extern void     obs__reset(void);
extern uint32_t obs__ring_count(void);
extern bool     obs__ring_read(uint32_t idx, EmbedIQ_Event_t *out);

/* Stream registration (hal_obs_stream_posix.c) */
extern void embediq_obs_stream_posix_register(void);

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
 * /dev/full behavioral probe.
 *
 * Checks both that /dev/full can be opened AND that writes to it actually
 * fail. Some container environments (e.g. GitHub Actions ubuntu-latest)
 * have a /dev/full that accepts writes rather than returning ENOSPC. In
 * those environments the latch tests cannot run correctly and must skip.
 *
 * setvbuf(_IONBF) matches the production HAL behaviour set in
 * hal_obs_stream_open(), ensuring the probe reflects what the tests
 * will actually experience.
 * ------------------------------------------------------------------------- */
static bool dev_full_available(void)
{
    FILE *f = fopen("/dev/full", "wb");
    if (!f) return false;
    /* Mirror production HAL: disable buffering so writes go to OS directly */
    (void)setvbuf(f, NULL, _IONBF, 0);
    uint8_t probe = 0xAAu;
    size_t written = fwrite(&probe, 1u, 1u, f);
    fclose(f);
    /* /dev/full is only usable if writes fail (ENOSPC behaviour confirmed).
     * If written == 1 the device accepted the byte — not a true /dev/full. */
    return (written == 0u);
}

/* ---------------------------------------------------------------------------
 * XOBS-2 test 1: HAL flash emits FAULT on NULL read buffer
 * ------------------------------------------------------------------------- */

static void test_hal_fault_flash_null_read_buf(void)
{
    obs__reset();
    hal_flash_read(0u, NULL, 1u);

    EmbedIQ_Event_t evt = {0};
    ASSERT(obs__ring_count() >= 1u,
           "flash_read with NULL buf must emit at least one FAULT event");
    obs__ring_read(0u, &evt);
    ASSERT(evt.event_type    == EMBEDIQ_OBS_EVT_HAL_FAULT,
           "event_type must be EMBEDIQ_OBS_EVT_HAL_FAULT (0x67)");
    ASSERT(evt.source_fb_id  == EMBEDIQ_HAL_SRC_FLASH,
           "source_fb_id must be EMBEDIQ_HAL_SRC_FLASH");
    ASSERT(evt.state_or_flag == (uint8_t)(-(int)HAL_ERR_INVALID),
           "state_or_flag must encode HAL_ERR_INVALID");
}

/* ---------------------------------------------------------------------------
 * XOBS-2 test 2: HAL flash emits FAULT on NULL write buffer
 * ------------------------------------------------------------------------- */

static void test_hal_fault_flash_null_write_buf(void)
{
    obs__reset();
    hal_flash_write(0u, NULL, 1u);

    EmbedIQ_Event_t evt = {0};
    ASSERT(obs__ring_count() >= 1u,
           "flash_write with NULL buf must emit at least one FAULT event");
    obs__ring_read(0u, &evt);
    ASSERT(evt.event_type    == EMBEDIQ_OBS_EVT_HAL_FAULT,
           "event_type must be EMBEDIQ_OBS_EVT_HAL_FAULT");
    ASSERT(evt.source_fb_id  == EMBEDIQ_HAL_SRC_FLASH,
           "source_fb_id must be EMBEDIQ_HAL_SRC_FLASH");
    ASSERT(evt.state_or_flag == (uint8_t)(-(int)HAL_ERR_INVALID),
           "state_or_flag must encode HAL_ERR_INVALID");
}

/* ---------------------------------------------------------------------------
 * XOBS-2 test 3: HAL timer emits FAULT on NULL callback
 * ------------------------------------------------------------------------- */

static void test_hal_fault_timer_null_callback(void)
{
    obs__reset();
    hal_timer_init(1000u, NULL, NULL);

    EmbedIQ_Event_t evt = {0};
    ASSERT(obs__ring_count() >= 1u,
           "timer_init with NULL cb must emit at least one FAULT event");
    obs__ring_read(0u, &evt);
    ASSERT(evt.event_type    == EMBEDIQ_OBS_EVT_HAL_FAULT,
           "event_type must be EMBEDIQ_OBS_EVT_HAL_FAULT");
    ASSERT(evt.source_fb_id  == EMBEDIQ_HAL_SRC_TIMER,
           "source_fb_id must be EMBEDIQ_HAL_SRC_TIMER");
    ASSERT(evt.state_or_flag == (uint8_t)(-(int)HAL_ERR_INVALID),
           "state_or_flag must encode HAL_ERR_INVALID");
}

/* ---------------------------------------------------------------------------
 * XOBS-2 test 4: HAL WDG emits FAULT on kick before init
 * MUST run first in main() — s_inited=0 at binary start.
 * ------------------------------------------------------------------------- */

static void test_hal_fault_wdg_kick_before_init(void)
{
    obs__reset();
    hal_wdg_kick();

    EmbedIQ_Event_t evt = {0};
    ASSERT(obs__ring_count() >= 1u,
           "wdg_kick before init must emit at least one FAULT event");
    obs__ring_read(0u, &evt);
    ASSERT(evt.event_type    == EMBEDIQ_OBS_EVT_HAL_FAULT,
           "event_type must be EMBEDIQ_OBS_EVT_HAL_FAULT");
    ASSERT(evt.source_fb_id  == EMBEDIQ_HAL_SRC_WDG,
           "source_fb_id must be EMBEDIQ_HAL_SRC_WDG");
    ASSERT(evt.state_or_flag == (uint8_t)(-(int)HAL_ERR_INVALID),
           "state_or_flag must encode HAL_ERR_INVALID");
}

/* ---------------------------------------------------------------------------
 * Latch test 1: NULL data write emits FAULT
 * ------------------------------------------------------------------------- */

static void test_obs_stream_null_data_emits_fault(void)
{
    hal_obs_stream_close();
    hal_obs_stream_open("/tmp/test_hal_obs_e.iqtrace");
    obs__reset();

    hal_obs_stream_write(NULL, 5u);

    EmbedIQ_Event_t evt = {0};
    ASSERT(obs__ring_count() >= 1u,
           "write with NULL data must emit at least one FAULT event");
    obs__ring_read(0u, &evt);
    ASSERT(evt.event_type    == EMBEDIQ_OBS_EVT_HAL_FAULT,
           "event_type must be EMBEDIQ_OBS_EVT_HAL_FAULT");
    ASSERT(evt.source_fb_id  == EMBEDIQ_HAL_SRC_OBS_STREAM,
           "source_fb_id must be EMBEDIQ_HAL_SRC_OBS_STREAM");

    hal_obs_stream_close();
}

/* ---------------------------------------------------------------------------
 * Latch test 2: first write failure emits FAULT; second is silent
 * Uses /dev/full — fopen succeeds, fwrite always fails.
 * ------------------------------------------------------------------------- */

static void test_obs_stream_latch_suppresses_second_fault(void)
{
    /* /dev/full is Linux-only — skip gracefully on macOS */
    if (!dev_full_available()) {
        printf("SKIP  %s  (/dev/full not openable on this platform)\n", __func__);
        return;
    }

    hal_obs_stream_close();
    obs__reset();                   /* clear ring BEFORE opening /dev/full */

    int ret = hal_obs_stream_open("/dev/full");
    ASSERT(ret == HAL_OK, "/dev/full must open successfully");

    static const uint8_t buf[4] = {0x01u, 0x02u, 0x03u, 0x04u};

    /* First write — fwrite fails on /dev/full → latch set, one FAULT emitted */
    hal_obs_stream_write(buf, (uint16_t)sizeof(buf));
    ASSERT(obs__ring_count() == 1u,
           "first write failure must emit exactly one FAULT event");

    /* Second write — latch active → silent discard, ring must not grow */
    hal_obs_stream_write(buf, (uint16_t)sizeof(buf));
    ASSERT(obs__ring_count() == 1u,
           "second write while latch set must not add to ring (storm suppressed)");

    hal_obs_stream_close();
}

/* ---------------------------------------------------------------------------
 * Latch test 3: open with valid path clears latch
 * ------------------------------------------------------------------------- */

static void test_obs_stream_latch_cleared_on_open(void)
{
    if (!dev_full_available()) {
        printf("SKIP  %s  (/dev/full not openable on this platform)\n", __func__);
        return;
    }

    hal_obs_stream_close();
    obs__reset();                   /* clear ring BEFORE opening /dev/full */

    hal_obs_stream_open("/dev/full");
    static const uint8_t buf[4] = {0xAAu, 0xBBu, 0xCCu, 0xDDu};
    hal_obs_stream_write(buf, (uint16_t)sizeof(buf));  /* sets latch */

    /* Reopen with a writable path — must clear the latch */
    int ret = hal_obs_stream_open("/tmp/test_hal_obs_g.iqtrace");
    ASSERT(ret == HAL_OK, "open with valid path must succeed");

    /* Capture ring count before clean write — latch cleared, no new fault */
    uint32_t count_before = obs__ring_count();
    int wr = hal_obs_stream_write(buf, (uint16_t)sizeof(buf));
    ASSERT(wr == HAL_OK,
           "write after open must succeed (latch cleared on open)");
    ASSERT(obs__ring_count() == count_before,
           "no FAULT must be emitted after latch cleared by open");

    hal_obs_stream_close();
}

/* ---------------------------------------------------------------------------
 * Latch test 4: close clears latch
 * ------------------------------------------------------------------------- */

static void test_obs_stream_latch_cleared_on_close(void)
{
    if (!dev_full_available()) {
        printf("SKIP  %s  (/dev/full not openable on this platform)\n", __func__);
        return;
    }

    hal_obs_stream_close();
    obs__reset();                   /* clear ring BEFORE opening /dev/full */

    hal_obs_stream_open("/dev/full");
    static const uint8_t buf[4] = {0x11u, 0x22u, 0x33u, 0x44u};
    hal_obs_stream_write(buf, (uint16_t)sizeof(buf));  /* sets latch */

    hal_obs_stream_close();         /* must clear latch unconditionally */

    int ret = hal_obs_stream_open("/tmp/test_hal_obs_h.iqtrace");
    ASSERT(ret == HAL_OK, "open after close must succeed");

    /* Capture ring count before clean write — latch cleared, no new fault */
    uint32_t count_before = obs__ring_count();
    int wr = hal_obs_stream_write(buf, (uint16_t)sizeof(buf));
    ASSERT(wr == HAL_OK,
           "write after close+reopen must succeed (latch cleared on close)");
    ASSERT(obs__ring_count() == count_before,
           "no FAULT must be emitted after latch cleared by close");

    hal_obs_stream_close();
}

/* ---------------------------------------------------------------------------
 * Entry point
 * ------------------------------------------------------------------------- */

int main(void)
{
    embediq_obs_stream_posix_register();

    /* XOBS-2 behavioral tests — PR #93
     * test_hal_fault_wdg_kick_before_init MUST run first (s_inited=0 at start) */
    test_hal_fault_wdg_kick_before_init();
    test_hal_fault_flash_null_read_buf();
    test_hal_fault_flash_null_write_buf();
    test_hal_fault_timer_null_callback();

    /* Latch behavioral tests — PR #94 */
    test_obs_stream_null_data_emits_fault();
    test_obs_stream_latch_suppresses_second_fault();
    test_obs_stream_latch_cleared_on_open();
    test_obs_stream_latch_cleared_on_close();

    printf("\n");
    if (g_tests_failed == 0) {
        printf("All %d tests passed. (0 failed)\n", g_tests_run);
    } else {
        printf("%d of %d tests FAILED.\n", g_tests_failed, g_tests_run);
    }
    return (g_tests_failed > 0) ? 1 : 0;
}
