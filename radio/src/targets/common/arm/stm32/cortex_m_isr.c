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
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "cortex_m_isr.h"
#include "stm32_cmsis.h"
#include "hal/crash_dump.h"

#define GET_VECTACTIVE() \
  ((SCB->ICSR & SCB_ICSR_VECTACTIVE_Msk) >> SCB_ICSR_VECTACTIVE_Pos)

// Pass the stacked exception frame (MSP or PSP, depending on EXC_RETURN),
// EXC_RETURN, the fault type and r4-r11 of the faulting context (pushed here,
// they are not part of the hardware frame) to fault_handler_c()
#define FAULT_HANDLING_ASM(type)   \
  __asm volatile(                  \
      "tst lr, #4 \n"              \
      "ite eq \n"                  \
      "mrseq r0, msp \n"           \
      "mrsne r0, psp \n"           \
      "mov r1, lr \n"              \
      "movs r2, #" #type " \n"     \
      "push {r4-r11} \n"           \
      "mov r3, sp \n"              \
      "b fault_handler_c \n")

#define HALT_IF_DEBUGGING()                                 \
  do {                                                      \
    if (CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk) { \
      __asm("bkpt 1");                                      \
    }                                                       \
  } while (0)

// Only linked into firmware builds (crash_dump.cpp) / boards that report
// faults, null otherwise
__attribute__((weak)) void crashDumpFault(uint32_t type, const uint32_t* frame,
                                          uint32_t excReturn,
                                          const uint32_t* regs);
__attribute__((weak)) void boardFaultHook(uint32_t type);

__attribute__((optimize("O0")))
void default_isr_handler()
{
  if (crashDumpFault) crashDumpFault(CRASH_TYPE_UNHANDLED_IRQ, 0, 0, 0);

  /* Halt in debugger if connected */
  if (CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk) {
    uint32_t active_irq = GET_VECTACTIVE();
    if (active_irq >= NVIC_USER_IRQ_OFFSET) {
      /* External interrupt: check IRQn_Type for number */
      active_irq -= NVIC_USER_IRQ_OFFSET;
      __asm__("bkpt 1");
    } else {
      /* Cortex-M Exception */
      __asm__("bkpt 2");
    }
  }
  while (1) {}
}

// Records the fault, then waits for the watchdog to reset the radio (which
// boots in emergency mode, exactly as without the capture)
__attribute__((used, noinline))
void fault_handler_c(const uint32_t* frame, uint32_t excReturn, uint32_t type,
                     const uint32_t* regs)
{
  if (crashDumpFault) crashDumpFault(type, frame, excReturn, regs);
  if (boardFaultHook) boardFaultHook(type);
  HALT_IF_DEBUGGING();
  while (1) {}
}

__attribute__((naked)) void HardFault_Handler(void)
{
  FAULT_HANDLING_ASM(1);  // CRASH_TYPE_HARDFAULT
}

__attribute__((naked)) void MemManage_Handler(void)
{
  FAULT_HANDLING_ASM(2);  // CRASH_TYPE_MEMMANAGE
}

__attribute__((naked)) void BusFault_Handler(void)
{
  FAULT_HANDLING_ASM(3);  // CRASH_TYPE_BUSFAULT
}

__attribute__((naked)) void UsageFault_Handler(void)
{
  FAULT_HANDLING_ASM(4);  // CRASH_TYPE_USAGEFAULT
}
