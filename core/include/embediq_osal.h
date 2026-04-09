/*
 * embediq_osal.h — OS Abstraction Layer contract
 *
 * Core Layer contract for threading, queues, signals, mutexes, and time.
 * All OS handles are opaque — zero RTOS or platform headers exposed here.
 * Declarations only — zero implementation in this file.
 *
 * I-01: Compiles standalone with zero OSAL or BSP dependencies.
 * R-03: C11. Fixed-width types from <stdint.h> / <stdbool.h> only.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EMBEDIQ_OSAL_H
#define EMBEDIQ_OSAL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Error code type
 * ------------------------------------------------------------------------- */

typedef int32_t embediq_err_t;
#define EMBEDIQ_OK            ((embediq_err_t) 0)
#define EMBEDIQ_ERR           ((embediq_err_t)-1)
#define EMBEDIQ_ERR_TIMEOUT   ((embediq_err_t)-2)  /**< Operation timed out or would block */
#define EMBEDIQ_ERR_NOMEM     ((embediq_err_t)-3)  /**< Allocation or resource limit reached */
#define EMBEDIQ_ERR_INVALID   ((embediq_err_t)-4)  /**< Invalid argument or precondition */

/* ---------------------------------------------------------------------------
 * Opaque OS primitive handles
 *
 * Callers hold pointers only. Struct definitions live in osal/<target>/.
 * Core code must never dereference or size these types (R-sub-10).
 * ------------------------------------------------------------------------- */

/** Opaque OS task / thread handle. */
typedef struct EmbedIQ_Task_s    EmbedIQ_Task_t;

/** Opaque OS message queue handle. */
typedef struct EmbedIQ_Queue_s   EmbedIQ_Queue_t;

/** Opaque binary signal (counting semaphore / event flag).
 *  Platform FB sub-functions only — app FBs must set osal_signal = NULL. */
typedef struct EmbedIQ_Signal_s  EmbedIQ_Signal_t;

/** Opaque mutual-exclusion lock. */
typedef struct EmbedIQ_Mutex_s   EmbedIQ_Mutex_t;

/** Opaque software timer handle. */
typedef struct EmbedIQ_Timer_s   EmbedIQ_Timer_t;

/** Opaque counting semaphore — used by the dispatch engine.
 *  Not for application FBs. typedef to pointer so callers do not need '*'. */
typedef struct EmbedIQ_Sem_s    *embediq_sem_t;

/* ---------------------------------------------------------------------------
 * Task / thread API
 * ------------------------------------------------------------------------- */

/** Create and immediately start an OS task. Returns NULL on failure. */
EmbedIQ_Task_t *embediq_osal_task_create(const char *name,
                                          void (*fn)(void *), void *arg,
                                          uint8_t priority,
                                          uint16_t stack_bytes);

/** Delete a task created by embediq_osal_task_create(). */
void embediq_osal_task_delete(EmbedIQ_Task_t *task);

/**
 * Wait for a task to exit naturally, then free its handle.
 * Unlike task_delete(), this does NOT cancel the thread — it only joins.
 * The task must have already exited or be on the verge of exiting.
 * Used by embediq_engine_dispatch_shutdown() for clean teardown.
 */
void embediq_osal_task_join(EmbedIQ_Task_t *task);

/** Block the calling task for at least ms milliseconds. */
void embediq_osal_delay_ms(uint32_t ms);

/* ---------------------------------------------------------------------------
 * Queue API — copy-by-value semantics (v1 constraint: no zero-copy)
 * ------------------------------------------------------------------------- */

/** Create a queue holding depth items of item_size bytes each. */
EmbedIQ_Queue_t *embediq_osal_queue_create(uint16_t depth, uint16_t item_size);

/** Copy item into the back of the queue. Blocks up to timeout_ms.
 *  Returns EMBEDIQ_OK on success.
 *  Returns EMBEDIQ_ERR_TIMEOUT if the queue is full and timeout elapsed.
 *  Never negate this return value with ! (R-osal-01). */
embediq_err_t embediq_osal_queue_send(EmbedIQ_Queue_t *q, const void *item,
                                       uint32_t timeout_ms);

/** Copy the front item out of the queue. Blocks up to timeout_ms.
 *  Returns EMBEDIQ_OK on success.
 *  Returns EMBEDIQ_ERR_TIMEOUT if the queue is empty and timeout elapsed.
 *  Never negate this return value with ! (R-osal-01). */
embediq_err_t embediq_osal_queue_recv(EmbedIQ_Queue_t *q, void *item,
                                       uint32_t timeout_ms);

/** Return the number of items currently in the queue. */
uint16_t embediq_osal_queue_count(EmbedIQ_Queue_t *q);

/** Destroy a queue and free its resources. Safe to call with NULL. */
void embediq_osal_queue_destroy(EmbedIQ_Queue_t *q);

/* ---------------------------------------------------------------------------
 * Signal API — used by Platform FB sub-functions to wake on hardware events
 *
 * Application FBs must NOT use signals directly (R-sub-11).
 * embediq_osal_signal_from_isr() is ISR-safe; exits in < 10 cycles.
 * ------------------------------------------------------------------------- */

/** Create a binary signal initialised to the unsignalled state. */
EmbedIQ_Signal_t *embediq_osal_signal_create(void);

/** Block until the signal is raised. */
void embediq_osal_signal_wait(EmbedIQ_Signal_t *sig);

/** Block up to ms milliseconds for the signal.
 *  Returns EMBEDIQ_OK if the signal was raised.
 *  Returns EMBEDIQ_ERR_TIMEOUT if the timeout elapsed without a signal.
 *  Never negate this return value with ! (R-osal-01). */
embediq_err_t embediq_osal_signal_wait_timeout(EmbedIQ_Signal_t *sig, uint32_t ms);

/** Raise the signal from an ISR context. Must exit in < 10 cycles. */
void embediq_osal_signal_from_isr(EmbedIQ_Signal_t *sig);

/* ---------------------------------------------------------------------------
 * Mutex API
 * ------------------------------------------------------------------------- */

/** Create an unlocked mutex. */
EmbedIQ_Mutex_t *embediq_osal_mutex_create(void);

/** Lock the mutex, blocking up to timeout_ms.
 *  Returns EMBEDIQ_OK if the lock was acquired.
 *  Returns EMBEDIQ_ERR_TIMEOUT if the timeout elapsed without acquiring the lock.
 *  Never negate this return value with ! (R-osal-01). */
embediq_err_t embediq_osal_mutex_lock(EmbedIQ_Mutex_t *m, uint32_t timeout_ms);

/** Unlock a mutex held by the calling task. */
void embediq_osal_mutex_unlock(EmbedIQ_Mutex_t *m);

/* ---------------------------------------------------------------------------
 * Semaphore API — used internally by the dispatch engine
 *
 * Not for application FBs. embediq_sem_post_from_isr() is safe to call from
 * an ISR or any thread context.
 * ------------------------------------------------------------------------- */

/** Create a counting semaphore with the given initial and maximum counts. */
embediq_sem_t  embediq_sem_create(uint32_t initial_count, uint32_t max_count);

/** Block until the semaphore count is > 0, then decrement and return. */
embediq_err_t  embediq_sem_wait(embediq_sem_t sem);

/** Non-blocking: return true and decrement if count > 0, else return false. */
bool           embediq_sem_trywait(embediq_sem_t sem);

/** Increment the semaphore count (unblocks one waiter). */
embediq_err_t  embediq_sem_post(embediq_sem_t sem);

/** Destroy a semaphore created by embediq_sem_create(). */
void           embediq_sem_destroy(embediq_sem_t sem);

/** Post from an ISR or timer context.
 *  POSIX: same as sem_post. FreeRTOS: use xSemaphoreGiveFromISR. */
embediq_err_t  embediq_sem_post_from_isr(embediq_sem_t sem);

/* ---------------------------------------------------------------------------
 * Time API
 *
 * timestamp_us wraps at ~71 minutes. Use sequence for ordering (I-13).
 * These functions are INFORMATIONAL ONLY — never use for gap detection.
 * ------------------------------------------------------------------------- */

/** Monotonic microsecond clock. Wraps at 2^32 (~71 min). Informational only. */
uint32_t embediq_osal_time_us(void);

/** Monotonic millisecond clock. Wraps at 2^32 (~49 days). Informational only. */
uint32_t embediq_osal_time_ms(void);

/* ---------------------------------------------------------------------------
 * OSAL observation obligation macros
 *
 * Activated in XOBS-1 (Phase 2 — before FreeRTOS OSAL implementation).
 * Currently no-ops. Placed here so CI tool check_osal_obs.py can enforce
 * their presence in every OSAL implementation file that touches tasks,
 * queues, mutexes, or signals.
 * ------------------------------------------------------------------------- */
#define EMBEDIQ_OSAL_OBS_TASK_FAIL(idx)       ((void)0)
#define EMBEDIQ_OSAL_OBS_MUTEX_TIMEOUT(idx)   ((void)0)
#define EMBEDIQ_OSAL_OBS_QUEUE_FULL(idx)      ((void)0)
#define EMBEDIQ_OSAL_OBS_SIGNAL_TIMEOUT(idx)  ((void)0)
#define EMBEDIQ_OSAL_OBS_STACK_OVERFLOW(idx)  ((void)0)

#ifdef __cplusplus
}
#endif

#endif /* EMBEDIQ_OSAL_H */
