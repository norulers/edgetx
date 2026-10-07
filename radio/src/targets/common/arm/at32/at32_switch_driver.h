/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 */

#pragma once

#include "hal.h"
#include "at32f435_437.h"
#include "hal/switch_driver.h"
#include "edgetx_constants.h"

// NOTE: the following type/functions keep the historical "stm32_" names to
// remain compatible with the EdgeTX core code that references them.
struct stm32_switch_t
{
  const char*   name;

  gpio_type*    GPIOx_high;
  uint32_t      Pin_high;

  gpio_type*    GPIOx_low;
  uint32_t      Pin_low;

  SwitchHwType  type;
  bool          inverted;
  SwitchConfig  defaultType;

#if defined(FUNCTION_SWITCHES)
  bool          isCustomSwitch;
  uint8_t       customSwitchIdx;
#endif
};

SwitchHwPos stm32_switch_get_position(const stm32_switch_t* sw);
bool stm32_switch_get_state(const stm32_switch_t* sw, SwitchHwPos pos);
