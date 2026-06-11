#define _POSIX_C_SOURCE 200809L

/*
 * examples/env_monitor/main.c — Simulated Environmental Monitor
 *
 * Demonstrates full EmbedIQ + Cloud OSS integration:
 *
 *   Phase 1 — PLATFORM
 *     fb_timer       Tick source (MSG_TIMER_1SEC)
 *
 *   Phase 2 — INFRASTRUCTURE
 *     fb_nvm         NVM backend (reads mqtt.* config from EMBEDIQ_NVM_PATH)
 *     fb_watchdog    Health monitor
 *     fb_telemetry   Metric aggregator → MSG_TELEMETRY_BATCH
 *     fb_cloud_mqtt  MQTT transport → Mosquitto → Cloud OSS
 *
 *   Phase 3 — APPLICATION
 *     fb_env_sim     Simulated temp/humidity/CO2 sensors + cloud command handler
 *
 * Prerequisites:
 *   - Mosquitto running on localhost:1883
 *   - Cloud OSS running on localhost:8080
 *   - Device registered: POST /api/v1/devices {id: "env-monitor-001", password: "..."}
 *   - NVM config blob: see examples/env_monitor/README.md
 *
 * Run:
 *   EMBEDIQ_NVM_PATH=/tmp/env_monitor.bin ./build/examples/env_monitor/env_monitor
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
#include <stdlib.h>   /* getenv */
#include <string.h>
#include <signal.h>
#include <stdint.h>
#include <stdbool.h>

/* ---------------------------------------------------------------------------
 * Driver FBs (fbs/drivers/ — external declarations)
 * ------------------------------------------------------------------------- */
extern EmbedIQ_FB_Handle_t fb_timer_register(void);
extern EmbedIQ_FB_Handle_t fb_nvm_register(void);
extern EmbedIQ_FB_Handle_t fb_watchdog_register(void);

/* ---------------------------------------------------------------------------
 * Service FBs (fbs/services/ — external declarations)
 * ------------------------------------------------------------------------- */
extern EmbedIQ_FB_Handle_t fb_telemetry_register(void);
extern EmbedIQ_FB_Handle_t fb_cloud_mqtt_register(void);

/* ---------------------------------------------------------------------------
 * Application FB (examples/env_monitor/ — local)
 * ------------------------------------------------------------------------- */
extern EmbedIQ_FB_Handle_t fb_env_sim_register(void);

/* ---------------------------------------------------------------------------
 * Platform declarations (hal/posix/ops/ — external)
 * ------------------------------------------------------------------------- */
extern void hal_mqtt_posix_declare(void);

/* ---------------------------------------------------------------------------
 * Shutdown flag (set by SIGINT / Ctrl+C)
 * ------------------------------------------------------------------------- */
static volatile bool g_running = true;

static void handle_sigint(int sig)
{
    (void)sig;
    g_running = false;
}

/* ---------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */

int main(void)
{
    printf("\n");
    printf("╔══════════════════════════════════════════════════╗\n");
    printf("║   EmbedIQ Environmental Monitor — Cloud Demo    ║\n");
    printf("║   Sensors: temperature · humidity · CO2          ║\n");
    printf("╚══════════════════════════════════════════════════╝\n\n");

    printf("[MAIN] EMBEDIQ_NVM_PATH = %s\n\n",
           getenv("EMBEDIQ_NVM_PATH") ? getenv("EMBEDIQ_NVM_PATH")
                                      : "(not set — using defaults)");

    /* SIGINT handler for clean Ctrl+C shutdown */
    (void)signal(SIGINT, handle_sigint);

    /* -----------------------------------------------------------------------
     * Declare platform libraries.
     * Must happen before embediq_engine_boot().
     * ----------------------------------------------------------------------- */
    hal_mqtt_posix_declare();

    /* -----------------------------------------------------------------------
     * Register Functional Blocks.
     * Boot phases determine init order: 1 → 2 → 3.
     * embediq_engine_boot() sorts and initialises in phase order.
     * ----------------------------------------------------------------------- */

    /* Phase 1 — PLATFORM */
    fb_timer_register();

    /* Phase 2 — INFRASTRUCTURE */
    fb_nvm_register();
    fb_watchdog_register();
    fb_telemetry_register();
    fb_cloud_mqtt_register();

    /* Phase 3 — APPLICATION */
    fb_env_sim_register();

    /* -----------------------------------------------------------------------
     * Boot the engine (initialises all FBs in phase order).
     * fb_cloud_mqtt init reads mqtt.host/port/client_id from NVM via
     * embediq_cfg and connects to the MQTT broker.
     * ----------------------------------------------------------------------- */
    printf("[MAIN] Booting engine...\n");
    int ret = embediq_engine_boot();
    if (ret != 0) {
        printf("[MAIN] ERROR: embediq_engine_boot() failed (%d)\n", ret);
        return 1;
    }

    /* -----------------------------------------------------------------------
     * Start per-FB dispatch threads.
     * Each subscribed FB gets a dedicated OS thread for message processing.
     * ----------------------------------------------------------------------- */
    embediq_engine_dispatch_boot();

    printf("[MAIN] All FBs running. Publishing sensor data to Cloud OSS.\n");
    printf("[MAIN] Press Ctrl+C to stop.\n\n");

    /* -----------------------------------------------------------------------
     * Run until SIGINT.
     * Print a heartbeat every 30 s to show the device is alive.
     * ----------------------------------------------------------------------- */
    uint32_t heartbeat_count = 0u;
    while (g_running) {
        embediq_osal_delay_ms(30000u);
        if (!g_running) { break; }
        heartbeat_count++;
        printf("[MAIN] Heartbeat #%u — device running, publishing to Cloud OSS.\n",
               (unsigned)heartbeat_count);
    }

    printf("\n[MAIN] Shutting down gracefully...\n");
    return 0;
}
