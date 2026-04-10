/*
 * test_platform_lib.c — Unit tests for the platform library registration API
 *
 * Verifies embediq_platform_lib_declare(): init ordering (declaration order),
 * deduplication (same function pointer registered twice = called once),
 * reverse deinit order, and count tracking.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_platform.h"
#include "embediq_config.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int s_run = 0, s_pass = 0;
#define TEST(name, expr) \
    do { s_run++; \
         if(expr){s_pass++;printf("PASS  %s\n",name);} \
         else{printf("FAIL  %s (line %d)\n",name,__LINE__);} \
    } while(0)

/* --- Call-order tracking ------------------------------------------------- */
#define TEST_CALL_LOG_SIZE  (2u * EMBEDIQ_MAX_LIB_INITS)  /* 32 slots */
static int s_call_log[TEST_CALL_LOG_SIZE];
static int s_call_count = 0;

static void reset_log(void) { s_call_count = 0; memset(s_call_log, 0, sizeof(s_call_log)); }
static void log_call(int id) { if (s_call_count < (int)TEST_CALL_LOG_SIZE) s_call_log[s_call_count++] = id; }

static void init_a(void) { log_call(1); }
static void init_b(void) { log_call(2); }
static void init_c(void) { log_call(3); }
static void deinit_a(void) { log_call(-1); }
static void deinit_b(void) { log_call(-2); }
static void deinit_c(void) { log_call(-3); }

/* --- Tests --------------------------------------------------------------- */

static void test_init_order(void)
{
    embediq_platform_lib_reset();   /* test-only: clear registry */
    reset_log();

    embediq_platform_lib_declare(init_a, deinit_a);
    embediq_platform_lib_declare(init_b, deinit_b);
    embediq_platform_lib_declare(init_c, deinit_c);

    embediq_platform_lib_init_all();

    TEST("init_order_a_first", s_call_log[0] == 1);
    TEST("init_order_b_second", s_call_log[1] == 2);
    TEST("init_order_c_third", s_call_log[2] == 3);
    TEST("init_count_3", s_call_count == 3);
}

static void test_deinit_reverse(void)
{
    reset_log();
    embediq_platform_lib_deinit_all();

    TEST("deinit_c_first",  s_call_log[0] == -3);
    TEST("deinit_b_second", s_call_log[1] == -2);
    TEST("deinit_a_third",  s_call_log[2] == -1);
    TEST("deinit_count_3",  s_call_count == 3);
}

static void test_dedup(void)
{
    embediq_platform_lib_reset();
    reset_log();

    embediq_platform_lib_declare(init_a, deinit_a);
    embediq_platform_lib_declare(init_a, deinit_a);  /* duplicate — must be ignored */
    embediq_platform_lib_declare(init_b, deinit_b);

    TEST("count_after_dedup", embediq_platform_lib_count() == 2);

    embediq_platform_lib_init_all();
    TEST("init_a_called_once", s_call_count == 2);
    TEST("first_call_is_a",    s_call_log[0] == 1);
    TEST("second_call_is_b",   s_call_log[1] == 2);
}

static void test_null_deinit(void)
{
    embediq_platform_lib_reset();
    embediq_platform_lib_declare(init_a, NULL);  /* NULL deinit is legal */
    embediq_platform_lib_init_all();
    embediq_platform_lib_deinit_all();  /* must not crash */
    TEST("null_deinit_safe", 1);
}

int main(void)
{
    printf("=== test_platform_lib ===\n");
    test_init_order();
    test_deinit_reverse();
    test_dedup();
    test_null_deinit();
    printf("\n%d/%d passed\n", s_pass, s_run);
    return (s_pass == s_run) ? 0 : 1;
}
