/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * AT32 placeholder for the EdgeTX EXTI driver interface.
 *
 * TODO(WIP): implement on top of the AT32 EXINT peripheral
 * (at32f435_437_exint.c) once the board pin mapping is defined. These stubs
 * let the pulse layer (crossfire.cpp etc.) compile and link.
 */

#include "stm32_exti_driver.h"

void stm32_exti_enable(uint32_t line, uint8_t trigger, stm32_exti_handler_t cb)
{
  (void)line;
  (void)trigger;
  (void)cb;
}

void stm32_exti_disable(uint32_t line)
{
  (void)line;
}

void stm32_exti_trigger_swi(uint32_t line)
{
  (void)line;
}

#if defined(USE_CUSTOM_EXTI_IRQ)
void stm32_exti_custom_enable(uint32_t line, uint8_t trigger, stm32_exti_handler_t cb)
{
  (void)line;
  (void)trigger;
  (void)cb;
}

void stm32_exti_custom_disable(uint32_t line)
{
  (void)line;
}

void stm32_exti_custom_trigger_swi(uint32_t line)
{
  (void)line;
}
#endif
