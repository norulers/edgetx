/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * AT32 pulse driver (WIP).
 *
 * NOTE: protocol pulse generation (timer PWM + DMA) is not yet ported for
 * AT32F435. Stubs preserve the API so the firmware links.
 */

#include "at32_pulse_driver.h"

int stm32_pulse_init(const stm32_pulse_timer_t* tim, uint32_t freq)
{
  (void)tim; (void)freq;
  return 0;
}

void stm32_pulse_deinit(const stm32_pulse_timer_t* tim)
{
  (void)tim;
}

void stm32_pulse_config_input(const stm32_pulse_timer_t* tim)
{
  (void)tim;
}

void stm32_pulse_config_output(const stm32_pulse_timer_t* tim, bool polarity,
                               uint32_t ocmode, uint32_t cmp_val)
{
  (void)tim; (void)polarity; (void)ocmode; (void)cmp_val;
}

void stm32_pulse_set_polarity(const stm32_pulse_timer_t* tim, bool polarity)
{
  (void)tim; (void)polarity;
}

bool stm32_pulse_get_polarity(const stm32_pulse_timer_t* tim)
{
  (void)tim;
  return false;
}

void stm32_pulse_set_period(const stm32_pulse_timer_t* tim, uint32_t period)
{
  (void)tim; (void)period;
}

void stm32_pulse_set_cmp_val(const stm32_pulse_timer_t* tim, uint32_t cmp_val)
{
  (void)tim; (void)cmp_val;
}

void stm32_pulse_start(const stm32_pulse_timer_t* tim)
{
  (void)tim;
}

void stm32_pulse_stop(const stm32_pulse_timer_t* tim)
{
  (void)tim;
}

bool stm32_pulse_is_completed(const stm32_pulse_timer_t* tim)
{
  (void)tim;
  return true;
}

void stm32_pulse_wait_for_completed(const stm32_pulse_timer_t* tim)
{
  (void)tim;
}

bool stm32_pulse_if_not_running_disable(const stm32_pulse_timer_t* tim)
{
  (void)tim;
  return true;
}

void stm32_pulse_start_dma_req(const stm32_pulse_timer_t* tim,
                               const void* pulses, uint16_t length,
                               uint32_t ocmode, uint32_t cmp_val)
{
  (void)tim; (void)pulses; (void)length; (void)ocmode; (void)cmp_val;
}

void stm32_pulse_dma_tc_isr(const stm32_pulse_timer_t* tim)
{
  (void)tim;
}

void stm32_pulse_tim_update_isr(const stm32_pulse_timer_t* tim)
{
  (void)tim;
}
