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

#ifndef _USB_DESCRIPTOR_H_
#define _USB_DESCRIPTOR_H_

// AT32F435 USB descriptor.
// NOTE (WIP): vendor/product IDs and descriptors for the board's USB identity.
// See boards/at32f435/PORTING.md.

#define USB_VENDOR_ID   0x1209
#define USB_PRODUCT_ID  0x4357

#define USB_NAME                     "AT32F435"
#define USB_MANUFACTURER             'E', 'D', 'G', 'E', 'T', 'X', ' ', ' '  /* 8 bytes */
#define USB_PRODUCT                  'A', 'T', '3', '2', 'F', '4', '3', '5'  /* 8 Bytes */

#endif // _USB_DESCRIPTOR_H_
