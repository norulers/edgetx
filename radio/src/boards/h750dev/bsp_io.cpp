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
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

// IO expander (PCA9539 / TCA9539) is NOT populated on this dev board.
// All IO expander logic is disabled. Switches and trims return safe defaults:
//   - bsp_get_fs_switches(): all bits HIGH (0xFFFF) = all trims released
//   - boardSwitchGetPosition(): SWITCH_HW_MID for 3-pos, SWITCH_HW_UP for 2-pos

#include "bsp_io.h"
#include "stm32_rgbleds.h"
#include "stm32_switch_driver.h"
#include "hal/switch_driver.h"
#include "hal.h"
#include "debug.h"

extern const stm32_switch_t* boardGetSwitchDef(uint8_t idx);

// All bits HIGH = all switches/trims open (active-LOW: 1 = not pressed)
static constexpr uint32_t IO_EXPANDER_SAFE_STATE = 0xFFFF;

int bsp_io_init()
{
  // IO expander not populated — nothing to initialise.
  return 0;
}

uint32_t bsp_get_fs_switches()
{
  // Return all-HIGH: every trim bit is 1 → (keys & BSP_TRxx) != 0 → not pressed.
  return IO_EXPANDER_SAFE_STATE;
}

void boardInitSwitches()
{
  bsp_io_init();
}

bool boardIsCustomSwitch(uint8_t idx);

SwitchHwPos boardSwitchGetPosition(uint8_t idx)
{
  const stm32_switch_t* def = boardGetSwitchDef(idx);
  if (!def) return SWITCH_HW_UP;

  if (def->isCustomSwitch) {
    // Custom (function) switch: Pin_high=1 → not pressed → UP
    return SWITCH_HW_UP;
  } else if (!def->Pin_low) {
    // 2-pos switch: default UP
    return SWITCH_HW_UP;
  } else {
    // 3-pos switch: default MID (both pins HIGH in active-LOW = middle)
    return SWITCH_HW_MID;
  }
}


