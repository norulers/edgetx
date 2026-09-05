/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 */

#include "system_clock.h"
#include "at32f435_437.h"

/*
 * AT32F435 board clock setup.
 *
 * The AT32 SDK SystemInit() (system_at32f435_437.c) only resets the clock to
 * the internal HICK (8 MHz). This routine configures the system clock to
 * 288 MHz from the HEXT crystal (12 MHz, T20V2 consistent) using the AT32 CRM API.
 *
 * Set HEXT_VALUE to match the crystal actually fitted on the board
 * (targets/at32f435/CMakeLists.txt -> HSE_VALUE).
 */

#ifndef HEXT_VALUE
  #define HEXT_VALUE   12000000U
#endif

#define SYS_CLOCK_HZ  288000000U

void systemClockInit()
{
  // Enable external crystal and wait for it to stabilise
  crm_clock_source_enable(CRM_CLOCK_SOURCE_HEXT, TRUE);
  if (crm_hext_stable_wait() != SUCCESS) {
    // Crystal failed - stay on internal HICK (8 MHz) and bail
    return;
  }

  // AHB = 1, APB1 = /2, APB2 = /1 (typical AT32F435 bus clocking)
  crm_ahb_div_set(CRM_AHB_DIV_1);
  crm_apb1_div_set(CRM_APB1_DIV_2);
  crm_apb2_div_set(CRM_APB2_DIV_1);

  // Compute PLL parameters for the target core clock and configure it
  uint16_t ms = 0, ns = 0, fr = 0;
  if (crm_pll_parameter_calculate(CRM_PLL_SOURCE_HEXT, SYS_CLOCK_HZ,
                                  &ms, &ns, &fr) == SUCCESS) {
    crm_pll_config(CRM_PLL_SOURCE_HEXT, ns, ms, (crm_pll_fr_type)fr);
  }

  // Switch the system clock to PLL and wait for it to take effect
  crm_sysclk_switch(CRM_SCLK_PLL);
  while (crm_sysclk_switch_status_get() != CRM_SCLK_PLL) {}

  // Update the global system clock frequency (used by SysTick, delays, etc.)
  system_core_clock_update();
}

