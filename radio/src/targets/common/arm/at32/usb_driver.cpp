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
 * AT32 USB driver.
 *
 * Implements the EdgeTX USB HAL (hal/usb_driver.h) on top of the Artery
 * AT32F435 USB OTG + USB device library. Currently only the mass-storage
 * class (MSC) is wired up.
 */

#include "hal/usb_driver.h"
#include "hal/gpio.h"

#include "at32f435_437.h"
#include "at32f435_437_usb.h"   // usb_reg_type / usb_ept_info
#include "at32_gpio.h"

#include "usb_conf.h"
#include "usb_core.h"
#include "usbd_core.h"
#include "usbd_int.h"
#include "msc_class.h"

#include "usbd_desc.h"

#include "delays_driver.h"
#include "hal.h"
#include "debug.h"

// Single OTG core for the AT32F435 (full-speed OTG1)
static otg_core_type otg_core;

static bool usbDriverStarted = false;

#if defined(BOOT)
static usbMode selectedUsbMode = USB_MASS_STORAGE_MODE;
#else
static usbMode selectedUsbMode = USB_UNSELECTED_MODE;
#endif

int getSelectedUsbMode()
{
  return selectedUsbMode;
}

void setSelectedUsbMode(int mode)
{
  selectedUsbMode = usbMode(mode);
}

#if defined(USB_GPIO_VBUS)
static uint8_t _usbVbusDebounce = 0;
static uint8_t _usbVbusLast = 0;
#endif

int usbPlugged()
{
#if defined(DEBUG_DISABLE_USB) && !defined(BOOT)
  return false;
#endif

#if defined(USB_GPIO_VBUS)
  uint8_t state = gpio_read(USB_GPIO_VBUS) ? 1 : 0;
  if (state == _usbVbusLast)
    _usbVbusDebounce = state;
  else
    _usbVbusLast = state;
  return _usbVbusDebounce;
#else
  // No VBUS sense pin wired up: assume plugged in.
  return 1;
#endif
}

void usbInit()
{
  // Enable USB OTG1 clock
  crm_periph_clock_enable(CRM_OTGFS1_PERIPH_CLOCK, TRUE);

  // Route the USB 48 MHz clock from the PLL (288 MHz / 6 = 48 MHz)
  crm_usb_clock_source_select(CRM_USB_CLOCK_SOURCE_PLL);
  crm_usb_clock_div_set(CRM_USB_DIV_6);

  // USB DM / DP pins (muxed to OTG1 FS)
  gpio_init_af(USB_GPIO_DM, USB_GPIO_AF, GPIO_PIN_SPEED_VERY_HIGH);
  gpio_init_af(USB_GPIO_DP, USB_GPIO_AF, GPIO_PIN_SPEED_VERY_HIGH);

#if defined(USB_GPIO_VBUS)
  gpio_init(USB_GPIO_VBUS, GPIO_IN_PU, GPIO_PIN_SPEED_LOW);
  // prime debounce state...
  usbPlugged();
#endif

  // USB OTG1 interrupt
  nvic_irq_enable(OTGFS1_IRQn, 5, 0);

  usbDriverStarted = false;
}

void usbStart()
{
  // Only the mass-storage class is currently wired up on AT32.
  if (getSelectedUsbMode() != USB_MASS_STORAGE_MODE)
    return;

  // Initialize the USB device core (OTG1 / full-speed) with the MSC class and
  // the board descriptors. usbd_init() also performs the bus connect.
  if (usbd_init(&otg_core, USB_FULL_SPEED_CORE_ID, USB_OTG1_ID,
                &msc_class_handler, &at32_msc_desc_handler) == USB_OK) {
    usbDriverStarted = true;
  }
}

void usbStop()
{
  usbDriverStarted = false;
  usbd_disconnect(&otg_core.dev);
}

bool usbStarted()
{
  return usbDriverStarted;
}

extern "C" void OTGFS1_IRQHandler()
{
  DEBUG_INTERRUPT(INT_OTG_FS);
  usbd_irq_handler(&otg_core);
}

// The AT32 USB middleware (usbd_core.c) calls usb_delay_ms(); provide it on
// top of the EdgeTX delay driver.
extern "C" void usb_delay_ms(uint32_t ms)
{
  delay_ms(ms);
}
