#define _POSIX_C_SOURCE 200809L

/*
 * examples/thermostat/main.c — EmbedIQ Smart Thermostat Demo
 *
 * Phase 1 gate demo: five Functional Blocks working together.
 *
 *   Phase 1 — PLATFORM
 *     fb_timer       Publishes MSG_TIMER_1SEC every second
 *
 *   Phase 2 — INFRASTRUCTURE
 *     fb_nvm         Persistent key-value store
 *     fb_watchdog    Health-token monitor; checks every 100 ms
 *
 *   Phase 3 — APPLICATION
 *     fb_temp_sensor     Simulated temperature sensor (20–90 °C oscillation)
 *     fb_temp_controller Thermal FSM (NORMAL → WARNING → CRITICAL → NORMAL)
 *
 * Run: ./build/examples/thermostat/embediq_thermostat
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_fb.h"
#include "embediq_obs.h"
#include "embediq_osal.h"

#include <stdio.h>

/* ---------------------------------------------------------------------------
 * Platform FB register functions (platform/posix/)
 * ------------------------------------------------------------------------- */

extern EmbedIQ_FB_Handle_t fb_timer_register(void);
extern EmbedIQ_FB_Handle_t fb_nvm_register(void);
extern EmbedIQ_FB_Handle_t fb_watchdog_register(void);

/* ---------------------------------------------------------------------------
 * Application FB register functions (examples/thermostat/)
 * ------------------------------------------------------------------------- */

extern EmbedIQ_FB_Handle_t fb_temp_sensor_register(void);
extern EmbedIQ_FB_Handle_t fb_temp_controller_register(void);

/* ---------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */

int main(void)
{
    printf("EmbedIQ Smart Thermostat Demo — Phase 1\n");
    printf("========================================\n\n");

    /* Route Observatory output to stdout (host default). */
    embediq_obs_set_transport(EMBEDIQ_OBS_TRANSPORT_STDOUT);

    /* -----------------------------------------------------------------------
     * Register FBs in boot-phase order.
     * embediq_engine_boot() will sort and initialise them 1 → 2 → 3.
     * --------------------------------------------------------------------- */

    /* Phase 1: platform */
    fb_timer_register();

    /* Phase 2: infrastructure services */
    fb_nvm_register();
    fb_watchdog_register();

    /* Phase 3: application logic */
    fb_temp_sensor_register();
    fb_temp_controller_register();

    /* -----------------------------------------------------------------------
     * Boot the framework.
     * Returns 0 on success; -1 if a dependency cycle or missing dep is found.
     * --------------------------------------------------------------------- */

    int ret = embediq_engine_boot();
    if (ret != 0) {
        printf("[MAIN] embediq_engine_boot() failed — aborting.\n");
        return 1;
    }

    printf("\n[MAIN] All FBs initialised. Running demo (30 s)...\n\n");

    /* -----------------------------------------------------------------------
     * Let the demo run for 30 seconds.
     * The timer thread drives the FB message loop:
     *   every 1 s: sensor reads → controller evaluates FSM → watchdog kicks
     * Temperature reaches WARNING (> 75 °C) at ≈ 12 s,
     * CRITICAL (> 85 °C) at ≈ 14 s, back to NORMAL at ≈ 24 s.
     * --------------------------------------------------------------------- */

    embediq_osal_delay_ms(30000u);

    printf("\n[MAIN] Demo complete.\n");
    return 0;
}
