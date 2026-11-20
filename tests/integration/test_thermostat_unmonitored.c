#define _POSIX_C_SOURCE 200809L

/*
 * tests/integration/test_thermostat_unmonitored.c — the thermostat controller
 * without a watchdog service
 *
 * The controller registers with the watchdog in its init_fn. Here the
 * thermostat FB set boots WITHOUT fb_watchdog, so the watchdog service is
 * never ready and that registration is refused. The controller must not run
 * as if protected: it reports its own fault (its "not monitored" reason)
 * through embediq_fb_report_fault(), and the engine executes it before the
 * FB would have been marked RUNNING.
 *
 * The thermostat FB set WITH fb_watchdog (test_thermostat_full,
 * test_thermostat_observable, test_thermostat_capture) keeps the controller
 * RUNNING; those tests are unchanged.
 *
 * Assertions:
 *   1. boot returns 0 — a refused registration is the controller's fault,
 *      not a boot failure
 *   2. the controller is in FAULT; the sensor stays RUNNING
 *   3. the engine recorded the controller's FAULT LIFECYCLE event
 *   4. the watchdog recorded the refused registration: one FAULT event with
 *      EMBEDIQ_FAULT_WDG_NOT_READY whose source is the controller
 *
 * The fault reason itself is not observable from outside: the LIFECYCLE
 * event carries no reason, and the controller has no fault_fn.
 *
 * Run:      ./build/tests/integration/test_thermostat_unmonitored
 * Expected: "All 4 tests passed. (0 failed)"
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_fb.h"
#include "embediq_obs.h"
#include "embediq_wdg.h"

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

extern void fb_engine__reset(void);
extern bool obs__ring_read(uint32_t idx, EmbedIQ_Event_t *out);
extern void obs__set_level(uint8_t level);

extern EmbedIQ_FB_Handle_t fb_temp_sensor_register(void);
extern EmbedIQ_FB_Handle_t fb_temp_controller_register(void);

static int g_tests_run    = 0;
static int g_tests_failed = 0;

#define ASSERT(cond, msg)  do {                                                \
    g_tests_run++;                                                             \
    if (!(cond)) {                                                             \
        fprintf(stderr, "FAIL  %s  %s:%d\n", (msg), __FILE__, __LINE__);       \
        g_tests_failed++;                                                      \
    } else {                                                                   \
        printf("PASS  %s\n", (msg));                                           \
    }                                                                          \
} while (0)

/* Events of type with state_or_flag == state from source src. */
static uint32_t events(uint8_t type, uint8_t state, uint8_t src)
{
    uint32_t n = 0u;
    EmbedIQ_Event_t e;
    for (uint32_t i = 0u; obs__ring_read(i, &e); i++) {
        if (e.event_type == type && e.state_or_flag == state && e.source_fb_id == src) n++;
    }
    return n;
}

int main(void)
{
    fb_engine__reset();
    obs__set_level(2u);
    embediq_obs_set_transport(EMBEDIQ_OBS_TRANSPORT_TCP);   /* ring only, no output */

    EmbedIQ_FB_Handle_t sensor = fb_temp_sensor_register();
    EmbedIQ_FB_Handle_t ctrl   = fb_temp_controller_register();
    uint8_t ctrl_ep = embediq_engine_fb_ep(ctrl);

    ASSERT(embediq_engine_boot() == 0, "boot returns 0 without fb_watchdog");
    ASSERT(embediq_fb_get_state(ctrl) == EMBEDIQ_FB_STATE_FAULT
           && embediq_fb_get_state(sensor) == EMBEDIQ_FB_STATE_RUNNING,
           "the controller is in FAULT; the sensor stays RUNNING");
    ASSERT(events(EMBEDIQ_OBS_EVT_LIFECYCLE, (uint8_t)EMBEDIQ_FB_STATE_FAULT, ctrl_ep) == 1u,
           "the engine recorded the controller's FAULT");
    ASSERT(events(EMBEDIQ_OBS_EVT_FAULT, (uint8_t)EMBEDIQ_FAULT_WDG_NOT_READY, ctrl_ep) == 1u,
           "the watchdog refused the controller's registration: not ready");

    fb_engine__reset();

    printf("\n");
    if (g_tests_failed == 0) {
        printf("All %d tests passed. (0 failed)\n", g_tests_run);
    } else {
        printf("%d of %d tests FAILED.\n", g_tests_failed, g_tests_run);
    }
    return (g_tests_failed > 0) ? 1 : 0;
}
