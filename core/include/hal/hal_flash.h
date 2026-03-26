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
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "hal_defs.h"

int      hal_flash_read(uint32_t addr, void *buf, size_t len);
int      hal_flash_write(uint32_t addr, const void *buf, size_t len);
/** hal_flash_erase — erase the page(s) covering [addr, addr+len). */
int      hal_flash_erase(uint32_t addr, size_t len);
/** hal_flash_page_size — returns the platform erase-page size in bytes. */
uint32_t hal_flash_page_size(void);
