/*
 * Copyright (C) EdgeTX
 *
 * Based on code named
 *   opentx - https://github.com/opentx/opentx
 *   th9x - http://code.google.com/p/th9x
 *   er9x - http://code.google.com/p/er9x
 *   gruvin9x - http://code.google.com/p/gruvin9x
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * AT32 USB MSC disk I/O interface.
 *
 * The Artery MSC class (msc_bot_scsi.c) calls the functions declared here to
 * access the underlying storage medium. This is the AT32 equivalent of the
 * STM32 usbd_storage_msd* files. It maps the MSC disk requests onto the
 * EdgeTX storage layer (hal/storage.h + hal/fatfs_diskio.h).
 */

#pragma once

#include "usb_std.h"

#ifdef __cplusplus
extern "C" {
#endif

// Standard SCSI Inquiry data (36 bytes). Must return a pointer to a buffer
// that stays valid while the command is processed.
uint8_t *get_inquiry(uint8_t lun);

// Report the medium capacity: number of blocks and block size (bytes).
// Writes values through the output pointers (no return value).
void msc_disk_capacity(uint8_t lun, uint32_t *block_num, uint32_t *block_size);

// Read / Write a number of bytes at the given byte address on the medium.
// Returns USB_OK on success or USB_FAIL on error.
usb_sts_type msc_disk_read(uint8_t lun, uint32_t blk_addr, uint8_t *buf, uint32_t len);
usb_sts_type msc_disk_write(uint8_t lun, uint32_t blk_addr, uint8_t *buf, uint32_t len);

#ifdef __cplusplus
}
#endif
