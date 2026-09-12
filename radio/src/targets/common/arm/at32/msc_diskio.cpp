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
 * AT32 USB MSC disk I/O implementation.
 *
 * Maps the Artery MSC class disk requests onto the EdgeTX storage layer
 * (hal/storage.h + hal/fatfs_diskio.h). The MSC middleware passes byte
 * addresses / byte lengths, so these are converted to sector / count here.
 */

#include "msc_diskio.h"

#include "hal/fatfs_diskio.h"
#include "hal/storage.h"
#include "usb_descriptor.h"

#include "debug.h"

#include <string.h>

#if !defined(BOOT)
  #include "timers_driver.h"
  #define WATCHDOG_SUSPEND(x) watchdogSuspend(x)
#else
  #define WATCHDOG_SUSPEND(...)
#endif

#if FF_MAX_SS != FF_MIN_SS
#error "Variable sector size is not supported"
#endif

#define BLOCK_SIZE FF_MAX_SS

// Logical unit numbers (only the SD card for now)
enum {
  STORAGE_SDCARD_LUN = 0,
  STORAGE_LUN_NBR
};

// USB Mass storage Standard Inquiry Data (36 bytes).
#define INQUIRY_DATA_LEN 36
const uint8_t STORAGE_Inquirydata[] = {
  /* LUN 0 */
  0x00,
  0x80,
  0x02,
  0x02,
  (INQUIRY_DATA_LEN - 5),
  0x00,
  0x00,
  0x00,
  USB_MANUFACTURER,                          /* Manufacturer : 8 bytes */
  USB_PRODUCT,                               /* Product      : 8 bytes  */
  'R', 'a', 'd', 'i', 'o', ' ', ' ', ' ',    /* Product      : 8 bytes  */
  '1', '.', '0', '0',                        /* Version      : 4 bytes  */
};

uint8_t *get_inquiry(uint8_t lun)
{
  if (lun >= STORAGE_LUN_NBR)
    return nullptr;
  return (uint8_t *)STORAGE_Inquirydata;
}

void msc_disk_capacity(uint8_t lun, uint32_t *block_num, uint32_t *block_size)
{
  if (lun >= STORAGE_LUN_NBR)
    return;

  *block_size = BLOCK_SIZE;

  // Lazily initialise the default storage driver on first use.
  auto drv = storageGetDefaultDriver();
  if (drv == nullptr) {
    *block_num = 0;
    return;
  }

  static DWORD sector_count = 0;
  if (sector_count == 0) {
    if (drv->ioctl(0, GET_SECTOR_COUNT, &sector_count) != RES_OK) {
      sector_count = 0;
      *block_num = 0;
      return;
    }
  }

  *block_num = sector_count;
}

usb_sts_type msc_disk_read(uint8_t lun, uint32_t blk_addr, uint8_t *buf, uint32_t len)
{
  if (lun >= STORAGE_LUN_NBR)
    return USB_FAIL;

  WATCHDOG_SUSPEND(100 /*1s*/);

  auto drv = storageGetDefaultDriver();
  if (drv == nullptr)
    return USB_FAIL;

  // blk_addr / len are byte based; convert to sector + count
  DWORD sector = blk_addr / BLOCK_SIZE;
  UINT count = len / BLOCK_SIZE;

  return (drv->read(0, buf, sector, count) == RES_OK) ? USB_OK : USB_FAIL;
}

usb_sts_type msc_disk_write(uint8_t lun, uint32_t blk_addr, uint8_t *buf, uint32_t len)
{
  if (lun >= STORAGE_LUN_NBR)
    return USB_FAIL;

  WATCHDOG_SUSPEND(500 /*5s*/);

  auto drv = storageGetDefaultDriver();
  if (drv == nullptr)
    return USB_FAIL;

  // blk_addr / len are byte based; convert to sector + count
  DWORD sector = blk_addr / BLOCK_SIZE;
  UINT count = len / BLOCK_SIZE;

  return (drv->write(0, buf, sector, count) == RES_OK) ? USB_OK : USB_FAIL;
}
