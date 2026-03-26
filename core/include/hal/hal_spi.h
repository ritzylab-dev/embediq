/*
 * core/include/hal/hal_spi.h — HAL SPI contract
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef HAL_SPI_H
#define HAL_SPI_H

#include <stdint.h>
#include <stddef.h>
#include "hal_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t speed_hz;
    uint8_t  mode;   /* SPI mode 0–3 (CPOL/CPHA) */
    uint8_t  bits;   /* bits per word, typically 8 */
} hal_spi_cfg_t;

int  hal_spi_init(const hal_spi_cfg_t *cfg);
/**
 * hal_spi_transfer — full-duplex transfer of len bytes.
 * tx may be NULL (send zeros); rx may be NULL (discard).
 */
int  hal_spi_transfer(const uint8_t *tx, uint8_t *rx, size_t len);
void hal_spi_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_SPI_H */
