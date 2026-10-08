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

#include "timers_driver.h"
#include "at32f435_437.h"

#include "hal.h"
#include "hal/watchdog_driver.h"

static volatile uint32_t _ms_ticks;

static void _init_1ms_timer()
{
  crm_periph_clock_enable(CRM_TMR14_PERIPH_CLOCK, TRUE);
  if ((MS_TIMER->ctrl1 & 0x1) == 0x1) return; // already running

  _ms_ticks = 0;

  // timer clock = PERI1_FREQUENCY * TIMER_MULT_APB1
  // pre-scaler to 1 MHz (1 uS per tick), period = 999 -> 1 ms
  tmr_base_init(MS_TIMER, 999, ((PERI1_FREQUENCY * TIMER_MULT_APB1) / 1000000) - 1);

  tmr_flag_clear(MS_TIMER, TMR_OVF_FLAG);
  tmr_interrupt_enable(MS_TIMER, TMR_OVF_INT, TRUE);

  tmr_counter_enable(MS_TIMER, TRUE);

  NVIC_EnableIRQ(MS_TIMER_IRQn);
  NVIC_SetPriority(MS_TIMER_IRQn, 0);
}

void timersInit()
{
  _init_1ms_timer();
}

uint32_t timersGetMsTick()
{
  return _ms_ticks;
}

uint32_t timersGetUsTick()
{
  uint32_t ms;
  uint32_t us;

  do {
    ms = _ms_ticks;
    us = MS_TIMER->cval;
    asm volatile("nop");
    asm volatile("nop");
  } while (ms != _ms_ticks);

  return ms * 1000 + us;
}

static volatile uint32_t watchdogTimeout = 0;

void watchdogSuspend(uint32_t timeout)
{
  watchdogTimeout = timeout;
}

// The 1 ms ISR schedules per5ms(). The bootloader provides its own
// implementation in boot_menu.cpp, and the firmware provides it via
// haptic.cpp when HAPTIC=YES. Provide a stub for the firmware when haptic
// is disabled so the timer ISR still links.
#if !defined(BOOT) && !defined(HAPTIC)
void per5ms()
{
}
#endif

static inline void _interrupt_1ms()
{
  static uint8_t pre_scale = 0;

  ++pre_scale;
  ++_ms_ticks;

  __DSB();
  __ISB();

  // 5ms loop
  if(pre_scale == 5 || pre_scale == 10) {
    per5ms();
  }

  // 10ms loop
  if (pre_scale == 10) {
    pre_scale = 0;

    if (watchdogTimeout) {
      watchdogTimeout -= 1;
      WDG_RESET();  // Retrigger hardware watchdog
    }
  }
}

extern "C" void MS_TIMER_IRQHandler()
{
  tmr_flag_clear(MS_TIMER, TMR_OVF_FLAG);
  _interrupt_1ms();
}
