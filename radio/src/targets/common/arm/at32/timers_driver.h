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

#pragma once

#include "edgetx_types.h"

extern "C" volatile tmr10ms_t g_tmr10ms;
static inline tmr10ms_t get_tmr10ms() { return g_tmr10ms; }

void watchdogSuspend(uint32_t timeout);

void timersInit();

uint32_t timersGetMsTick();
uint32_t timersGetUsTick();

// declared "weak", to be implemented by application
void per5ms();
void per10ms();
