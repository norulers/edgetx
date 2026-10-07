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
 */

#include "hal/flash_driver.h"

#include "at32f435_437.h"
#include "board.h"

#include <string.h>

// AT32F435 internal flash sector size (bytes)
#define AT32_FLASH_SECTOR_SIZE           (8 * 1024)

static uint32_t at32_flash_get_size_kb()
{
  // FLASHSIZE is defined in board.h for the AT32F435ZMT7 (4032 KB)
  return FLASHSIZE / 1024;
}

static uint32_t at32_flash_get_sector(uint32_t address)
{
  uint32_t sector_addr = address - FLASH_BASE;
  return sector_addr / AT32_FLASH_SECTOR_SIZE;
}

static uint32_t at32_flash_get_sector_size(uint32_t sector)
{
  (void)sector;
  return AT32_FLASH_SECTOR_SIZE;
}

static inline void at32_flash_unlock() { flash_unlock(); }
static inline void at32_flash_lock() { flash_lock(); }

static int at32_flash_erase_sector(uint32_t address)
{
  int ret = 0;

  at32_flash_unlock();
  if (flash_sector_erase(address) != FLASH_OPERATE_DONE) {
    ret = -1;
  }
  at32_flash_lock();
  return ret;
}

static int at32_flash_program(uint32_t address, void* data, uint32_t len)
{
  uint32_t* p_data = (uint32_t*)data;
  uint32_t end_addr = address + len;

  int ret = 0;
  at32_flash_unlock();
  while (address < end_addr) {
    if (flash_word_program(address, *p_data) != FLASH_OPERATE_DONE) {
      ret = -1;
      break;
    }
    address += sizeof(uint32_t);
    p_data += 1;
  }
  at32_flash_lock();
  return ret;
}

static int at32_flash_read(uint32_t address, void* data, uint32_t len)
{
  memcpy(data, (void*)address, len);
  return 0;
}

const etx_flash_driver_t at32_flash_driver = {
  .get_size_kb = at32_flash_get_size_kb,
  .get_sector = at32_flash_get_sector,
  .get_sector_size = at32_flash_get_sector_size,
  .erase_sector = at32_flash_erase_sector,
  .program = at32_flash_program,
  .read = at32_flash_read,
};

// Legacy API

#define FLASH_PAGESIZE 256

void unlockFlash() { at32_flash_unlock(); }
void lockFlash() { at32_flash_lock(); }

void flashWrite(uint32_t* address, const uint32_t* buffer)
{
  // check first if the address is on a sector boundary
  uint32_t sector = at32_flash_get_sector((uintptr_t)address);
  uint32_t sector_addr = FLASH_BASE + sector * AT32_FLASH_SECTOR_SIZE;

  if ((uintptr_t)address == sector_addr) {
    if (at32_flash_erase_sector((uintptr_t)address) < 0) return;
  }

  at32_flash_program((uintptr_t)address, (uint8_t*)buffer, FLASH_PAGESIZE);
}

// TODO: move this somewhere else, as it depends on firmware layout
uint32_t isFirmwareStart(const uint8_t * buffer)
{
  const uint32_t * block = (const uint32_t *)buffer;

  // Stack pointer in RAM (AT32F435 internal RAM at 0x20000000)
  if ((block[0] & 0xFFFC0000) != 0x20000000) {
    return 0;
  }
  // First ISR pointer in FLASH
  if ((block[1] & 0xFFF00000) != 0x08000000) {
    return 0;
  }
  // Second ISR pointer in FLASH
  if ((block[2] & 0xFFF00000) != 0x08000000) {
    return 0;
  }
  return 1;
}

uint32_t isBootloaderStart(const uint8_t * buffer)
{
  const uint32_t * block = (const uint32_t *)buffer;

  for (int i = 0; i < 256; i++) {
    if (block[i] == 0x544F4F42/*BOOT*/) {
      return 1;
    }
  }
  return 0;
}
