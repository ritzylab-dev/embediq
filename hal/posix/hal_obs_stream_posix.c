/*
 * hal/posix/hal_obs_stream_posix.c — POSIX Observatory binary stream
 *
 * Implements hal_obs_stream.h using standard C file I/O.
 * Registers a stream ops table with the Observatory core via
 * embediq_obs_set_stream_ops() — this is the HAL → Core direction
 * (allowed by layer rules).
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "hal_obs_stream.h"
#include "embediq_obs.h"

#include <stdio.h>
#include <string.h>

static FILE *g_stream = NULL;

int hal_obs_stream_open(const char *path)
{
    if (!path) return HAL_ERR_IO;
    if (g_stream) {
        fclose(g_stream);
        g_stream = NULL;
    }
    g_stream = fopen(path, "wb");
    return g_stream ? HAL_OK : HAL_ERR_IO;
}

int hal_obs_stream_write(const void *data, uint16_t len)
{
    if (!g_stream || !data) return HAL_ERR_IO;
    if (len == 0u) return HAL_OK;
    size_t written = fwrite(data, 1u, (size_t)len, g_stream);
    return (written == (size_t)len) ? HAL_OK : HAL_ERR_IO;
}

int hal_obs_stream_flush(void)
{
    if (!g_stream) return HAL_ERR_IO;
    return (fflush(g_stream) == 0) ? HAL_OK : HAL_ERR_IO;
}

int hal_obs_stream_close(void)
{
    if (!g_stream) return HAL_OK;  /* already closed */
    fflush(g_stream);
    int ret = fclose(g_stream);
    g_stream = NULL;
    return (ret == 0) ? HAL_OK : HAL_ERR_IO;
}

/* ---------------------------------------------------------------------------
 * Observatory stream ops registration
 * ------------------------------------------------------------------------- */

static const embediq_obs_stream_ops_t g_posix_stream_ops = {
    .open  = hal_obs_stream_open,
    .write = hal_obs_stream_write,
    .flush = hal_obs_stream_flush,
    .close = hal_obs_stream_close,
};

void embediq_obs_stream_posix_register(void)
{
    embediq_obs_set_stream_ops(&g_posix_stream_ops);
}
