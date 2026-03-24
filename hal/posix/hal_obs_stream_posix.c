/*
 * hal/posix/hal_obs_stream_posix.c — POSIX Observatory binary stream
 *
 * Implements hal_obs_stream.h using standard C file I/O.
 * Zero EmbedIQ framework dependencies.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "hal_obs_stream.h"

#include <stdio.h>
#include <string.h>

static FILE *g_stream = NULL;

int hal_obs_stream_open(const char *path)
{
    if (!path) return HAL_OBS_STREAM_ERR;
    if (g_stream) {
        fclose(g_stream);
        g_stream = NULL;
    }
    g_stream = fopen(path, "wb");
    return g_stream ? HAL_OBS_STREAM_OK : HAL_OBS_STREAM_ERR;
}

int hal_obs_stream_write(const void *data, uint16_t len)
{
    if (!g_stream || !data) return HAL_OBS_STREAM_ERR;
    if (len == 0u) return HAL_OBS_STREAM_OK;
    size_t written = fwrite(data, 1u, (size_t)len, g_stream);
    return (written == (size_t)len) ? HAL_OBS_STREAM_OK : HAL_OBS_STREAM_ERR;
}

int hal_obs_stream_flush(void)
{
    if (!g_stream) return HAL_OBS_STREAM_ERR;
    return (fflush(g_stream) == 0) ? HAL_OBS_STREAM_OK : HAL_OBS_STREAM_ERR;
}

int hal_obs_stream_close(void)
{
    if (!g_stream) return HAL_OBS_STREAM_OK;  /* already closed */
    fflush(g_stream);
    int ret = fclose(g_stream);
    g_stream = NULL;
    return (ret == 0) ? HAL_OBS_STREAM_OK : HAL_OBS_STREAM_ERR;
}
