#define _POSIX_C_SOURCE 200809L

/*
 * fbs/drivers/fb_gpio.c -- GPIO Platform Driver FB
 *
 * Wraps the HAL GPIO contract and exposes output control to the message bus.
 * Application FBs publish MSG_GPIO_SET_REQUEST with a LOGICAL gpio_id; fb_gpio
 * maps gpio_id -> physical pin via its board config table and drives the HAL.
 * It publishes MSG_GPIO_PIN_EVENT (carrying the logical gpio_id) on success.
 * v1 is output-only; input pins are configured at init, read path is v1.x.
 *
 * Platform Driver FB (EMBEDIQ_BOOT_PHASE_PLATFORM). Calls the HAL via the
 * hal_gpio.h contract; the platform implementation (hal_gpio_posix.c) is linked
 * by the application/test executable. No <stdio.h>, no OSAL calls (R-sub-03).
 *
 * Dispatcher contract (verified — fb_engine.c:459, fb_cloud_mqtt precedent):
 * sub-function run_fn receives a pointer to the message PAYLOAD BYTES, not the
 * EmbedIQ_Msg_t envelope, and no length. The payload region is always
 * EMBEDIQ_MSG_MAX_PAYLOAD bytes, so the 2-byte request struct is read directly.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "fb_gpio.h"
#include "embediq_subfn.h"
#include "embediq_bus.h"
#include "embediq_msg.h"
#include "embediq_config.h"
#include "embediq_platform_msgs.h"
#include "embediq_obs.h"

#include <string.h>
#include <stdint.h>

/* ---------------------------------------------------------------------------
 * Private macros
 * ------------------------------------------------------------------------- */

/* FB-level Observatory fault source. fb_watchdog uses the runtime endpoint id
 * as its source; for a named constant the established convention (fb_cloud_mqtt,
 * fb_provisioning) is 0u, with the state field distinguishing fault paths. */
#define SRC_FB_GPIO  0u

/* ---------------------------------------------------------------------------
 * Static (private) function declarations
 * ------------------------------------------------------------------------- */

static const embediq_gpio_pin_config_t *find_pin_cfg(uint8_t gpio_id);
static void gpio_obs_fault(uint8_t state);
static void gpio_set(EmbedIQ_FB_Handle_t fb, const void *msg,
                     void *fb_data, void *subfn_data);
static void gpio_init(EmbedIQ_FB_Handle_t fb, void *fb_data);

/* ---------------------------------------------------------------------------
 * Static data (R-02: no malloc -- all state is static)
 * ------------------------------------------------------------------------- */

static const embediq_gpio_pin_config_t *g_pins      = NULL;
static uint8_t                          g_pin_count = 0u;

EMBEDIQ_SUBS(g_gpio_subs, MSG_GPIO_SET_REQUEST);
EMBEDIQ_PUBS(g_gpio_pubs, MSG_GPIO_PIN_EVENT);

#ifdef EMBEDIQ_PLATFORM_HOST
#define GPIO_TEST_MAX_PUB  8u   /* capture depth for host unit tests */
static int      g_test_pub_count    = 0;
static uint16_t g_test_pub_ids[GPIO_TEST_MAX_PUB];
static uint8_t  g_test_pub_gpio_ids[GPIO_TEST_MAX_PUB];
static uint8_t  g_test_pub_states[GPIO_TEST_MAX_PUB];
static int      g_test_fault_count  = 0;
#endif

/* ---------------------------------------------------------------------------
 * Public function implementations
 * ------------------------------------------------------------------------- */

EmbedIQ_FB_Handle_t fb_gpio_register(const embediq_gpio_pin_config_t *pins,
                                     uint8_t pin_count)
{
    g_pins      = pins;
    g_pin_count = pin_count;

    static const EmbedIQ_FB_Config_t k_cfg = {
        .name               = "fb_gpio",
        .boot_phase         = EMBEDIQ_BOOT_PHASE_PLATFORM,
        .init_fn            = gpio_init,
        .subscriptions      = g_gpio_subs,
        .subscription_count = 1u,
        .publications       = g_gpio_pubs,
        .publication_count  = 1u,
    };
    return embediq_fb_register(&k_cfg);
}

/* ---------------------------------------------------------------------------
 * Static function implementations
 * ------------------------------------------------------------------------- */

/* Find the config entry for a logical gpio_id; returns NULL if not configured. */
static const embediq_gpio_pin_config_t *find_pin_cfg(uint8_t gpio_id)
{
    for (uint8_t i = 0u; i < g_pin_count; i++) {
        if (g_pins[i].gpio_id == gpio_id) {
            return &g_pins[i];
        }
    }
    return NULL;
}

/* Emit an always-on Observatory fault for a service-layer failure path. */
static void gpio_obs_fault(uint8_t state)
{
    EMBEDIQ_OBS_EMIT_FAULT(EMBEDIQ_OBS_EVT_FAULT, SRC_FB_GPIO, 0u, state, 0u);
#ifdef EMBEDIQ_PLATFORM_HOST
    g_test_fault_count++;
#endif
}

/* MSG_GPIO_SET_REQUEST handler. `msg` points at the payload bytes. */
static void gpio_set(EmbedIQ_FB_Handle_t fb, const void *msg,
                     void *fb_data, void *subfn_data)
{
    (void)fb_data;
    (void)subfn_data;

    /* Fault path 1: NULL payload. The dispatcher passes payload bytes with no
     * length, so a non-NULL check is the only meaningful pre-read guard. */
    if (msg == NULL) {
        gpio_obs_fault(1u);
        return;
    }

    MSG_GPIO_SET_REQUEST_Payload_t p;
    (void)memcpy(&p, msg, sizeof(p));

    /* Fault path 2: unknown logical gpio_id. */
    const embediq_gpio_pin_config_t *cfg = find_pin_cfg(p.gpio_id);
    if (cfg == NULL) {
        gpio_obs_fault(2u);
        return;
    }

    /* Drive the physical pin via the HAL. R-sub-17: check the return value. */
    int ret = hal_gpio_write(cfg->pin, p.state);
    if (ret != 0) {
        /* HAL already emitted an OBS error at source 0xD3; emit at FB granularity too. */
        gpio_obs_fault(3u);
        return;
    }

    /* Success -- publish MSG_GPIO_PIN_EVENT carrying the LOGICAL gpio_id. */
#ifdef EMBEDIQ_PLATFORM_HOST
    if (g_test_pub_count < 8) {
        g_test_pub_ids[g_test_pub_count]      = MSG_GPIO_PIN_EVENT;
        g_test_pub_gpio_ids[g_test_pub_count] = p.gpio_id;
        g_test_pub_states[g_test_pub_count]   = p.state;
        g_test_pub_count++;
    }
#endif

    EmbedIQ_Msg_t evt;
    (void)memset(&evt, 0, sizeof(evt));
    evt.msg_id   = MSG_GPIO_PIN_EVENT;
    evt.priority = (uint8_t)EMBEDIQ_MSG_PRIORITY_NORMAL;

    MSG_GPIO_PIN_EVENT_Payload_t ep;
    ep.gpio_id = p.gpio_id;
    ep.state   = p.state;
    (void)memcpy(evt.payload, &ep, sizeof(ep));
    evt.payload_len = (uint16_t)sizeof(ep);

    embediq_publish(fb, &evt);
}

/* FB init -- register the sub-function (R-sub-08), then configure pins.
 * init_fn returns void (embediq_fb.h contract), so a pin init failure is
 * non-fatal: it is reported via OBS (state 4) and the remaining pins proceed. */
static void gpio_init(EmbedIQ_FB_Handle_t fb, void *fb_data)
{
    (void)fb_data;

    static const EmbedIQ_SubFn_Config_t k_subfn_cfg = {
        .name               = "gpio_set",
        .init_order         = 1u,
        .init_fn            = NULL,
        .run_fn             = gpio_set,
        .exit_fn            = NULL,
        .subscriptions      = g_gpio_subs,
        .subscription_count = 1u,
        .subfn_data         = NULL,
        .fsm                = NULL,
        .osal_signal        = NULL,
    };
    (void)embediq_subfn_register(fb, &k_subfn_cfg);

    /* Initialize all configured pins via the HAL (physical pin only). */
    for (uint8_t i = 0u; i < g_pin_count; i++) {
        int ret = hal_gpio_init(g_pins[i].pin, g_pins[i].dir, g_pins[i].pull);
        if (ret != 0) {
            gpio_obs_fault(4u);
        }
    }
}

/* ---------------------------------------------------------------------------
 * Host-only: release pins at process exit, and test accessors.
 * ------------------------------------------------------------------------- */

#ifdef EMBEDIQ_PLATFORM_HOST

__attribute__((destructor))
static void gpio_deinit_all(void)
{
    for (uint8_t i = 0u; i < g_pin_count; i++) {
        hal_gpio_deinit(g_pins[i].pin);
    }
}

void fb_gpio__test_reset(void)
{
    g_test_pub_count   = 0;
    g_test_fault_count = 0;
}

int      fb_gpio__test_pub_count(void)      { return g_test_pub_count; }
uint16_t fb_gpio__test_pub_id(int idx)      { return g_test_pub_ids[idx]; }
uint8_t  fb_gpio__test_pub_gpio_id(int idx) { return g_test_pub_gpio_ids[idx]; }
uint8_t  fb_gpio__test_pub_state(int idx)   { return g_test_pub_states[idx]; }
int      fb_gpio__test_fault_count(void)    { return g_test_fault_count; }

#endif /* EMBEDIQ_PLATFORM_HOST */
