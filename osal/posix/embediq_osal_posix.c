/*
 * osal/posix/embediq_osal_posix.c — POSIX implementation of embediq_osal.h
 *
 * Implements every function declared in core/include/embediq_osal.h using
 * POSIX primitives.  Target: macOS / Linux.
 *
 * Primitive mapping:
 *   pthread_t + wrapper fn          → EmbedIQ_Task_t
 *   pthread_mutex + pthread_cond    → EmbedIQ_Queue_t   (ring buffer, copy-by-value)
 *   pthread_mutex + pthread_cond    → EmbedIQ_Signal_t  (counting semaphore)
 *   pthread_mutex                   → EmbedIQ_Mutex_t
 *   CLOCK_MONOTONIC                 → time APIs
 *   nanosleep                       → embediq_osal_delay_ms
 *
 * malloc / free policy (OSAL exception to R-02):
 *   malloc/free are used ONLY inside this file to allocate opaque handles.
 *   All other layers (Shell 1 and above) must never call malloc directly.
 *
 * Struct definitions are private to this file.  Core code sees only the
 * forward-declared opaque types from embediq_osal.h (R-sub-10).
 *
 * Cancellation note: embediq_osal_signal_wait and embediq_osal_queue_send/
 * recv call pthread_cond_wait, which is a cancellation point.  Callers
 * that cancel a blocked thread must install their own cleanup handlers if
 * they hold external locks around the OSAL call.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_osal.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>

/* ---------------------------------------------------------------------------
 * Private struct definitions
 *
 * Visible only within osal/posix/.  Core headers forward-declare these as
 * incomplete types; nothing outside this directory may dereference them.
 * ------------------------------------------------------------------------- */

struct EmbedIQ_Task_s {
    pthread_t thread;
};

struct EmbedIQ_Queue_s {
    pthread_mutex_t mutex;
    pthread_cond_t  not_empty;
    pthread_cond_t  not_full;
    uint16_t        depth;
    uint16_t        item_size;
    uint16_t        count;
    uint16_t        head;
    uint16_t        tail;
    uint8_t        *buf;    /* depth * item_size bytes */
};

struct EmbedIQ_Signal_s {
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    int             count;  /* counting semaphore value — non-negative */
};

struct EmbedIQ_Mutex_s {
    pthread_mutex_t mutex;
};

/* Phase 1: no timer API functions declared in embediq_osal.h.
 * Struct defined here to satisfy the forward declaration. */
struct EmbedIQ_Timer_s {
    int _reserved;
};

/* ---------------------------------------------------------------------------
 * Internal helpers
 * ------------------------------------------------------------------------- */

/** 64-bit microseconds from CLOCK_MONOTONIC. Never wraps within a session. */
static uint64_t _time_us_64(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL
         + (uint64_t)(ts.tv_nsec / 1000);
}

/**
 * Compute an absolute CLOCK_REALTIME deadline for pthread_cond_timedwait.
 * @param out        Receives the absolute deadline.
 * @param timeout_ms Relative timeout in milliseconds.
 */
static void _abs_deadline(struct timespec *out, uint32_t timeout_ms)
{
    clock_gettime(CLOCK_REALTIME, out);
    uint64_t ns = (uint64_t)out->tv_nsec
                + (uint64_t)timeout_ms * 1000000ULL;
    out->tv_sec  += (time_t)(ns / 1000000000ULL);
    out->tv_nsec  = (long)  (ns % 1000000000ULL);
}

/* ---------------------------------------------------------------------------
 * Task / thread API
 * ------------------------------------------------------------------------- */

/** Wrapper adapting void(*)(void*) to the void*(*)(void*) pthread expects. */
typedef struct {
    void (*fn)(void *);
    void  *arg;
} _task_wrapper_t;

static void *_task_entry(void *raw)
{
    _task_wrapper_t w = *(_task_wrapper_t *)raw;
    free(raw);      /* freed before fn() so cancellation inside fn is leak-free */
    w.fn(w.arg);
    return NULL;
}

EmbedIQ_Task_t *embediq_osal_task_create(const char    *name,
                                          void         (*fn)(void *),
                                          void          *arg,
                                          uint8_t        priority,
                                          uint16_t       stack_bytes)
{
    (void)name;
    (void)priority;
    (void)stack_bytes;

    _task_wrapper_t *w = malloc(sizeof(_task_wrapper_t));
    if (!w) return NULL;
    w->fn  = fn;
    w->arg = arg;

    EmbedIQ_Task_t *task = malloc(sizeof(EmbedIQ_Task_t));
    if (!task) { free(w); return NULL; }

    if (pthread_create(&task->thread, NULL, _task_entry, w) != 0) {
        free(w);
        free(task);
        return NULL;
    }
    return task;
}

void embediq_osal_task_delete(EmbedIQ_Task_t *task)
{
    if (!task) return;
    pthread_cancel(task->thread);
    pthread_join(task->thread, NULL);
    free(task);
}

void embediq_osal_delay_ms(uint32_t ms)
{
    struct timespec ts = {
        .tv_sec  = (time_t)(ms / 1000u),
        .tv_nsec = (long)((ms % 1000u) * 1000000L),
    };
    nanosleep(&ts, NULL);
}

/* ---------------------------------------------------------------------------
 * Queue API — copy-by-value ring buffer, mutex + condvar
 * ------------------------------------------------------------------------- */

EmbedIQ_Queue_t *embediq_osal_queue_create(uint16_t depth, uint16_t item_size)
{
    EmbedIQ_Queue_t *q = malloc(sizeof(EmbedIQ_Queue_t));
    if (!q) return NULL;

    q->buf = malloc((size_t)depth * item_size);
    if (!q->buf) { free(q); return NULL; }

    pthread_mutex_init(&q->mutex,     NULL);
    pthread_cond_init (&q->not_empty, NULL);
    pthread_cond_init (&q->not_full,  NULL);
    q->depth     = depth;
    q->item_size = item_size;
    q->count     = 0;
    q->head      = 0;
    q->tail      = 0;
    return q;
}

bool embediq_osal_queue_send(EmbedIQ_Queue_t *q, const void *item,
                              uint32_t timeout_ms)
{
    struct timespec abs   = {0};
    bool            timed = (timeout_ms > 0 && timeout_ms != UINT32_MAX);
    if (timed) _abs_deadline(&abs, timeout_ms);

    pthread_mutex_lock(&q->mutex);

    while (q->count >= q->depth) {
        if (timeout_ms == 0) {
            pthread_mutex_unlock(&q->mutex);
            return false;
        }
        if (timed) {
            if (pthread_cond_timedwait(&q->not_full, &q->mutex, &abs)
                    == ETIMEDOUT) {
                pthread_mutex_unlock(&q->mutex);
                return false;
            }
        } else {
            /* UINT32_MAX — block forever */
            pthread_cond_wait(&q->not_full, &q->mutex);
        }
    }

    memcpy(q->buf + (size_t)q->tail * q->item_size, item, q->item_size);
    q->tail  = (uint16_t)((q->tail  + 1u) % q->depth);
    q->count = (uint16_t)( q->count + 1u);
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->mutex);
    return true;
}

bool embediq_osal_queue_recv(EmbedIQ_Queue_t *q, void *item,
                              uint32_t timeout_ms)
{
    struct timespec abs   = {0};
    bool            timed = (timeout_ms > 0 && timeout_ms != UINT32_MAX);
    if (timed) _abs_deadline(&abs, timeout_ms);

    pthread_mutex_lock(&q->mutex);

    while (q->count == 0) {
        if (timeout_ms == 0) {
            pthread_mutex_unlock(&q->mutex);
            return false;
        }
        if (timed) {
            if (pthread_cond_timedwait(&q->not_empty, &q->mutex, &abs)
                    == ETIMEDOUT) {
                pthread_mutex_unlock(&q->mutex);
                return false;
            }
        } else {
            pthread_cond_wait(&q->not_empty, &q->mutex);
        }
    }

    memcpy(item, q->buf + (size_t)q->head * q->item_size, q->item_size);
    q->head  = (uint16_t)((q->head  + 1u) % q->depth);
    q->count = (uint16_t)( q->count - 1u);
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->mutex);
    return true;
}

uint16_t embediq_osal_queue_count(EmbedIQ_Queue_t *q)
{
    pthread_mutex_lock(&q->mutex);
    uint16_t n = q->count;
    pthread_mutex_unlock(&q->mutex);
    return n;
}

/* ---------------------------------------------------------------------------
 * Signal API — counting semaphore via mutex + condvar
 *
 * embediq_osal_signal_from_isr is mapped to a normal mutex lock on POSIX
 * because there are no hardware ISRs.  It is still safe to call from any
 * thread context, including the POSIX signal handler (sem_post would be
 * the true ISR-safe primitive; for simulation a mutex is acceptable).
 * ------------------------------------------------------------------------- */

EmbedIQ_Signal_t *embediq_osal_signal_create(void)
{
    EmbedIQ_Signal_t *sig = malloc(sizeof(EmbedIQ_Signal_t));
    if (!sig) return NULL;
    pthread_mutex_init(&sig->mutex, NULL);
    pthread_cond_init (&sig->cond,  NULL);
    sig->count = 0;
    return sig;
}

void embediq_osal_signal_wait(EmbedIQ_Signal_t *sig)
{
    pthread_mutex_lock(&sig->mutex);
    while (sig->count == 0) {
        pthread_cond_wait(&sig->cond, &sig->mutex);
    }
    sig->count--;
    pthread_mutex_unlock(&sig->mutex);
}

bool embediq_osal_signal_wait_timeout(EmbedIQ_Signal_t *sig, uint32_t ms)
{
    struct timespec abs;
    _abs_deadline(&abs, ms);

    pthread_mutex_lock(&sig->mutex);
    while (sig->count == 0) {
        if (pthread_cond_timedwait(&sig->cond, &sig->mutex, &abs)
                == ETIMEDOUT) {
            pthread_mutex_unlock(&sig->mutex);
            return false;
        }
    }
    sig->count--;
    pthread_mutex_unlock(&sig->mutex);
    return true;
}

void embediq_osal_signal_from_isr(EmbedIQ_Signal_t *sig)
{
    pthread_mutex_lock(&sig->mutex);
    sig->count++;
    pthread_cond_signal(&sig->cond);
    pthread_mutex_unlock(&sig->mutex);
}

/* ---------------------------------------------------------------------------
 * Mutex API
 *
 * Timed lock uses trylock + 1 ms nanosleep polling.  This avoids reliance
 * on pthread_mutex_timedlock, which requires _POSIX_C_SOURCE >= 200809L and
 * is not exposed by all macOS SDK configurations.
 * ------------------------------------------------------------------------- */

EmbedIQ_Mutex_t *embediq_osal_mutex_create(void)
{
    EmbedIQ_Mutex_t *m = malloc(sizeof(EmbedIQ_Mutex_t));
    if (!m) return NULL;
    pthread_mutex_init(&m->mutex, NULL);
    return m;
}

bool embediq_osal_mutex_lock(EmbedIQ_Mutex_t *m, uint32_t timeout_ms)
{
    if (timeout_ms == UINT32_MAX) {
        return pthread_mutex_lock(&m->mutex) == 0;
    }
    if (timeout_ms == 0) {
        return pthread_mutex_trylock(&m->mutex) == 0;
    }

    /* Poll at 1 ms granularity — sufficient for simulation use cases */
    uint32_t start = embediq_osal_time_ms();
    for (;;) {
        if (pthread_mutex_trylock(&m->mutex) == 0) return true;
        if ((embediq_osal_time_ms() - start) >= timeout_ms) break;
        struct timespec ns = { .tv_sec = 0, .tv_nsec = 1000000L }; /* 1 ms */
        nanosleep(&ns, NULL);
    }
    /* One final attempt once the deadline is reached */
    return pthread_mutex_trylock(&m->mutex) == 0;
}

void embediq_osal_mutex_unlock(EmbedIQ_Mutex_t *m)
{
    pthread_mutex_unlock(&m->mutex);
}

/* ---------------------------------------------------------------------------
 * Time API — CLOCK_MONOTONIC, uint32_t wrapping (I-13 compliant)
 * ------------------------------------------------------------------------- */

uint32_t embediq_osal_time_us(void)
{
    return (uint32_t)_time_us_64();
}

uint32_t embediq_osal_time_ms(void)
{
    return (uint32_t)(_time_us_64() / 1000ULL);
}
