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

/**
 * CONTRACT — Read semantics (mandatory for all implementations):
 *   HAL_OK means buf holds the stored bytes for [addr, addr+len). A region
 *   never written since the store was created, or erased, reads successfully
 *   with the implementation's erased/absent value (POSIX: zeros beyond the
 *   stored length, 0xFF for erased regions); callers that must detect
 *   "nothing stored" use their own framing (the NVM store uses a magic number
 *   and CRC), not a byte pattern. HAL_ERR_IO means the store could not be read
 *   for any reason other than its absence (absent file or absent directory are
 *   an empty store); buf is unspecified and the caller must not treat it as
 *   content. An implementation never reports HAL_OK with fabricated content
 *   when the store could not be read, and never performs a write or erase that
 *   depends on a read that failed or on a store it could not hold in full.
 *
 *   POSIX: an absent backing file is an empty store; any other open or read
 *   failure is HAL_ERR_IO; a backing file larger than the implementation holds
 *   is HAL_ERR_INVALID.
 */
int      hal_flash_read(uint32_t addr, void *buf, size_t len);
/**
 * CONTRACT — Write atomicity (mandatory for all implementations):
 *   Either the write completes in full and the data is readable on the next
 *   hal_flash_read() call, or it does not happen at all. Partial writes
 *   observable after a power-loss event are a contract violation and will
 *   corrupt the NVM store on any target.
 *
 *   A write (and an erase) that must first read the store fails without
 *   modifying it if that read fails or the store exceeds the implementation's
 *   capacity. A failed flush, close or rename leaves the previous store intact
 *   and no temporary copy behind. The new contents are synced to the medium
 *   before success is reported.
 *
 *   POSIX: satisfied via write-to-tmp + fflush + fsync + rename(2) in
 *   hal_flash_posix.c; the temp file is removed on any failure.
 *   ESP32 / STM32: use sector-copy-before-erase or dual-bank write.
 *   This requirement MUST be documented in any RTOS/MCU HAL implementation
 *   comment before that implementation is submitted for review.
 */
int      hal_flash_write(uint32_t addr, const void *buf, size_t len);
/** hal_flash_erase — erase the page(s) covering [addr, addr+len); like a
 *  write, it fails without modifying the store if the store cannot be read. */
int      hal_flash_erase(uint32_t addr, size_t len);
/** hal_flash_page_size — returns the platform erase-page size in bytes. */
uint32_t hal_flash_page_size(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_FLASH_H */
