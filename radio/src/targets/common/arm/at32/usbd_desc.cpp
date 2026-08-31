/*
 * Copyright (C) EdgeTX
 *
 * Based on the Artery AT32 USB middleware (msc_desc.c) and the EdgeTX STM32
 * usbd_desc.c, this file provides the USB device descriptors for the AT32
 * mass-storage class using the board-specific VID/PID/strings.
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 */

#include <cstddef>

#include "at32f435_437.h"

#include "usb_std.h"
#include "usbd_core.h"
#include "usbd_sdr.h"

#include "usbd_desc.h"
#include "usb_descriptor.h"

// Endpoint definitions (see msc_class.h / msc_desc.h)
#define USBD_MSC_BULK_IN_EPT             0x81
#define USBD_MSC_BULK_OUT_EPT            0x01
#define USBD_IN_MAXPACKET_SIZE           0x40
#define USBD_OUT_MAXPACKET_SIZE          0x40

#define USBD_MSC_CONFIG_DESC_SIZE        32
#define USBD_MSC_SIZ_STRING_LANGID       4
#define USBD_MSC_SIZ_STRING_SERIAL       0x1A

#define USBD_MSC_VID                     USB_VENDOR_ID
#define USBD_MSC_PID                     USB_PRODUCT_ID

#define USBD_MSC_DESC_MANUFACTURER_STRING    "EdgeTX"
#define USBD_MSC_DESC_PRODUCT_STRING         USB_NAME " Mass Storage"
#define USBD_MSC_DESC_CONFIGURATION_STRING   "Mass Storage Config"
#define USBD_MSC_DESC_INTERFACE_STRING       "Mass Storage Interface"

// AT32F435 96-bit unique ID registers
#define MCU_ID1                           (0x1FFFF7E8)
#define MCU_ID2                           (0x1FFFF7EC)
#define MCU_ID3                           (0x1FFFF7F0)

static usbd_desc_t *get_device_descriptor(void);
static usbd_desc_t *get_device_qualifier(void);
static usbd_desc_t *get_device_configuration(void);
static usbd_desc_t *get_device_other_speed(void);
static usbd_desc_t *get_device_lang_id(void);
static usbd_desc_t *get_device_manufacturer_string(void);
static usbd_desc_t *get_device_product_string(void);
static usbd_desc_t *get_device_serial_string(void);
static usbd_desc_t *get_device_interface_string(void);
static usbd_desc_t *get_device_config_string(void);

static uint16_t usbd_unicode_convert(uint8_t *string, uint8_t *unicode_buf);
static void usbd_int_to_unicode(uint32_t value, uint8_t *pbuf, uint8_t len);
static void get_serial_num(void);

// Device descriptor handler
usbd_desc_handler at32_msc_desc_handler =
{
  get_device_descriptor,
  get_device_qualifier,
  get_device_configuration,
  get_device_other_speed,
  get_device_lang_id,
  get_device_manufacturer_string,
  get_device_product_string,
  get_device_serial_string,
  get_device_interface_string,
  get_device_config_string,
};

ALIGNED_HEAD static uint8_t g_usbd_desc_buffer[256] ALIGNED_TAIL;

// USB device standard descriptor
ALIGNED_HEAD static uint8_t g_usbd_descriptor[USB_DEVICE_DESC_LEN] ALIGNED_TAIL =
{
  USB_DEVICE_DESC_LEN,                   /* bLength */
  USB_DESCIPTOR_TYPE_DEVICE,             /* bDescriptorType */
  0x00,                                  /* bcdUSB */
  0x02,
  0x00,                                  /* bDeviceClass */
  0x00,                                  /* bDeviceSubClass */
  0x00,                                  /* bDeviceProtocol */
  USB_MAX_EP0_SIZE,                      /* bMaxPacketSize */
  LBYTE(USBD_MSC_VID),                   /* idVendor */
  HBYTE(USBD_MSC_VID),
  LBYTE(USBD_MSC_PID),                   /* idProduct */
  HBYTE(USBD_MSC_PID),
  0x00,                                  /* bcdDevice rel. 2.00 */
  0x02,
  USB_MFC_STRING,                        /* Index of manufacturer string */
  USB_PRODUCT_STRING,                    /* Index of product string */
  USB_SERIAL_STRING,                     /* Index of serial number string */
  1                                      /* bNumConfigurations */
};

// USB configuration standard descriptor
ALIGNED_HEAD static uint8_t g_usbd_configuration[USBD_MSC_CONFIG_DESC_SIZE] ALIGNED_TAIL =
{
  USB_DEVICE_CFG_DESC_LEN,               /* bLength: configuration descriptor size */
  USB_DESCIPTOR_TYPE_CONFIGURATION,      /* bDescriptorType: configuration */
  LBYTE(USBD_MSC_CONFIG_DESC_SIZE),      /* wTotalLength: bytes returned */
  HBYTE(USBD_MSC_CONFIG_DESC_SIZE),
  0x01,                                  /* bNumInterfaces */
  0x01,                                  /* bConfigurationValue */
  0x04,                                  /* iConfiguration */
  0xC0,                                  /* bmAttributes: self powered */
  0x32,                                  /* MaxPower 100 mA */

  USB_DEVICE_IF_DESC_LEN,                /* bLength: interface descriptor size */
  USB_DESCIPTOR_TYPE_INTERFACE,          /* bDescriptorType: interface */
  0x00,                                  /* bInterfaceNumber */
  0x00,                                  /* bAlternateSetting */
  0x02,                                  /* bNumEndpoints */
  USB_CLASS_CODE_MSC,                    /* bInterfaceClass: MSC */
  0x06,                                  /* bInterfaceSubClass: SCSI */
  0x50,                                  /* bInterfaceProtocol: BBB */
  0x05,                                  /* iInterface */

  USB_DEVICE_EPT_LEN,                    /* bLength: size of endpoint descriptor */
  USB_DESCIPTOR_TYPE_ENDPOINT,           /* bDescriptorType: endpoint */
  USBD_MSC_BULK_IN_EPT,                  /* bEndpointAddress */
  USB_EPT_DESC_BULK,                     /* bmAttributes: bulk */
  LBYTE(USBD_IN_MAXPACKET_SIZE),
  HBYTE(USBD_IN_MAXPACKET_SIZE),
  0x00,                                  /* bInterval */

  USB_DEVICE_EPT_LEN,                    /* bLength: size of endpoint descriptor */
  USB_DESCIPTOR_TYPE_ENDPOINT,           /* bDescriptorType: endpoint */
  USBD_MSC_BULK_OUT_EPT,                 /* bEndpointAddress */
  USB_EPT_DESC_BULK,                     /* bmAttributes: bulk */
  LBYTE(USBD_OUT_MAXPACKET_SIZE),
  HBYTE(USBD_OUT_MAXPACKET_SIZE),
  0x00,                                  /* bInterval */
};

// USB string lang id
ALIGNED_HEAD static uint8_t g_string_lang_id[USBD_MSC_SIZ_STRING_LANGID] ALIGNED_TAIL =
{
  USBD_MSC_SIZ_STRING_LANGID,
  USB_DESCIPTOR_TYPE_STRING,
  0x09,
  0x04,
};

// USB string serial
ALIGNED_HEAD static uint8_t g_string_serial[USBD_MSC_SIZ_STRING_SERIAL] ALIGNED_TAIL =
{
  USBD_MSC_SIZ_STRING_SERIAL,
  USB_DESCIPTOR_TYPE_STRING,
};

static usbd_desc_t device_descriptor =
{
  USB_DEVICE_DESC_LEN,
  g_usbd_descriptor
};

static usbd_desc_t config_descriptor =
{
  USBD_MSC_CONFIG_DESC_SIZE,
  g_usbd_configuration
};

static usbd_desc_t langid_descriptor =
{
  USBD_MSC_SIZ_STRING_LANGID,
  g_string_lang_id
};

static usbd_desc_t serial_descriptor =
{
  USBD_MSC_SIZ_STRING_SERIAL,
  g_string_serial
};

static usbd_desc_t vp_desc;

// Convert an ASCII string to a USB unicode string descriptor.
static uint16_t usbd_unicode_convert(uint8_t *string, uint8_t *unicode_buf)
{
  uint16_t str_len = 0, id_pos = 2;
  uint8_t *tmp_str = string;

  while (*tmp_str != '\0') {
    str_len++;
    unicode_buf[id_pos++] = *tmp_str++;
    unicode_buf[id_pos++] = 0x00;
  }

  str_len = str_len * 2 + 2;
  unicode_buf[0] = (uint8_t)str_len;
  unicode_buf[1] = USB_DESCIPTOR_TYPE_STRING;

  return str_len;
}

// Convert an integer to a unicode string.
static void usbd_int_to_unicode(uint32_t value, uint8_t *pbuf, uint8_t len)
{
  uint8_t idx = 0;

  for (idx = 0; idx < len; idx++) {
    if (((value >> 28)) < 0xA) {
      pbuf[2 * idx] = (value >> 28) + '0';
    } else {
      pbuf[2 * idx] = (value >> 28) + 'A' - 10;
    }
    value = value << 4;
    pbuf[2 * idx + 1] = 0;
  }
}

// Generate a serial number from the AT32F435 unique ID.
static void get_serial_num(void)
{
  uint32_t serial0, serial1, serial2;

  serial0 = *(uint32_t *)MCU_ID1;
  serial1 = *(uint32_t *)MCU_ID2;
  serial2 = *(uint32_t *)MCU_ID3;

  serial0 += serial2;

  if (serial0 != 0) {
    usbd_int_to_unicode(serial0, &g_string_serial[2], 8);
    usbd_int_to_unicode(serial1, &g_string_serial[18], 4);
  }
}

static usbd_desc_t *get_device_descriptor(void)
{
  return &device_descriptor;
}

static usbd_desc_t *get_device_qualifier(void)
{
  return NULL;
}

static usbd_desc_t *get_device_configuration(void)
{
  return &config_descriptor;
}

static usbd_desc_t *get_device_other_speed(void)
{
  return NULL;
}

static usbd_desc_t *get_device_lang_id(void)
{
  return &langid_descriptor;
}

static usbd_desc_t *get_device_manufacturer_string(void)
{
  vp_desc.length = usbd_unicode_convert((uint8_t *)USBD_MSC_DESC_MANUFACTURER_STRING, g_usbd_desc_buffer);
  vp_desc.descriptor = g_usbd_desc_buffer;
  return &vp_desc;
}

static usbd_desc_t *get_device_product_string(void)
{
  vp_desc.length = usbd_unicode_convert((uint8_t *)USBD_MSC_DESC_PRODUCT_STRING, g_usbd_desc_buffer);
  vp_desc.descriptor = g_usbd_desc_buffer;
  return &vp_desc;
}

static usbd_desc_t *get_device_serial_string(void)
{
  get_serial_num();
  return &serial_descriptor;
}

static usbd_desc_t *get_device_interface_string(void)
{
  vp_desc.length = usbd_unicode_convert((uint8_t *)USBD_MSC_DESC_INTERFACE_STRING, g_usbd_desc_buffer);
  vp_desc.descriptor = g_usbd_desc_buffer;
  return &vp_desc;
}

static usbd_desc_t *get_device_config_string(void)
{
  vp_desc.length = usbd_unicode_convert((uint8_t *)USBD_MSC_DESC_CONFIGURATION_STRING, g_usbd_desc_buffer);
  vp_desc.descriptor = g_usbd_desc_buffer;
  return &vp_desc;
}
