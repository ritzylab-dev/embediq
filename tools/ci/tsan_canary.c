/**
 * @file tsan_canary.c
 * @brief Deliberately racy. Built and run only by the ThreadSanitizer CI job to
 *        prove that a data race is reported and fails the summary step. Never
 *        linked into the framework.
 *
 * Two threads each increment one unsynchronised global 100,000 times; main
 * joins both.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <pthread.h>
#include <stddef.h>

static long g_counter;

static void *increment(void *arg)
{
    (void)arg;
    for (int i = 0; i < 100000; i++) {
        g_counter++;
    }
    return NULL;
}

int main(void)
{
    pthread_t a;
    pthread_t b;
    if (pthread_create(&a, NULL, increment, NULL) != 0) { return 1; }
    if (pthread_create(&b, NULL, increment, NULL) != 0) { return 1; }
    (void)pthread_join(a, NULL);
    (void)pthread_join(b, NULL);
    return (g_counter > 0) ? 0 : 1;
}
