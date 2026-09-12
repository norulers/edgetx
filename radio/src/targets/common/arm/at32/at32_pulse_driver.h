/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "at32f435_437.h"
#include "hal/gpio.h"

// NOTE: keeps the historical "stm32_" names for core compatibility.
struct stm32_pulse_timer_t;

struct stm32_pulse_dma_tc_cb_t {
  void (*cb)(void*);
  void* ctx;
};

struct stm32_pulse_timer_t {
  gpio_t        GPIO;
  uint32_t      GPIO_Alternate;
  tmr_type*     TIMx;
  uint32_t      TIM_Freq;
  uint32_t      TIM_Channel;
  IRQn_Type     TIM_IRQn;
  dma_type*     DMAx;
  uint32_t      DMA_Stream;
  uint32_t      DMA_Channel;
  IRQn_Type     DMA_IRQn;
  stm32_pulse_dma_tc_cb_t* DMA_TC_CallbackPtr;
};

int  stm32_pulse_init(const stm32_pulse_timer_t* tim, uint32_t freq);
void stm32_pulse_deinit(const stm32_pulse_timer_t* tim);
void stm32_pulse_config_input(const stm32_pulse_timer_t* tim);
void stm32_pulse_config_output(const stm32_pulse_timer_t* tim, bool polarity,
                               uint32_t ocmode, uint32_t cmp_val);
void stm32_pulse_set_polarity(const stm32_pulse_timer_t* tim, bool polarity);
bool stm32_pulse_get_polarity(const stm32_pulse_timer_t* tim);
void stm32_pulse_set_period(const stm32_pulse_timer_t* tim, uint32_t period);
void stm32_pulse_set_cmp_val(const stm32_pulse_timer_t* tim, uint32_t cmp_val);
void stm32_pulse_start(const stm32_pulse_timer_t* tim);
void stm32_pulse_stop(const stm32_pulse_timer_t* tim);
bool stm32_pulse_is_completed(const stm32_pulse_timer_t* tim);
void stm32_pulse_wait_for_completed(const stm32_pulse_timer_t* tim);
bool stm32_pulse_if_not_running_disable(const stm32_pulse_timer_t* tim);
void stm32_pulse_start_dma_req(const stm32_pulse_timer_t* tim,
                               const void* pulses, uint16_t length,
                               uint32_t ocmode, uint32_t cmp_val);
void stm32_pulse_dma_tc_isr(const stm32_pulse_timer_t* tim);
void stm32_pulse_tim_update_isr(const stm32_pulse_timer_t* tim);
