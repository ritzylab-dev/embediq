/*
 * core/include/hal/hal_flash.h — HAL flash/NVM contract
 *
 * Used by fb_nvm (Driver FB) via the embediq_nvm.h ops table.
 * Addresses are byte offsets into the platform NVM partition.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef HAL_FLASH_H
#define HAL_FLASH_H

#include <stdint.h>
#include <stddef.h>
#include "hal_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

int      hal_flash_read(uint32_t addr, void *buf, size_t len);
/**
 * CONTRACT — Write atomicity (mandatory for all implementations):
 *   Either the write completes in full and the data is readable on the next
 *   hal_flash_read() call, or it does not happen at all. Partial writes
 *   observable after a power-loss event are a contract violation and will
 *   corrupt the NVM store on any target.
 *
 *   POSIX: satisfied via write-to-tmp + rename(2) in hal_flash_posix.c.
 *   ESP32 / STM32: use sector-copy-before-erase or dual-bank write.
 *   This requirement MUST be documented in any RTOS/MCU HAL implementation
 *   comment before that implementation is submitted for review.
 */
int      hal_flash_write(uint32_t addr, const void *buf, size_t len);
/** hal_flash_erase — erase the page(s) covering [addr, addr+len). */
int      hal_flash_erase(uint32_t addr, size_t len);
/** hal_flash_page_size — returns the platform erase-page size in bytes. */
uint32_t hal_flash_page_size(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_FLASH_H */
