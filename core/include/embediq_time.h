/*
 * embediq_time.h — Time abstraction contract
 *
 * Provides monotonic microsecond/millisecond clocks and an optional wall-clock
 * epoch for FBs that need real timestamps (e.g., telemetry, logging).
 *
 * Unlike embediq_osal_time_us() (uint32_t, wraps at ~71 min), this API
 * returns uint64_t so callers can accumulate time across sessions without
 * roll-over logic. The implementation wraps the OSAL clock internally.
 *
 * Wall clock is OPTIONAL — targets without an RTC leave epoch unset.
 * Callers must check embediq_time_is_synced() before using timestamps as
 * real-world time. Unsynced timestamps are relative to boot.
 *
 * I-01: Compiles standalone with zero OSAL or BSP dependencies.
 * R-03: C11. Fixed-width types from <stdint.h> only.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EMBEDIQ_TIME_H
#define EMBEDIQ_TIME_H

#include "embediq_osal.h"  /* embediq_err_t */

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Monotonic clock
 *
 * Never jumps backward. Suitable for duration measurement and event ordering.
 * Does NOT depend on wall-clock synchronisation.
 * ------------------------------------------------------------------------- */

/**
 * Monotonic time in microseconds since boot.
 * Never jumps backward.
 *
 * NOTE: On targets with a uint32_t hardware counter this is a software-
 * extended 64-bit value. The host/POSIX implementation uses CLOCK_MONOTONIC
 * and never wraps within a session.
 *
 * Use sequence numbers for ordering events across FBs, not timestamps
 * (AGENTS.md §9, I-13: timestamp_us is informational only).
 */
uint64_t embediq_time_us(void);

/**
 * Monotonic time in milliseconds since boot.
 * Wraps at UINT32_MAX ms (~49 days). Sufficient for timeout and watchdog use.
 */
uint32_t embediq_time_ms(void);

/* ---------------------------------------------------------------------------
 * Wall clock (optional — not all targets have RTC)
 * ------------------------------------------------------------------------- */

/**
 * Set the wall-clock epoch in UTC microseconds.
 * Call once after NTP sync or RTC read. If never called, embediq_time_us()
 * returns time relative to boot and embediq_time_is_synced() returns 0.
 *
 * @param epoch_us  UTC microseconds since Unix epoch (1970-01-01 00:00:00 UTC).
 * @return EMBEDIQ_OK on success, EMBEDIQ_ERR if epoch_us is implausible.
 */
embediq_err_t embediq_time_set_epoch(uint64_t epoch_us);

/**
 * Returns 1 if the wall clock has been synchronised via embediq_time_set_epoch(),
 * 0 if time is relative to boot only.
 */
uint8_t embediq_time_is_synced(void);

#ifdef __cplusplus
}
#endif

#endif /* EMBEDIQ_TIME_H */
