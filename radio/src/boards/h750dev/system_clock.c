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

#include "stm32h7xx_ll_pwr.h"
#include "stm32h7xx_ll_rcc.h"
#include "stm32h7xx_ll_system.h"
#include "stm32h7xx_ll_bus.h"

#define BOOTSTRAP __attribute__((section(".bootstrap")))

BOOTSTRAP void SystemClock_Config(void)
{
  /* Power Configuration: start with SCALE1 */
  LL_PWR_ConfigSupply(LL_PWR_LDO_SUPPLY);
  LL_PWR_SetRegulVoltageScaling(LL_PWR_REGU_VOLTAGE_SCALE1);
  while (LL_PWR_IsActiveFlag_VOS() == 0){
  }

  /* Activate VOS0 overdrive BEFORE enabling HSE/PLL (correct sequence per RM):
   * CSI must be running, SYSCFG clock enabled, then ODEN set */
  LL_RCC_CSI_Enable();
  while (LL_RCC_CSI_IsReady() != 1) {
  }
  LL_APB4_GRP1_EnableClock(LL_APB4_GRP1_PERIPH_SYSCFG);
  SYSCFG->PWRCR |= SYSCFG_PWRCR_ODEN;  // Enable overdrive -> VOS0
  while (LL_PWR_IsActiveFlag_VOS() == 0) {
  }

  /* Enable HSE oscillator */
  LL_RCC_HSE_Enable();
  while (LL_RCC_HSE_IsReady() != 1) {
  }

  /* Set FLASH latency: HCLK=240MHz, VOS0 -> 3 WS sufficient, use 4 for margin */
  LL_FLASH_SetLatency(LL_FLASH_LATENCY_4);

  /* Main PLL configuration: HSE=25MHz, M=5 -> 5MHz, *N=192 -> 960MHz, /P=2 -> 480MHz */
  LL_RCC_PLL_SetSource(LL_RCC_PLLSOURCE_HSE);
  LL_RCC_PLL1P_Enable();
  LL_RCC_PLL1Q_Enable();
  LL_RCC_PLL1R_Enable();
  LL_RCC_PLL1FRACN_Disable();
  LL_RCC_PLL1_SetVCOInputRange(LL_RCC_PLLINPUTRANGE_4_8);  // VCO_in = 5MHz
  LL_RCC_PLL1_SetVCOOutputRange(LL_RCC_PLLVCORANGE_WIDE); // VCO = 960MHz
  LL_RCC_PLL1_SetM(5);
  LL_RCC_PLL1_SetN(192);
  LL_RCC_PLL1_SetP(2);   // PLL1P = 480MHz (SYSCLK)
  LL_RCC_PLL1_SetQ(20);  // PLL1Q = 48MHz  (USB)
  LL_RCC_PLL1_SetR(2);
  LL_RCC_PLL1_Enable();
  while (LL_RCC_PLL1_IsReady() != 1) {
  }

  /* Set Sys & AHB & APB prescalers: HCLK=480/2=240MHz, APBx=240/2=120MHz */
  LL_RCC_SetSysPrescaler(LL_RCC_SYSCLK_DIV_1);
  LL_RCC_SetAHBPrescaler(LL_RCC_AHB_DIV_2);
  LL_RCC_SetAPB1Prescaler(LL_RCC_APB1_DIV_2);
  LL_RCC_SetAPB2Prescaler(LL_RCC_APB2_DIV_2);
  LL_RCC_SetAPB4Prescaler(LL_RCC_APB4_DIV_2);

  /* Set PLL1 as System Clock Source */
  LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_PLL1);
  while (LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_PLL1) {
  }

  /* Config & activate PLL2 (I2S audio clock) */
  /* HSE=25MHz / M=5 = 5MHz VCO_in, *N=54 = 270MHz VCO, /P=24 = 11.25MHz (~11.2896MHz for 44.1kHz) */
  LL_RCC_PLL2P_Enable();
  LL_RCC_PLL2FRACN_Disable();
  LL_RCC_PLL2_SetVCOInputRange(LL_RCC_PLLINPUTRANGE_4_8);
  LL_RCC_PLL2_SetVCOOutputRange(LL_RCC_PLLVCORANGE_WIDE);
  LL_RCC_PLL2_SetM(5);
  LL_RCC_PLL2_SetN(54);
  LL_RCC_PLL2_SetP(24); // 270 / 24 = 11.25 MHz (I2S MCLK ~11.2896 MHz)
  LL_RCC_PLL2_Enable();
  while (LL_RCC_PLL2_IsReady() != 1) {
  }

  /* Config & activate PLL3 */
  LL_RCC_PLL3P_Enable();
  LL_RCC_PLL3Q_Enable();
  LL_RCC_PLL3R_Enable();
  LL_RCC_PLL3FRACN_Disable();
  // PLL3 for LTDC: HSE=25MHz / M=5 = 5MHz, *N=66 = 330MHz VCO, /R=10 = 33MHz pixel clock
  LL_RCC_PLL3_SetVCOInputRange(LL_RCC_PLLINPUTRANGE_4_8);  // VCO_in = 5MHz
  LL_RCC_PLL3_SetVCOOutputRange(LL_RCC_PLLVCORANGE_WIDE);  // VCO = 330MHz
  LL_RCC_PLL3_SetM(5);
  LL_RCC_PLL3_SetN(66);  // VCO = 5 * 66 = 330MHz
  LL_RCC_PLL3_SetP(2);
  LL_RCC_PLL3_SetQ(2);
  LL_RCC_PLL3_SetR(10);  // PLL3R = 330 / 10 = 33MHz LTDC pixel clock
  LL_RCC_PLL3_Enable();
  while (LL_RCC_PLL3_IsReady() != 1) {
  }

  /* Enable SRAM1, SRAM2 & SRAM3 */
  LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_D2SRAM1 |
                           LL_AHB2_GRP1_PERIPH_D2SRAM2 |
                           LL_AHB2_GRP1_PERIPH_D2SRAM3);

  /* Set periph clock sources */
  LL_RCC_SetSPIClockSource(LL_RCC_SPI123_CLKSOURCE_PLL1Q);
  LL_RCC_SetUSBClockSource(LL_RCC_USB_CLKSOURCE_PLL1Q);
  LL_RCC_SetADCClockSource(LL_RCC_ADC_CLKSOURCE_CLKP);
}
