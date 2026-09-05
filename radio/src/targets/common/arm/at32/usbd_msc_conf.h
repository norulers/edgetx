/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 */

#pragma once

/*
 * AT32 USB MSC configuration header.
 *
 * edgetx.h includes "usbd_msc_conf.h" unconditionally (non-SIMU). On STM32 it
 * is provided by the STM32 USB device library. On AT32 the middleware uses its
 * own MSC_MAX_DATA_BUF_LEN, but we keep this header so that generic code can
 * still reference MASS_STORAGE_BUFFER_SIZE.
 */

#if defined(BOOT)
  #define MASS_STORAGE_BUFFER_SIZE 4096U
#else
  #define MASS_STORAGE_BUFFER_SIZE 512U
#endif

// No class BOS descriptor support on the AT32 MSC stack.
#define USBD_CLASS_BOS_ENABLED 0
