/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * AT32 module timer driver (WIP).
 *
 * NOTE: protocol pulse output (CRSF, etc.) via timer + DMA is not yet ported.
 * This stub lets the firmware link; module pulse output must be implemented
 * using the AT32 TMR + DMA.
 */

#include "at32_module_timer_driver.h"

static void* module_timer_init(void* hw_def, const etx_timer_config_t* cfg)
{
  (void)cfg;
  return hw_def;
}

static void module_timer_deinit(void* ctx)
{
  (void)ctx;
}

static void module_timer_send(void* ctx, const etx_timer_config_t* cfg,
                              const void* pulses, uint16_t length)
{
  (void)ctx; (void)cfg; (void)pulses; (void)length;
}

static bool module_timer_tx_complete(void* ctx)
{
  (void)ctx;
  return true;
}

const etx_timer_driver_t STM32ModuleTimerDriver = {
  .init = module_timer_init,
  .deinit = module_timer_deinit,
  .send = module_timer_send,
  .txCompleted = module_timer_tx_complete,
};
