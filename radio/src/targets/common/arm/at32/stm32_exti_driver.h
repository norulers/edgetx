/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * AT32 implementation of the EdgeTX EXTI driver interface.
 * (mirrors targets/common/arm/stm32/stm32_exti_driver.h)
 */

#pragma once

#include <stdint.h>

typedef void (*stm32_exti_handler_t)();

// Set callback and enable IRQ
void stm32_exti_enable(uint32_t line, uint8_t trigger, stm32_exti_handler_t cb);

// Reset callback and disable IRQ if no more handlers
void stm32_exti_disable(uint32_t line);

// trigger software interrupt
void stm32_exti_trigger_swi(uint32_t line);

#if defined(USE_CUSTOM_EXTI_IRQ)
void stm32_exti_custom_enable(uint32_t line, uint8_t trigger, stm32_exti_handler_t cb);
void stm32_exti_custom_disable(uint32_t line);
void stm32_exti_custom_trigger_swi(uint32_t line);
#endif
