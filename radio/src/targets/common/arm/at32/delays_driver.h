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

#include <stdint.h>
#include "definitions.h"

EXTERN_C(void delaysInit());
EXTERN_C(void delay_01us(uint32_t count));
EXTERN_C(void delay_us(uint32_t count));
EXTERN_C(void delay_ms(uint32_t count));
EXTERN_C(uint32_t ticksNow());
