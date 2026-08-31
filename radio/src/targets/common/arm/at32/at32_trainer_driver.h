/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 */

#pragma once

#include "at32_pulse_driver.h"

void trainer_init();
void trainer_init_capture(const stm32_pulse_timer_t* tim);
void trainer_init_output(const stm32_pulse_timer_t* tim);
void trainer_stop();
void trainer_timer_isr();
