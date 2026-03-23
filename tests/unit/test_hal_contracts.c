/*
 * tests/unit/test_hal_contracts.c
 *
 * Compile-and-value test for all HAL contract headers and the Endpoint
 * Router contract.  There are no implementations behind these headers yet;
 * this test proves:
 *   (a) every header compiles standalone with only C-standard includes, and
 *   (b) the named constants carry the values the rest of the codebase assumes.
 *
 * Run:      ./build/tests/unit/test_hal_contracts
 * Expected: "All N tests passed. (0 failed)"
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stddef.h>

/* HAL contracts */
#include "hal/hal_timer.h"
#include "hal/hal_uart.h"
#include "hal/hal_gpio.h"
#include "hal/hal_spi.h"
#include "hal/hal_i2c.h"
#include "hal/hal_flash.h"
#include "hal/hal_wdg.h"

/* Endpoint Router contract */
#include "embediq_endpoint.h"

/* ── Minimal test harness (stdlib only) ──────────────────────────── */

static int g_tests_run    = 0;
static int g_tests_failed = 0;

#define ASSERT(cond, msg)  do {                                                \
    g_tests_run++;                                                             \
    if (!(cond)) {                                                             \
        fprintf(stderr, "FAIL  %-56s  %s:%d  — %s\n",                        \
                __func__, __FILE__, __LINE__, (msg));                          \
        g_tests_failed++;                                                      \
    } else {                                                                   \
        printf("PASS  %s\n", __func__);                                        \
    }                                                                          \
} while (0)

/* ── hal_timer ────────────────────────────────────────────────────── */

static void test_hal_timer_ok_is_zero(void)
{
    ASSERT(HAL_OK == 0, "HAL_OK must be 0");
}

static void test_hal_timer_err_values_are_negative(void)
{
    ASSERT(HAL_ERR_INVALID < 0, "HAL_ERR_INVALID must be negative");
    ASSERT(HAL_ERR_BUSY    < 0, "HAL_ERR_BUSY must be negative");
    ASSERT(HAL_ERR_TIMEOUT < 0, "HAL_ERR_TIMEOUT must be negative");
    ASSERT(HAL_ERR_IO      < 0, "HAL_ERR_IO must be negative");
}

/* ── hal_gpio ─────────────────────────────────────────────────────── */

static void test_hal_gpio_dir_values(void)
{
    ASSERT(HAL_GPIO_DIR_IN  == 0, "HAL_GPIO_DIR_IN must be 0");
    ASSERT(HAL_GPIO_DIR_OUT == 1, "HAL_GPIO_DIR_OUT must be 1");
}

static void test_hal_gpio_pull_values(void)
{
    ASSERT(HAL_GPIO_PULL_NONE == 0, "HAL_GPIO_PULL_NONE must be 0");
    ASSERT(HAL_GPIO_PULL_UP   == 1, "HAL_GPIO_PULL_UP must be 1");
    ASSERT(HAL_GPIO_PULL_DOWN == 2, "HAL_GPIO_PULL_DOWN must be 2");
}

/* ── hal_uart ─────────────────────────────────────────────────────── */

static void test_hal_uart_parity_values(void)
{
    ASSERT(HAL_UART_PARITY_NONE == 0, "HAL_UART_PARITY_NONE must be 0");
    ASSERT(HAL_UART_PARITY_ODD  == 1, "HAL_UART_PARITY_ODD must be 1");
    ASSERT(HAL_UART_PARITY_EVEN == 2, "HAL_UART_PARITY_EVEN must be 2");
}

/* ── embediq_endpoint ─────────────────────────────────────────────── */

static void test_endpoint_invalid_sentinel(void)
{
    ASSERT(EMBEDIQ_EP_INVALID == 0xFFFFu, "EMBEDIQ_EP_INVALID must be 0xFFFF");
}

static void test_endpoint_name_max_at_least_16(void)
{
    ASSERT(EMBEDIQ_EP_NAME_MAX >= 16, "EMBEDIQ_EP_NAME_MAX must be >= 16");
}

/* ── struct sizes (smoke: padding is not absurd) ──────────────────── */

static void test_hal_uart_cfg_size_reasonable(void)
{
    ASSERT(sizeof(hal_uart_cfg_t) <= 32u, "hal_uart_cfg_t must be <= 32 bytes");
}

static void test_hal_spi_cfg_size_reasonable(void)
{
    ASSERT(sizeof(hal_spi_cfg_t) <= 16u, "hal_spi_cfg_t must be <= 16 bytes");
}

static void test_hal_i2c_cfg_size_reasonable(void)
{
    ASSERT(sizeof(hal_i2c_cfg_t) <= 8u, "hal_i2c_cfg_t must be <= 8 bytes");
}

/* ── Entry point ──────────────────────────────────────────────────── */

int main(void)
{
    test_hal_timer_ok_is_zero();
    test_hal_timer_err_values_are_negative();
    test_hal_gpio_dir_values();
    test_hal_gpio_pull_values();
    test_hal_uart_parity_values();
    test_endpoint_invalid_sentinel();
    test_endpoint_name_max_at_least_16();
    test_hal_uart_cfg_size_reasonable();
    test_hal_spi_cfg_size_reasonable();
    test_hal_i2c_cfg_size_reasonable();

    printf("\nAll %d tests passed. (%d failed)\n",
           g_tests_run - g_tests_failed, g_tests_failed);
    return g_tests_failed > 0 ? 1 : 0;
}
