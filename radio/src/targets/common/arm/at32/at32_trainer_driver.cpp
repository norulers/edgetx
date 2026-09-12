/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * AT32 trainer driver (WIP).
 */

#include "at32_trainer_driver.h"
#include "at32_pulse_driver.h"
#include "hal/trainer_driver.h"

void trainer_init()
{
}

void trainer_stop()
{
}

void trainer_init_capture(const stm32_pulse_timer_t* tim)
{
  (void)tim;
}

void trainer_init_output(const stm32_pulse_timer_t* tim)
{
  (void)tim;
}

void trainer_timer_isr()
{
}

// Board-level trainer (DSC) API - no trainer port wired on this scaffold yet
void board_trainer_init() { trainer_init(); }
bool trainer_dsc_available() { return false; }
void trainer_init_dsc_out() {}
void trainer_init_dsc_in() {}
void trainer_stop_dsc() { trainer_stop(); }
bool is_trainer_dsc_connected() { return false; }
void trainer_init_module_cppm() {}
void trainer_stop_module_cppm() {}
