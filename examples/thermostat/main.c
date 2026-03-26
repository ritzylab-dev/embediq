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
#include <string.h>

/* ---------------------------------------------------------------------------
 * Driver FB register functions (fbs/drivers/)
 * ------------------------------------------------------------------------- */

extern EmbedIQ_FB_Handle_t fb_timer_register(void);
extern EmbedIQ_FB_Handle_t fb_nvm_register(void);
extern EmbedIQ_FB_Handle_t fb_watchdog_register(void);

/* ---------------------------------------------------------------------------
 * Application FB register functions (examples/thermostat/)
 * ------------------------------------------------------------------------- */

extern EmbedIQ_FB_Handle_t fb_temp_sensor_register(void);
extern EmbedIQ_FB_Handle_t fb_temp_controller_register(void);

/* POSIX stream ops registration for Observatory file capture */
extern void embediq_obs_stream_posix_register(void);

/* ---------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */

int main(int argc, char *argv[])
{
    /* Parse optional --capture <path> argument. */
    const char *capture_path = NULL;
    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], "--capture") == 0) {
            capture_path = argv[i + 1];
            break;
        }
    }

    /* Register POSIX stream ops for Observatory file capture. */
    embediq_obs_stream_posix_register();

    printf("EmbedIQ Smart Thermostat Demo — Phase 1\n");
    printf("========================================\n\n");

    /* Verbose Observatory output: show FSM transitions and all events. */
    embediq_obs_set_transport(EMBEDIQ_OBS_TRANSPORT_STDOUT);
    embediq_obs_set_level(2u);

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
     * embediq_engine_boot() also calls message_bus_boot() internally so that
     * per-FB queues exist before the Phase-1 timer thread starts publishing.
     * Returns 0 on success; -1 if a dependency cycle or missing dep is found.
     * --------------------------------------------------------------------- */

    int ret = embediq_engine_boot();
    if (ret != 0) {
        printf("[MAIN] embediq_engine_boot() failed — aborting.\n");
        return 1;
    }

    /* -----------------------------------------------------------------------
     * Start per-FB dispatch threads.
     * Creates one pthread per FB that has subscriptions (fb_temp_sensor and
     * fb_temp_controller).  Each thread reads from its FB's priority queues
     * and calls the matching sub-function — completing the end-to-end chain:
     *   fb_timer (thread) → MSG_TIMER_1SEC → fb_temp_sensor (dispatch thread)
     *     → sensor_tick → embediq_publish(MSG_TEMP_READING)
     *     → fb_temp_controller (dispatch thread) → ctrl_run → FSM + wdg_checkin
     * --------------------------------------------------------------------- */

    embediq_engine_dispatch_boot();

    /* -----------------------------------------------------------------------
     * Start .iqtrace file capture if --capture was supplied.
     * --------------------------------------------------------------------- */

    if (capture_path != NULL) {
        EmbedIQ_Obs_Session_t session = {
            .device_id         = 0x00000001u,
            .fw_version        = EMBEDIQ_OBS_FW_VERSION(1, 0, 0),
            .timestamp_base_us = 0ULL,
            .session_id        = 1u,
            .platform_id       = EMBEDIQ_OBS_PLATFORM_POSIX,
            .trace_level       = 2u,
            ._pad              = {0, 0},
            .build_id          = "thermostat",
        };
        embediq_obs_session_begin(&session);
        if (embediq_obs_capture_begin(capture_path) == 0) {
            printf("[MAIN] Capturing to %s\n", capture_path);
        } else {
            printf("[MAIN] WARNING: capture_begin failed for %s\n", capture_path);
            capture_path = NULL;  /* disable end call */
        }
    }

    printf("\n[MAIN] All FBs initialised. Running demo (30 s)...\n\n");

    /* -----------------------------------------------------------------------
     * Let the demo run for 30 seconds.
     * Temperature reaches WARNING (> 75 °C) at ≈ 12 s,
     * CRITICAL (> 85 °C) at ≈ 14 s, back to NORMAL at ≈ 24 s.
     * --------------------------------------------------------------------- */

    embediq_osal_delay_ms(30000u);

    if (capture_path != NULL) {
        embediq_obs_capture_end();
        printf("[MAIN] Capture written to %s\n", capture_path);
    }

    printf("\n[MAIN] Demo complete.\n");
    return 0;
}
