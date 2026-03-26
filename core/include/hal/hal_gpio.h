/*
 * core/include/hal/hal_gpio.h — HAL GPIO contract
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef HAL_GPIO_H
#define HAL_GPIO_H

#include <stdint.h>
#include "hal_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HAL_GPIO_DIR_IN  = 0,
    HAL_GPIO_DIR_OUT = 1,
} hal_gpio_dir_t;

typedef enum {
    HAL_GPIO_PULL_NONE = 0,
    HAL_GPIO_PULL_UP   = 1,
    HAL_GPIO_PULL_DOWN = 2,
} hal_gpio_pull_t;

int  hal_gpio_init(uint8_t pin, hal_gpio_dir_t dir, hal_gpio_pull_t pull);
int  hal_gpio_write(uint8_t pin, uint8_t val);
/** hal_gpio_read — writes the pin level (0 or 1) to *val_out. */
int  hal_gpio_read(uint8_t pin, uint8_t *val_out);
void hal_gpio_deinit(uint8_t pin);

#ifdef __cplusplus
}
#endif

#endif /* HAL_GPIO_H */
