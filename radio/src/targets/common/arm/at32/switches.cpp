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
 * AT32 switch driver board glue.
 *
 * Provides the board-level switch API required by hal/switch_driver.cpp.
 * The `_switch_defs` / `n_switches` tables are generated from the target's
 * JSON hardware description (boards/hw_defs/<flavour>.json -> stm32_switches.inc).
 */

#include "hal/switch_driver.h"
#include "at32_switch_driver.h"

#include "definitions.h"
#include "edgetx_helpers.h"

// `__weak` is normally provided by the STM32 HAL headers, which the AT32 port
// does not use. Define it for the GCC/ARM toolchain used here.
#ifndef __weak
#define __weak __attribute__((weak))
#endif

// generated switch structs
#include "stm32_switches.inc"

__weak void boardInitSwitches()
{
  _init_switches();
}

__weak SwitchHwPos boardSwitchGetPosition(uint8_t idx)
{
  if (idx >= n_switches) return SWITCH_HW_UP;
  const stm32_switch_t* sw = &_switch_defs[idx];
  return stm32_switch_get_position(sw);
}

__weak const char* boardSwitchGetName(uint8_t idx)
{
  if (idx >= n_switches) return nullptr;
  return _switch_defs[idx].name;
}

__weak SwitchHwType boardSwitchGetType(uint8_t idx)
{
  if (idx >= n_switches) return SWITCH_HW_2POS;
  return _switch_defs[idx].type;
}

const stm32_switch_t* boardGetSwitchDef(uint8_t idx)
{
  if (idx >= n_switches) return nullptr;
  return &_switch_defs[idx];
}

uint8_t boardGetMaxSwitches() { return n_switches; }

#if defined(FUNCTION_SWITCHES)
bool boardIsCustomSwitch(uint8_t idx)
{
  return (idx < n_switches) ? _switch_defs[idx].isCustomSwitch : false;
}
uint8_t boardGetCustomSwitchIdx(uint8_t idx)
{
  if (idx >= n_switches) return 0;
  return _switch_defs[idx].customSwitchIdx;
}
#endif

SwitchConfig boardSwitchGetDefaultConfig(uint8_t idx)
{
  if (idx >= n_switches) return SWITCH_NONE;
  return _switch_defs[idx].defaultType;
}

#if !defined(COLORLCD)
switch_display_pos_t switchGetDisplayPosition(uint8_t idx)
{
  (void)idx;
  // AT32 scaffold: no display-positioned switches are configured yet
  // (boards/hw_defs/at32f435.json -> "switches": []), so no `_switch_display`
  // table is generated. Keep a safe default until the board is described.
  return {0, 0};
}
#endif
