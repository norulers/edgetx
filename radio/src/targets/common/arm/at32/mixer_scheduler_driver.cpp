/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 */

#include "mixer_scheduler.h"
#include "at32f435_437.h"

#include "FreeRTOSConfig.h"
#include "hal.h"

// Start scheduler with default period
void mixerSchedulerStart()
{
  crm_periph_clock_enable(CRM_TMR12_PERIPH_CLOCK, TRUE);

  tmr_counter_enable(MIXER_SCHEDULER_TIMER, FALSE);
  tmr_base_init(MIXER_SCHEDULER_TIMER,
                getMixerSchedulerPeriod() - 1,
                (MIXER_SCHEDULER_TIMER_FREQ / 1000000) - 1); // 1uS timer

  tmr_flag_clear(MIXER_SCHEDULER_TIMER, TMR_OVF_FLAG);

  NVIC_EnableIRQ(MIXER_SCHEDULER_TIMER_IRQn);
  NVIC_SetPriority(MIXER_SCHEDULER_TIMER_IRQn,
                   configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY);

  tmr_interrupt_enable(MIXER_SCHEDULER_TIMER, TMR_OVF_INT, TRUE);
  tmr_counter_enable(MIXER_SCHEDULER_TIMER, TRUE);
}

void mixerSchedulerStop()
{
  tmr_counter_enable(MIXER_SCHEDULER_TIMER, FALSE);
  NVIC_DisableIRQ(MIXER_SCHEDULER_TIMER_IRQn);
}

void mixerSchedulerEnableTrigger()
{
  tmr_interrupt_enable(MIXER_SCHEDULER_TIMER, TMR_OVF_INT, TRUE);
}

void mixerSchedulerDisableTrigger()
{
  tmr_interrupt_enable(MIXER_SCHEDULER_TIMER, TMR_OVF_INT, FALSE);
}

void mixerSchedulerSoftTrigger()
{
  // Generate a software update event to fire the interrupt immediately
  tmr_event_sw_trigger(MIXER_SCHEDULER_TIMER, (tmr_event_trigger_type)TMR_OVERFLOW_SWTRIG);
}

extern "C" void MIXER_SCHEDULER_TIMER_IRQHandler(void)
{
  tmr_flag_clear(MIXER_SCHEDULER_TIMER, TMR_OVF_FLAG);
  mixerSchedulerDisableTrigger();

  // set next period
  tmr_period_value_set(MIXER_SCHEDULER_TIMER, getMixerSchedulerPeriod() - 1);

  // trigger mixer start
  mixerSchedulerISRTrigger();
}
