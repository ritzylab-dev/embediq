/*
 * core/include/hal/hal_uart.h — HAL UART contract
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "hal_defs.h"

/* Parity constants */
#define HAL_UART_PARITY_NONE  0
#define HAL_UART_PARITY_ODD   1
#define HAL_UART_PARITY_EVEN  2

typedef struct {
    uint32_t baud_rate;
    uint8_t  data_bits;  /* 7 or 8 */
    uint8_t  stop_bits;  /* 1 or 2 */
    uint8_t  parity;     /* HAL_UART_PARITY_* */
} hal_uart_cfg_t;

int  hal_uart_init(const hal_uart_cfg_t *cfg);
int  hal_uart_tx(const uint8_t *buf, size_t len);
/**
 * hal_uart_rx — receive up to len bytes.
 * @return bytes received, or negative HAL_ERR_* on error/timeout
 */
int  hal_uart_rx(uint8_t *buf, size_t len, uint32_t timeout_ms);
void hal_uart_deinit(void);
