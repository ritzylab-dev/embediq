/*
 * fb_gpio.h -- GPIO Platform Driver FB public API
 *
 * fb_gpio is a Platform-layer Driver FB (EMBEDIQ_BOOT_PHASE_PLATFORM).
 * It manages one or more GPIO output pins via the HAL contract in hal_gpio.h.
 *
 * Application FBs drive GPIO pins by publishing MSG_GPIO_SET_REQUEST with a
 * logical gpio_id. They never know the physical pin number.
 *
 * Board config maps gpio_id to physical pin. See examples/gpio_blink for a
 * reference board config pattern.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FB_GPIO_H
#define FB_GPIO_H

#include "embediq_fb.h"
#include "hal/hal_gpio.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Board GPIO config entry -- maps a logical GPIO ID to a physical pin.
 *
 * gpio_id  Logical identifier used in MSG_GPIO_SET_REQUEST / MSG_GPIO_PIN_EVENT.
 *          Application FBs publish this ID. They never see the physical pin.
 * pin      Physical BCM/SoC pin number. Visible only to fb_gpio and the HAL.
 *          On Raspberry Pi: BCM pin number (e.g. 17).
 *          On other platforms: platform-specific encoding (see HAL docs).
 */
typedef struct {
    uint8_t          gpio_id;  /* Logical GPIO identifier (0-based, board-defined) */
    uint8_t          pin;      /* Physical pin number (HAL-visible only)            */
    hal_gpio_dir_t   dir;      /* Pin direction                                     */
    hal_gpio_pull_t  pull;     /* Pull configuration                                */
} embediq_gpio_pin_config_t;

/*
 * Register fb_gpio with the EmbedIQ engine.
 *
 * Call once from main() before embediq_engine_boot().
 * The pins array must have static lifetime -- fb_gpio retains a pointer to it.
 *
 * @param pins       Board GPIO config table (static lifetime required).
 * @param pin_count  Number of entries in the table.
 * @return           FB handle, or NULL on registration failure.
 */
EmbedIQ_FB_Handle_t fb_gpio_register(const embediq_gpio_pin_config_t *pins,
                                      uint8_t pin_count);

/* ---------------------------------------------------------------------------
 * Test injection -- EMBEDIQ_PLATFORM_HOST builds only
 * Used by unit tests to simulate HAL write failures.
 * ------------------------------------------------------------------------- */
#ifdef EMBEDIQ_PLATFORM_HOST
/*
 * Cause the next hal_gpio_write() call for physical pin `pin` to return -1.
 * Resets automatically after one triggered failure.
 */
void hal_gpio__fail_next_write(uint8_t pin);
#endif

#ifdef __cplusplus
}
#endif

#endif /* FB_GPIO_H */
