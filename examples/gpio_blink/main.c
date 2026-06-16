#define _POSIX_C_SOURCE 200809L

/*
 * examples/gpio_blink/main.c — EmbedIQ GPIO Blink Demo
 *
 * Blinks an LED on a configurable BCM GPIO pin at 1 Hz for a configurable
 * duration, then exits cleanly. Demonstrates the fb_gpio Driver FB end-to-end:
 *
 *   fb_timer ──MSG_TIMER_1SEC──► fb_blink_led ──MSG_GPIO_SET_REQUEST(gpio_id=0)──► fb_gpio
 *
 * Board portability: only this file knows the physical pin. The board config
 * table maps logical gpio_id=0 → the BCM pin (--pin, default 17). The
 * application FB (fb_blink_led) is given the logical gpio_id=0 and never sees
 * a physical pin number.
 *
 * Runs on real hardware (Linux sysfs) and on host/CI (hal_gpio_posix.c writes
 * /tmp/embediq_gpio{N}_value under EMBEDIQ_PLATFORM_HOST).
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_fb.h"
#include "embediq_obs.h"
#include "embediq_osal.h"
#include "fb_gpio.h"      /* embediq_gpio_pin_config_t, fb_gpio_register() */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>       /* atoi() */

/* Driver FB register functions (fbs/drivers/) */
extern EmbedIQ_FB_Handle_t fb_timer_register(void);
extern EmbedIQ_FB_Handle_t fb_nvm_register(void);

/* Application FB register function (examples/gpio_blink/) */
extern EmbedIQ_FB_Handle_t fb_blink_led_register(uint8_t led_gpio_id);

/* Observatory stdout/file stream transport (hal/posix) */
extern void embediq_obs_stream_posix_register(void);

/* Board GPIO config — single LED pin. Logical gpio_id=0 → physical BCM pin. */
static embediq_gpio_pin_config_t s_gpio_pins[1];

int main(int argc, char *argv[])
{
    int led_pin  = 17;   /* default BCM GPIO 17 (physical header pin 11) */
    int duration = 10;   /* default run time in seconds                  */

    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], "--pin") == 0) {
            led_pin = atoi(argv[i + 1]);
        } else if (strcmp(argv[i], "--duration") == 0) {
            duration = atoi(argv[i + 1]);
        }
    }

    embediq_obs_stream_posix_register();

    printf("EmbedIQ GPIO Blink Demo\n========================\n");

    embediq_obs_set_transport(EMBEDIQ_OBS_TRANSPORT_STDOUT);
    embediq_obs_set_level(1u);

    /* Board config: gpio_id=0 maps to the BCM pin from --pin (default 17).
     * Application FBs use gpio_id=0; only fb_gpio and the HAL see the physical pin. */
    s_gpio_pins[0].gpio_id = 0u;             /* logical ID used in MSG_GPIO_SET_REQUEST */
    s_gpio_pins[0].pin  = (uint8_t)led_pin;  /* physical BCM pin (from --pin arg)       */
    s_gpio_pins[0].dir  = HAL_GPIO_DIR_OUT;
    s_gpio_pins[0].pull = HAL_GPIO_PULL_NONE;

    /* Register FBs; embediq_engine_boot() sorts and initialises them 1 → 2 → 3. */
    fb_timer_register();                  /* Phase 1 — PLATFORM */
    fb_gpio_register(s_gpio_pins, 1u);    /* Phase 1 — PLATFORM */
    fb_nvm_register();                    /* Phase 2 — INFRASTRUCTURE */
    fb_blink_led_register(0u);            /* Phase 3 — APPLICATION; logical gpio_id=0 */

    int ret = embediq_engine_boot();
    if (ret != 0) {
        printf("[MAIN] embediq_engine_boot() failed — aborting.\n");
        return 1;
    }

    embediq_engine_dispatch_boot();

    printf("[MAIN] Blinking gpio_id=0 (physical pin %u) for %d second(s)...\n",
           (unsigned)led_pin, duration);

    embediq_osal_delay_ms((uint32_t)duration * 1000u);

    printf("[MAIN] Demo complete.\n");
    return 0;
}
