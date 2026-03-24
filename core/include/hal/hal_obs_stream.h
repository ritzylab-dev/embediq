/*
 * hal_obs_stream.h — Observatory binary stream HAL contract
 *
 * Implemented by hal/posix/hal_obs_stream_posix.c on POSIX targets.
 * Zero EmbedIQ framework dependencies — pure stdint.h.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef HAL_OBS_STREAM_H
#define HAL_OBS_STREAM_H

#include <stdint.h>
#include <stddef.h>

/** Open (or create) a binary stream at path. Truncates any existing file. */
int hal_obs_stream_open(const char *path);

/** Append len bytes from data to the open stream. */
int hal_obs_stream_write(const void *data, uint16_t len);

/** Flush any buffered bytes to storage. */
int hal_obs_stream_flush(void);

/** Close the open stream. */
int hal_obs_stream_close(void);

/* Return codes */
#define HAL_OBS_STREAM_OK   0
#define HAL_OBS_STREAM_ERR  (-1)

#endif /* HAL_OBS_STREAM_H */
