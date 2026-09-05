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

#include "board.h"
#include "hal.h"

#include "at32f435_437.h"

/*
 * AT32F435BSP GPIO setup.
 *
 * The pin map below follows the taranis F407ZG (LQFP144) target - AT32F435 is
 * pin-compatible with STM32F407. Confirm each assignment against the board.
 */

#if !defined(SIMU)

// Configure a single output pin (push-pull, strong drive, no pull)
static void gpioConfigOut(gpio_t pin)
{
  gpio_type* port = gpio_get_port(pin);
  uint16_t p = (uint16_t)(1U << gpio_get_pin(pin));
  gpio_init_type init;
  gpio_default_para_init(&init);
  init.gpio_mode          = GPIO_MODE_OUTPUT;
  init.gpio_out_type      = GPIO_OUTPUT_PUSH_PULL;
  init.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
  init.gpio_pull          = GPIO_PULL_NONE;
  init.gpio_pins          = p;
  gpio_init(port, &init);
}

// Configure a single input pin with pull-up
static void gpioConfigInPullUp(gpio_t pin)
{
  gpio_type* port = gpio_get_port(pin);
  uint16_t p = (uint16_t)(1U << gpio_get_pin(pin));
  gpio_init_type init;
  gpio_default_para_init(&init);
  init.gpio_mode          = GPIO_MODE_INPUT;
  init.gpio_out_type      = GPIO_OUTPUT_PUSH_PULL;
  init.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
  init.gpio_pull          = GPIO_PULL_UP;
  init.gpio_pins          = p;
  gpio_init(port, &init);
}

extern "C" void bspGpioInit()
{
  // Enable clock for the ports used on the board
  crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_GPIOB_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_GPIOC_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_GPIOD_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_GPIOE_PERIPH_CLOCK, TRUE);

  // External module power (PD.08) - off by default
  gpioConfigOut(GPIO_PIN(GPIOD, 8));
  gpio_set(EXTMODULE_PWR_GPIO);

  // LED indicators (PC.13/14/15) - off by default
  gpioConfigOut(GPIO_LED_RED);
  gpioConfigOut(GPIO_LED_GREEN);
  gpioConfigOut(GPIO_LED_BLUE);
  gpio_set(GPIO_LED_RED);
  gpio_set(GPIO_LED_GREEN);
  gpio_set(GPIO_LED_BLUE);

  // LCD control pins (NCS PA.15, A0 PC.11, RST PD.15)
  gpioConfigOut(LCD_NCS_GPIO);
  gpioConfigOut(LCD_A0_GPIO);
  gpioConfigOut(LCD_RST_GPIO);
  gpio_set(LCD_NCS_GPIO);
  gpio_set(LCD_A0_GPIO);
  gpio_set(LCD_RST_GPIO);

  // BIND key (PD.09) - input with pull-up
  gpioConfigInPullUp(GPIO_PIN(GPIOD, KEYS_GPIO_PIN_BIND));

  // Rotary encoder (PE.10 / PE.11) - input with pull-up
  gpioConfigInPullUp(GPIO_PIN(GPIOE, 10));
  gpioConfigInPullUp(GPIO_PIN(GPIOE, 11));
}

#endif // !defined(SIMU)

