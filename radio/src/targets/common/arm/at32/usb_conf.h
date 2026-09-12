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
 * AT32 USB device library configuration.
 *
 * The Artery AT32F435_437 USB middleware headers include "usb_conf.h"
 * unconditionally. This file provides the build-time configuration that the
 * middleware expects (device mode, OTG core selection, endpoint count and
 * FIFO sizes). See thirdparty/AT32F435_437_USB_Library.
 */

#ifndef __USB_CONF_H
#define __USB_CONF_H

// NULL and friends for the USB middleware C sources.
#include <stddef.h>

// AT32F435 device support. The USB middleware headers (usbd_core.h etc.)
// reference usb_reg_type / usb_ept_info, which are defined in
// at32f435_437_usb.h (NOT pulled in by at32f435_437.h).
#include "at32f435_437.h"
#include "at32f435_437_usb.h"

// -----------------------------------------------------------------------------
// OTG mode -- AT32F435 has OTG1 (full-speed) and OTG2 (high-speed).
// For this monochrome T20V2-compatible target we use the full-speed OTG1 core.
// -----------------------------------------------------------------------------
#define USE_OTG_DEVICE_MODE
// #define USE_OTG_HOST_MODE

// OTG core selection (see at32f435_437_usb.h: USB_OTG1_ID / USB_OTG2_ID)
#define USB_OTG1_ID                      0
#define USB_OTG2_ID                      1

// Maximum number of endpoints supported by the OTG1 (FS) core.
// AT32F435 OTG1 supports 6 bidirectional endpoints (EP0 + 5).
// at32f435_437_usb.h defines USB_EPT_MAX_NUM=8 (for the HS core); override it
// here so the TX FIFO allocation stays within OTG_FIFO_SIZE for the FS core.
#undef USB_EPT_MAX_NUM
#define USB_EPT_MAX_NUM                  6

// Endpoint receive / transmit FIFO sizes, in 32-bit words.
// The total OTG FIFO is OTG_FIFO_SIZE = 320 words (see at32f435_437_usb.h).
// The RX FIFO + EP0..EP3 TX FIFOs must fit within that budget:
//   64 + 64 + 64 + 64 + 64 = 320
#define USBD_RX_SIZE                     64
#define USBD_EP0_TX_SIZE                 64
#define USBD_EP1_TX_SIZE                 64
#define USBD_EP2_TX_SIZE                 64
#define USBD_EP3_TX_SIZE                 64

// NOTE: usbd_core.c uses a runtime "if (USB_EPT_MAX_NUM == 8)" (not #if), so the
// EP4..EP7 macros must always be defined for the code to compile.
#define USBD_EP4_TX_SIZE                 64
#define USBD_EP5_TX_SIZE                 64
#define USBD_EP6_TX_SIZE                 64
#define USBD_EP7_TX_SIZE                 64

// OTG2 (high-speed) FIFOs -- only used if select OTG2. Kept for completeness.
#define USBD2_RX_SIZE                    160
#define USBD2_EP0_TX_SIZE                64
#define USBD2_EP1_TX_SIZE                64
#define USBD2_EP2_TX_SIZE                64
#define USBD2_EP3_TX_SIZE                64
#define USBD2_EP4_TX_SIZE                64
#define USBD2_EP5_TX_SIZE                64
#define USBD2_EP6_TX_SIZE                64
#define USBD2_EP7_TX_SIZE                64

// Optional OTG behaviour modifiers
// #define USB_SOF_OUTPUT_ENABLE
// #define USB_VBUS_IGNORE

// MS Windows OS compatibility descriptor support (0 = disabled)
#define USBD_SUPPORT_WINUSB              0

// Delay helper used by the USB middleware (usbd_core.c). Implemented in
// usb_driver.cpp; declared here so the C sources see a prototype.
#ifdef __cplusplus
extern "C" {
#endif
void usb_delay_ms(uint32_t ms);
#ifdef __cplusplus
}
#endif

#endif // __USB_CONF_H
