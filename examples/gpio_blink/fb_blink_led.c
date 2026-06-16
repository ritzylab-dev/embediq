/*
 * examples/gpio_blink/fb_blink_led.c — LED blink Application FB
 *
 * Toggles an LED once per second by publishing MSG_GPIO_SET_REQUEST to fb_gpio.
 * Subscribes to MSG_TIMER_1SEC (from fb_timer). Boot phase: APPLICATION (3).
 *
 * Platform portability: this FB uses a logical gpio_id only. It never references
 * a physical pin. The gpio_id → pin mapping is in main.c's board config table;
 * fb_gpio resolves it at runtime. This file requires zero changes to move the
 * LED to a different pin or board.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_fb.h"
#include "embediq_subfn.h"
#include "embediq_bus.h"
#include "embediq_msg.h"
#include "embediq_config.h"
#include "embediq_platform_msgs.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

/* ---------------------------------------------------------------------------
 * Static (private) function declarations
 * ------------------------------------------------------------------------- */

static void blink_tick(EmbedIQ_FB_Handle_t fb, const void *msg,
                       void *fb_data, void *subfn_data);
static void blink_led_init(EmbedIQ_FB_Handle_t fb, void *fb_data);

/* ---------------------------------------------------------------------------
 * Module state (static — R-02: no malloc)
 * ------------------------------------------------------------------------- */

static uint8_t g_led_gpio_id = 0u;   /* Logical GPIO ID, set by fb_blink_led_register() */
static uint8_t g_led_state   = 0u;   /* current LED state: 0=off, 1=on                  */

EMBEDIQ_SUBS(g_blink_subs, MSG_TIMER_1SEC);
EMBEDIQ_PUBS(g_blink_pubs, MSG_GPIO_SET_REQUEST);

/* ---------------------------------------------------------------------------
 * Public function implementations
 * ------------------------------------------------------------------------- */

EmbedIQ_FB_Handle_t fb_blink_led_register(uint8_t led_gpio_id)
{
    g_led_gpio_id = led_gpio_id;
    g_led_state   = 0u;

    static const EmbedIQ_FB_Config_t k_cfg = {
        .name               = "fb_blink_led",
        .boot_phase         = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .init_fn            = blink_led_init,
        .subscriptions      = g_blink_subs,
        .subscription_count = 1u,
        .publications       = g_blink_pubs,
        .publication_count  = 1u,
    };
    return embediq_fb_register(&k_cfg);
}

/* ---------------------------------------------------------------------------
 * Static function implementations
 * ------------------------------------------------------------------------- */

/* MSG_TIMER_1SEC handler — toggle the LED and request the new state from fb_gpio. */
static void blink_tick(EmbedIQ_FB_Handle_t fb, const void *msg,
                       void *fb_data, void *subfn_data)
{
    /* MSG_TIMER_1SEC has no payload. Dispatcher contract: msg points at payload
     * bytes; for a zero-payload message, discard it safely (no read needed). */
    (void)msg; (void)fb_data; (void)subfn_data;

    /* Toggle LED state each second. */
    g_led_state ^= 1u;

    /* Publish MSG_GPIO_SET_REQUEST to fb_gpio using the LOGICAL gpio_id.
     * fb_gpio maps gpio_id → physical pin. This FB never sees the physical pin. */
    EmbedIQ_Msg_t m;
    (void)memset(&m, 0, sizeof(m));
    m.msg_id   = MSG_GPIO_SET_REQUEST;
    m.priority = (uint8_t)EMBEDIQ_MSG_PRIORITY_NORMAL;

    MSG_GPIO_SET_REQUEST_Payload_t p;
    p.gpio_id = g_led_gpio_id;   /* logical ID — NOT a physical pin number */
    p.state   = g_led_state;
    (void)memcpy(m.payload, &p, sizeof(p));
    m.payload_len = (uint16_t)sizeof(p);

    printf("[blink_led] gpio_id=%u -> %s\n",
           (unsigned)g_led_gpio_id, g_led_state ? "HIGH" : "LOW");
    embediq_publish(fb, &m);
}

/* FB init — register the blink_tick sub-function (R-sub-08: inside init_fn). */
static void blink_led_init(EmbedIQ_FB_Handle_t fb, void *fb_data)
{
    (void)fb_data;

    static const EmbedIQ_SubFn_Config_t k_subfn_cfg = {
        .name               = "blink_tick",
        .init_order         = 1u,
        .init_fn            = NULL,
        .run_fn             = blink_tick,
        .exit_fn            = NULL,
        .subscriptions      = g_blink_subs,
        .subscription_count = 1u,
        .subfn_data         = NULL,
        .fsm                = NULL,
        .osal_signal        = NULL,
    };
    (void)embediq_subfn_register(fb, &k_subfn_cfg);
}
