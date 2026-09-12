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

#pragma once

#include "at32f435_437.h"
#include <stdint.h>
#include "hal/gpio.h"

#define GPIO_UNDEF (0xffffffff)

// GPIO_PIN(x, y) encodes a GPIO port base address | pin number (0-15)
#define GPIO_PIN(x, y) ((uintptr_t)x | y)

// Generate GPIO mode bitfields
//
// bit 0+1: pin mode (input / output)
// bit 2+3: pull resistor configuration
// bit   4: output type (0: push-pull, 1: open-drain)
//
#define _GPIO_MODE(io, pr, ot) ((io << 0) | (pr << 2) | (ot << 4))

enum {
  GPIO_IN    = _GPIO_MODE(0, 0, 0),    // input w/o pull R
  GPIO_IN_PD = _GPIO_MODE(0, 2, 0),    // input with pull-down
  GPIO_IN_PU = _GPIO_MODE(0, 1, 0),    // input with pull-up
  GPIO_OUT   = _GPIO_MODE(1, 0, 0),    // push-pull output
  GPIO_OD    = _GPIO_MODE(1, 0, 1),    // open-drain w/o pull R
  GPIO_OD_PU = _GPIO_MODE(1, 1, 1)     // open-drain with pull-up
};

enum {
  GPIO_PIN_SPEED_LOW       = 0x00,
  GPIO_PIN_SPEED_MEDIUM    = 0x01,
  GPIO_PIN_SPEED_HIGH      = 0x02,
  GPIO_PIN_SPEED_VERY_HIGH = 0x03,
};

gpio_type* gpio_get_port(gpio_t pin);
uint32_t gpio_get_pin(gpio_t pin);
