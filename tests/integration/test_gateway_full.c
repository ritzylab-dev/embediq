#define _POSIX_C_SOURCE 200809L

/*
 * tests/integration/test_gateway_full.c — Gateway full integration test
 *
 * Boots all 6 FBs on real POSIX threads and runs for 30 seconds.
 * Pass criterion: process exits with 0 (no crash, no boot failure).
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
#include <stdlib.h>

extern EmbedIQ_FB_Handle_t fb_timer_register(void);
extern EmbedIQ_FB_Handle_t fb_nvm_register(void);
extern EmbedIQ_FB_Handle_t fb_watchdog_register(void);
extern EmbedIQ_FB_Handle_t fb_field_receiver_register(void);
extern EmbedIQ_FB_Handle_t fb_policy_engine_register(void);
extern EmbedIQ_FB_Handle_t fb_north_bridge_register(void);
extern void embediq_obs_stream_posix_register(void);

int main(void)
{
    embediq_obs_stream_posix_register();
    embediq_obs_set_transport(EMBEDIQ_OBS_TRANSPORT_STDOUT);
    embediq_obs_set_level(1u);

    fb_timer_register();
    fb_nvm_register();
    fb_watchdog_register();
    fb_field_receiver_register();
    fb_policy_engine_register();
    fb_north_bridge_register();

    if (embediq_engine_boot() != 0) {
        fprintf(stderr, "[TEST] embediq_engine_boot() failed\n");
        return EXIT_FAILURE;
    }
    embediq_engine_dispatch_boot();

    /* Run for 30 s to exercise all scripted events. */
    embediq_osal_delay_ms(30000u);

    printf("[TEST] gateway_full PASS\n");
    return EXIT_SUCCESS;
}
