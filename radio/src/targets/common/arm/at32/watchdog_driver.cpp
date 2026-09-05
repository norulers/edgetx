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

#include "hal/watchdog_driver.h"

#include "at32f435_437.h"

// AT32 independent watchdog (WDT) uses the internal LICK (~40 kHz).
// With WDT_CLK_DIV_32 the counter clock is ~1250 Hz (0.8 ms per LSB).
#define WDT_MS_TO_RELOAD(ms)  (((ms) / 0.8f))

void watchdogInit(unsigned int duration)
{
  wdt_register_write_enable(TRUE);
  wdt_divider_set(WDT_CLK_DIV_32);

  wdt_register_write_enable(TRUE);
  wdt_reload_value_set((uint16_t)WDT_MS_TO_RELOAD(duration));

  wdt_counter_reload();
  wdt_enable();
}

void watchdogReset()
{
  wdt_counter_reload();
}
