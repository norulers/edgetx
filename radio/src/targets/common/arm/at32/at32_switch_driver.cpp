/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * AT32 switch driver (WIP).
 */

#include "at32_switch_driver.h"

SwitchHwPos stm32_switch_get_position(const stm32_switch_t* sw)
{
  if (!sw) return SWITCH_HW_MID;

  if (sw->GPIOx_high && sw->GPIOx_low) {
    bool ph = gpio_read(GPIO_PIN(sw->GPIOx_high, sw->Pin_high));
    bool pl = gpio_read(GPIO_PIN(sw->GPIOx_low, sw->Pin_low));
    if (ph && pl) return SWITCH_HW_MID;
    if (ph && !pl) return SWITCH_HW_UP;
    if (!ph && pl) return SWITCH_HW_DOWN;
    return SWITCH_HW_MID;
  }

  if (sw->GPIOx_high) {
    return gpio_read(GPIO_PIN(sw->GPIOx_high, sw->Pin_high))
             ? SWITCH_HW_UP : SWITCH_HW_DOWN;
  }

  return SWITCH_HW_MID;
}

bool stm32_switch_get_state(const stm32_switch_t* sw, SwitchHwPos pos)
{
  if (!sw) return false;
  SwitchHwPos p = stm32_switch_get_position(sw);
  if (sw->inverted) {
    return !(p == pos);
  }
  return (p == pos);
}
