#define _POSIX_C_SOURCE 200809L

/*
 * examples/gateway/main.c — EmbedIQ Industrial Edge Gateway Demo
 *
 * Demonstrates EmbedIQ on a Linux gateway device.  Three application
 * Functional Blocks simulate a complete edge-to-cloud pipeline:
 *
 *   Phase 1 — PLATFORM
 *     fb_timer            Publishes MSG_TIMER_1SEC every second
 *
 *   Phase 2 — INFRASTRUCTURE
 *     fb_nvm              Persistent key-value store
 *     fb_watchdog         Health-token monitor; checks every 100 ms
 *
 *   Phase 3 — APPLICATION
 *     fb_field_receiver   Simulates RS-485/Modbus field sensor polling
 *     fb_policy_engine    Applies threshold rules; fires alerts; throttles telemetry
 *     fb_north_bridge     Publishes north to cloud (or buffers locally when offline)
 *
 * No hardware required.  Runs on any Linux or macOS host.
 * Build: cmake -B build -DEMBEDIQ_PLATFORM=host && cmake --build build
 * Run:   ./build/examples/gateway/embediq_gateway
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
 * Application FB register functions (examples/gateway/)
 * ------------------------------------------------------------------------- */

extern EmbedIQ_FB_Handle_t fb_field_receiver_register(void);
extern EmbedIQ_FB_Handle_t fb_policy_engine_register(void);
extern EmbedIQ_FB_Handle_t fb_north_bridge_register(void);

/* POSIX stream ops for Observatory file capture */
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

    embediq_obs_stream_posix_register();

    printf("EmbedIQ Industrial Edge Gateway Demo\n");
    printf("======================================\n\n");
    printf("Scenario: Water treatment plant edge gateway\n");
    printf("  - 3 field sensors polled every second (Tank_Temp, Tank_Pressure, Pump_Flow)\n");
    printf("  - Policy engine applies thresholds and fires alerts\n");
    printf("  - North bridge publishes telemetry to cloud (simulated)\n");
    printf("  - Connectivity drop at t=12s, restored at t=23s with buffer flush\n");
    printf("  - Pump dry-run event at t=24s\n\n");

    embediq_obs_set_transport(EMBEDIQ_OBS_TRANSPORT_STDOUT);
    embediq_obs_set_level(2u);

    /* Phase 1: platform */
    fb_timer_register();

    /* Phase 2: infrastructure */
    fb_nvm_register();
    fb_watchdog_register();

    /* Phase 3: application — pipeline order: field -> policy -> north */
    fb_field_receiver_register();
    fb_policy_engine_register();
    fb_north_bridge_register();

    int ret = embediq_engine_boot();
    if (ret != 0) {
        printf("[MAIN] embediq_engine_boot() failed — aborting.\n");
        return 1;
    }

    embediq_engine_dispatch_boot();

    if (capture_path != NULL) {
        EmbedIQ_Obs_Session_t session = {
            .device_id         = 0x00000002u,
            .fw_version        = EMBEDIQ_OBS_FW_VERSION(1, 0, 0),
            .timestamp_base_us = 0ULL,
            .session_id        = 2u,
            .platform_id       = EMBEDIQ_OBS_PLATFORM_POSIX,
            .trace_level       = 2u,
            ._pad              = {0, 0},
            .build_id          = "gateway",
        };
        embediq_obs_session_begin(&session);
        if (embediq_obs_capture_begin(capture_path) == 0) {
            printf("[MAIN] Capturing to %s\n\n", capture_path);
        } else {
            printf("[MAIN] WARNING: capture_begin failed for %s\n", capture_path);
            capture_path = NULL;
        }
    }

    printf("[MAIN] All FBs initialised. Running demo (30 s)...\n\n");

    embediq_osal_delay_ms(30000u);

    if (capture_path != NULL) {
        embediq_obs_capture_end();
        printf("[MAIN] Capture written to %s\n", capture_path);
    }

    printf("\n[MAIN] Demo complete.\n");
    return 0;
}
