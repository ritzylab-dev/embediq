#define _POSIX_C_SOURCE 200809L

/*
 * tests/unit/test_fb_gpio.c -- Unit tests for fb_gpio Driver FB
 *
 * Drives fb_gpio through the real engine: fb_engine__reset() -> register ->
 * embediq_engine_boot() -> fb_engine__deliver_msg() (the same injection
 * mechanism used by the thermostat integration tests, passing payload bytes).
 * GPIO effects are verified against the POSIX sysfs HAL's host /tmp files;
 * published-event payloads and OBS faults are verified via fb_gpio's host-only
 * capture accessors (same pattern as fb_watchdog's wdg__fault_count()).
 *
 * Portability invariant under test: application code uses a logical gpio_id;
 * only fb_gpio + the HAL know the physical pin (gpio_id=0 -> pin=17 here).
 *
 * Run:      ./build/tests/unit/test_fb_gpio
 * Expected: "All N tests passed. (0 failed)"
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "fb_gpio.h"
#include "embediq_fb.h"
#include "embediq_platform_msgs.h"
#include "hal/hal_gpio.h"

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Package-internal APIs (host test builds only)
 * ------------------------------------------------------------------------- */

extern void     fb_engine__reset(void);
extern void     fb_engine__deliver_msg(EmbedIQ_FB_Handle_t handle,
                                       uint16_t msg_id, const void *msg);

extern void     fb_gpio__test_reset(void);
extern int      fb_gpio__test_pub_count(void);
extern uint16_t fb_gpio__test_pub_id(int idx);
extern uint8_t  fb_gpio__test_pub_gpio_id(int idx);
extern uint8_t  fb_gpio__test_pub_state(int idx);
extern int      fb_gpio__test_fault_count(void);

/* ---------------------------------------------------------------------------
 * Minimal test harness (pattern follows test_fb_watchdog.c)
 * ------------------------------------------------------------------------- */

static int g_tests_run    = 0;
static int g_tests_failed = 0;

#define ASSERT(cond, msg)  do {                                                 \
    g_tests_run++;                                                              \
    if (!(cond)) {                                                              \
        fprintf(stderr, "FAIL  %-52s  %s:%d  %s\n",                             \
                __func__, __FILE__, __LINE__, (msg));                           \
        g_tests_failed++;                                                       \
    } else {                                                                    \
        printf("PASS  %s  %s\n", __func__, (msg));                             \
    }                                                                           \
} while (0)

/* gpio_id=0 is the logical ID application code uses; pin=17 is physical. */
static const embediq_gpio_pin_config_t k_test_pins[] = {
    { .gpio_id = 0u, .pin = 17u, .dir = HAL_GPIO_DIR_OUT, .pull = HAL_GPIO_PULL_NONE },
};

static int read_file_content(const char *path, char *buf, size_t buflen)
{
    FILE *f = fopen(path, "r");
    if (f == NULL) { return -1; }
    size_t n = fread(buf, 1u, buflen - 1u, f);
    (void)fclose(f);
    buf[n] = '\0';
    return (int)n;
}

/* Fresh engine + clean /tmp + cleared capture; register fb_gpio and boot so
 * gpio_init() runs (sub-fn registered, pins exported/configured). */
static EmbedIQ_FB_Handle_t setup(void)
{
    fb_engine__reset();
    (void)remove("/tmp/embediq_gpio_export");
    (void)remove("/tmp/embediq_gpio_unexport");
    (void)remove("/tmp/embediq_gpio17_direction");
    (void)remove("/tmp/embediq_gpio17_value");
    fb_gpio__test_reset();

    EmbedIQ_FB_Handle_t h = fb_gpio_register(k_test_pins, 1u);
    (void)embediq_engine_boot();
    return h;
}

static void deliver_set_request(EmbedIQ_FB_Handle_t h, uint8_t gpio_id, uint8_t state)
{
    MSG_GPIO_SET_REQUEST_Payload_t p = { .gpio_id = gpio_id, .state = state };
    fb_engine__deliver_msg(h, MSG_GPIO_SET_REQUEST, &p);
}

/* ---------------------------------------------------------------------------
 * TEST 1 — SET_REQUEST(gpio_id=0) drives physical pin 17 via the HAL
 * ------------------------------------------------------------------------- */
static void test_set_request_drives_hal_write(void)
{
    EmbedIQ_FB_Handle_t h = setup();
    char buf[24];

    deliver_set_request(h, 0u, 1u);
    ASSERT(read_file_content("/tmp/embediq_gpio17_value", buf, sizeof(buf)) >= 1,
           "physical pin 17 value file written for logical gpio_id 0");
    ASSERT(buf[0] == '1', "gpio17 value == '1' after set high");

    deliver_set_request(h, 0u, 0u);
    (void)read_file_content("/tmp/embediq_gpio17_value", buf, sizeof(buf));
    ASSERT(buf[0] == '0', "gpio17 value == '0' after set low");
}

/* ---------------------------------------------------------------------------
 * TEST 2 — publishes MSG_GPIO_PIN_EVENT carrying the LOGICAL gpio_id
 * ------------------------------------------------------------------------- */
static void test_publishes_pin_event(void)
{
    EmbedIQ_FB_Handle_t h = setup();

    deliver_set_request(h, 0u, 1u);

    ASSERT(fb_gpio__test_pub_count() == 1, "exactly one MSG_GPIO_PIN_EVENT published");
    ASSERT(fb_gpio__test_pub_id(0) == MSG_GPIO_PIN_EVENT, "published id is MSG_GPIO_PIN_EVENT");
    ASSERT(fb_gpio__test_pub_gpio_id(0) == 0u, "event carries logical gpio_id 0 (not physical pin 17)");
    ASSERT(fb_gpio__test_pub_state(0) == 1u, "event state == 1");
}

/* ---------------------------------------------------------------------------
 * TEST 3 — unknown gpio_id emits a fault, publishes nothing
 * ------------------------------------------------------------------------- */
static void test_unknown_gpio_id_emits_fault(void)
{
    EmbedIQ_FB_Handle_t h = setup();

    deliver_set_request(h, 99u, 1u);   /* gpio_id 99 not in config */

    ASSERT(fb_gpio__test_fault_count() >= 1, "fault emitted for unknown gpio_id");
    ASSERT(fb_gpio__test_pub_count() == 0, "no MSG_GPIO_PIN_EVENT for unknown gpio_id");
}

/* ---------------------------------------------------------------------------
 * TEST 4 — HAL write failure emits a fault, publishes nothing
 * ------------------------------------------------------------------------- */
static void test_hal_write_failure_emits_fault(void)
{
    EmbedIQ_FB_Handle_t h = setup();

    hal_gpio__fail_next_write(17u);   /* arm failure on physical pin 17 */
    deliver_set_request(h, 0u, 1u);

    ASSERT(fb_gpio__test_fault_count() >= 1, "fault emitted when hal_gpio_write fails");
    ASSERT(fb_gpio__test_pub_count() == 0, "no MSG_GPIO_PIN_EVENT when write fails");
}

/* ---------------------------------------------------------------------------
 * TEST 5 — init configures the physical pin direction to "out"
 * ------------------------------------------------------------------------- */
static void test_init_configures_direction(void)
{
    (void)setup();   /* boot runs gpio_init -> hal_gpio_init(pin=17, DIR_OUT) */

    char buf[24];
    ASSERT(read_file_content("/tmp/embediq_gpio17_direction", buf, sizeof(buf)) >= 3,
           "direction file written for physical pin 17 at init");
    ASSERT(strncmp(buf, "out", 3) == 0, "gpio17 direction == 'out'");
}

/* ---------------------------------------------------------------------------
 * TEST 6 — deinit unexports the physical pin
 * ------------------------------------------------------------------------- */
static void test_deinit_calls_unexport(void)
{
    (void)setup();

    hal_gpio_deinit(17u);

    char buf[24];
    ASSERT(read_file_content("/tmp/embediq_gpio_unexport", buf, sizeof(buf)) >= 2,
           "unexport file written on deinit");
    ASSERT(strstr(buf, "17") != NULL, "unexport file contains physical pin 17");
}

/* ---------------------------------------------------------------------------
 * Entry point
 * ------------------------------------------------------------------------- */

int main(void)
{
    test_set_request_drives_hal_write();
    test_publishes_pin_event();
    test_unknown_gpio_id_emits_fault();
    test_hal_write_failure_emits_fault();
    test_init_configures_direction();
    test_deinit_calls_unexport();

    printf("\n");
    if (g_tests_failed == 0) {
        printf("All %d tests passed. (0 failed)\n", g_tests_run);
    } else {
        printf("%d of %d tests FAILED.\n", g_tests_failed, g_tests_run);
    }
    return (g_tests_failed > 0) ? 1 : 0;
}
