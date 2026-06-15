#define _POSIX_C_SOURCE 200809L

/*
 * hal/posix/peripherals/hal_gpio_posix.c -- POSIX sysfs GPIO HAL
 *
 * Implements the hal_gpio.h contract over the Linux sysfs GPIO interface
 * (/sys/class/gpio). No libgpiod -- zero new dependencies. Under
 * EMBEDIQ_PLATFORM_HOST the sysfs paths are redirected to /tmp files so unit
 * tests and CI run without root or GPIO hardware.
 *
 * This layer deals exclusively in PHYSICAL pin numbers. The logical gpio_id
 * abstraction lives in fb_gpio.c -- the HAL never sees it.
 *
 * Returns int (0 = success, -1 = error) per the contract -- NOT embediq_err_t.
 * Every non-success path emits EMBEDIQ_HAL_OBS_EMIT_ERROR (HAL obs obligation).
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "hal/hal_gpio.h"
#include "embediq_obs.h"
#include "embediq_config.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* ---------------------------------------------------------------------------
 * sysfs paths -- redirected to /tmp on host builds. On host the attribute
 * separator is "_" (flat files in /tmp); on the real target it is "/".
 * ------------------------------------------------------------------------- */

#ifdef EMBEDIQ_PLATFORM_HOST
#  define GPIO_SYSFS_EXPORT    "/tmp/embediq_gpio_export"
#  define GPIO_SYSFS_UNEXPORT  "/tmp/embediq_gpio_unexport"
#  define GPIO_SYSFS_BASE      "/tmp/embediq_gpio"
#  define GPIO_SYSFS_ATTR_SEP  "_"
#else
#  define GPIO_SYSFS_EXPORT    "/sys/class/gpio/export"
#  define GPIO_SYSFS_UNEXPORT  "/sys/class/gpio/unexport"
#  define GPIO_SYSFS_BASE      "/sys/class/gpio/gpio"
#  define GPIO_SYSFS_ATTR_SEP  "/"
#endif

/* ---------------------------------------------------------------------------
 * Test-only failure injection (host builds only)
 * ------------------------------------------------------------------------- */

#ifdef EMBEDIQ_PLATFORM_HOST
static uint8_t g_fail_write_pin   = 0xFFu;   /* 0xFF = no pending failure */
static uint8_t g_fail_write_armed = 0u;

void hal_gpio__fail_next_write(uint8_t pin)
{
    g_fail_write_pin   = pin;
    g_fail_write_armed = 1u;
}
#endif

/* ---------------------------------------------------------------------------
 * Internal helpers
 * ------------------------------------------------------------------------- */

static int gpio_build_path(char *buf, size_t buflen, uint8_t pin, const char *attr)
{
    int n = snprintf(buf, buflen, "%s%u%s%s",
                     GPIO_SYSFS_BASE, (unsigned)pin, GPIO_SYSFS_ATTR_SEP, attr);
    if (n < 0 || (size_t)n >= buflen) {
        EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_GPIO, HAL_ERR_INVALID);
        return -1;
    }
    return 0;
}

static int gpio_write_file(const char *path, const char *content)
{
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_GPIO, HAL_ERR_IO);
        return -1;
    }
    int ret = 0;
    if (fputs(content, f) == EOF) {
        EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_GPIO, HAL_ERR_IO);
        ret = -1;
    }
    (void)fclose(f);
    return ret;
}

/* ---------------------------------------------------------------------------
 * hal_gpio.h contract implementations (physical pin numbers only)
 * ------------------------------------------------------------------------- */

int hal_gpio_init(uint8_t pin, hal_gpio_dir_t dir, hal_gpio_pull_t pull)
{
    (void)pull;  /* sysfs does not expose pull configuration */

    char pin_str[12];
    (void)snprintf(pin_str, sizeof(pin_str), "%u\n", (unsigned)pin);
    if (gpio_write_file(GPIO_SYSFS_EXPORT, pin_str) != 0) {
        return -1;
    }

    char buf[EMBEDIQ_GPIO_SYSFS_PATH_SIZE];
    if (gpio_build_path(buf, sizeof(buf), pin, "direction") != 0) {
        return -1;
    }
    const char *dir_str = (dir == HAL_GPIO_DIR_OUT) ? "out" : "in";
    return gpio_write_file(buf, dir_str);
}

int hal_gpio_write(uint8_t pin, uint8_t val)
{
#ifdef EMBEDIQ_PLATFORM_HOST
    if (g_fail_write_armed && pin == g_fail_write_pin) {
        g_fail_write_armed = 0u;
        g_fail_write_pin   = 0xFFu;
        EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_GPIO, HAL_ERR_IO);
        return -1;
    }
#endif
    char buf[EMBEDIQ_GPIO_SYSFS_PATH_SIZE];
    if (gpio_build_path(buf, sizeof(buf), pin, "value") != 0) {
        return -1;
    }
    return gpio_write_file(buf, (val != 0u) ? "1" : "0");
}

int hal_gpio_read(uint8_t pin, uint8_t *val_out)
{
    if (val_out == NULL) {
        EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_GPIO, HAL_ERR_INVALID);
        return -1;
    }
    char buf[EMBEDIQ_GPIO_SYSFS_PATH_SIZE];
    if (gpio_build_path(buf, sizeof(buf), pin, "value") != 0) {
        return -1;
    }
    FILE *f = fopen(buf, "r");
    if (f == NULL) {
        EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_GPIO, HAL_ERR_IO);
        return -1;
    }
    char ch = '\0';
    if (fread(&ch, 1u, 1u, f) != 1u) {
        (void)fclose(f);
        EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_GPIO, HAL_ERR_IO);
        return -1;
    }
    (void)fclose(f);
    *val_out = (ch == '1') ? 1u : 0u;
    return 0;
}

void hal_gpio_deinit(uint8_t pin)
{
    char pin_str[12];
    (void)snprintf(pin_str, sizeof(pin_str), "%u\n", (unsigned)pin);
    (void)gpio_write_file(GPIO_SYSFS_UNEXPORT, pin_str);
    /* deinit is best-effort -- no return value, no fault emit on failure. */
}
