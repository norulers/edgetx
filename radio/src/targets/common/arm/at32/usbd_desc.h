/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * AT32 USB MSC descriptor handler declarations.
 */

#pragma once

#include "usbd_core.h"

#ifdef __cplusplus
extern "C" {
#endif

// USB device descriptor handler for the AT32 mass-storage class.
extern usbd_desc_handler at32_msc_desc_handler;

#ifdef __cplusplus
}
#endif
