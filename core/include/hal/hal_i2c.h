/*
 * core/include/hal/hal_i2c.h — HAL I2C contract
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "hal_timer.h"

typedef struct {
    uint32_t speed_hz;  /* 100000 (standard) or 400000 (fast) */
} hal_i2c_cfg_t;

int  hal_i2c_init(const hal_i2c_cfg_t *cfg);
int  hal_i2c_write(uint8_t addr, const uint8_t *buf, size_t len);
int  hal_i2c_read(uint8_t  addr, uint8_t  *buf, size_t len);
void hal_i2c_deinit(void);
