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

#include "stm32_hal_ll.h"
#include "stm32_hal.h"
#include "edgetx_types.h"
#include "dma2d.h"
#include "hal.h"
#include "debug.h"
#include "lcd.h"
#include "lcd_driver_800.h"
#include "board.h"

#include "hal/gpio.h"
#include "stm32_gpio.h"

uint8_t TouchControllerType = 0;  // 0: other; 1: CST836U
static const uint16_t lcd_phys_w = LCD_PHYS_W;
static const uint16_t lcd_phys_h = LCD_PHYS_H;

static LTDC_HandleTypeDef hltdc;
static void* initialFrameBuffer = nullptr;

static void startLcdRefresh(lv_disp_drv_t *disp_drv, uint16_t *buffer,
                            const rect_t &copy_area)
{
  (void)disp_drv;
  (void)copy_area;

  SCB_CleanDCache();

  LTDC_Layer1->CFBAR = (uint32_t)buffer;

  // Reload shadow registers on the next vertical blank and return immediately.
  // The LTDC line interrupt calls lcdFlushed() once the reload is done and the
  // scan has passed the active area (fully asynchronous, same as pl18/st16).
  LTDC->SRCR = LTDC_SRCR_VBR;
  __HAL_LTDC_ENABLE_IT(&hltdc, LTDC_IT_LI);
}

static void LCD_AF_GPIOConfig(void)
{
  const gpio_t _lcd_af_gpios[] = {
      GPIO_PIN(GPIOI, 12), GPIO_PIN(GPIOI, 13), GPIO_PIN(GPIOI, 14),
      GPIO_PIN(GPIOI, 15),  // LTDC_R0

      GPIO_PIN(GPIOJ, 0),   // LTDC_R1
      GPIO_PIN(GPIOJ, 1),  GPIO_PIN(GPIOJ, 2),  GPIO_PIN(GPIOJ, 3),
      GPIO_PIN(GPIOJ, 4),  GPIO_PIN(GPIOJ, 5),  GPIO_PIN(GPIOJ, 6),
      GPIO_PIN(GPIOJ, 7),   // LTDC_G0
      GPIO_PIN(GPIOJ, 8),   // LTDC_G1
      GPIO_PIN(GPIOJ, 9),   // LTDC_G2
      // PJ10/PJ11 are SPI5 MOSI/DC (secondary LCD) — not LTDC pins
      GPIO_PIN(GPIOJ, 12),  // LTDC_B0
      GPIO_PIN(GPIOJ, 13),  // LTDC_B1
      GPIO_PIN(GPIOJ, 14), GPIO_PIN(GPIOJ, 15),

      GPIO_PIN(GPIOH, 15),  // LTDC_G4  (PH15 = AF14, alternate routing)
      // PK0 is SPI5_SCK (secondary LCD) — not an LTDC pin
      GPIO_PIN(GPIOK, 1),  GPIO_PIN(GPIOK, 2),  // LTDC_G6, LTDC_G7
      GPIO_PIN(GPIOK, 3),  GPIO_PIN(GPIOK, 4),  GPIO_PIN(GPIOK, 5),
      GPIO_PIN(GPIOK, 6),  GPIO_PIN(GPIOK, 7),
  };

  for (unsigned i = 0; i < sizeof(_lcd_af_gpios) / sizeof(_lcd_af_gpios[0]); i++) {
    gpio_init_af(_lcd_af_gpios[i], GPIO_AF14, GPIO_SPEED_FREQ_VERY_HIGH);
  }

  // PG10 (LTDC_G3) and PH4 (LTDC_G5) use alternate routing requiring AF9
  gpio_init_af(GPIO_PIN(GPIOG, 10), GPIO_AF9, GPIO_SPEED_FREQ_VERY_HIGH);
  gpio_init_af(GPIO_PIN(GPIOH, 4),  GPIO_AF9, GPIO_SPEED_FREQ_VERY_HIGH);
}

void LCD_Init_LTDC() {
  hltdc.Instance = LTDC;

  // PLL3R clock: HSE 25MHz / M=5 * N=66 / R=10 = 33 MHz pixel clock

  hltdc.Init.HSPolarity = LTDC_HSPOLARITY_AL;
  hltdc.Init.VSPolarity = LTDC_VSPOLARITY_AL;
  hltdc.Init.DEPolarity = LTDC_DEPOLARITY_AL;
  hltdc.Init.PCPolarity = LTDC_PCPOLARITY_IPC;

  hltdc.Init.Backcolor.Red   = 0;
  hltdc.Init.Backcolor.Green = 0;
  hltdc.Init.Backcolor.Blue  = 0;

  hltdc.Init.HorizontalSync    = HSW-1;
  hltdc.Init.VerticalSync      = VSH-1;
  hltdc.Init.AccumulatedHBP    = HSW+HBP-1;
  hltdc.Init.AccumulatedVBP    = VSH+VBP-1;
  hltdc.Init.AccumulatedActiveW = lcd_phys_w + HBP+HSW-1;
  hltdc.Init.AccumulatedActiveH = lcd_phys_h + VBP+VSH-1;
  hltdc.Init.TotalWidth        = lcd_phys_w + HBP + HFP + HSW -1;
  hltdc.Init.TotalHeigh        = lcd_phys_h + VBP + VFP+VSH-1;

  HAL_LTDC_Init(&hltdc);

  NVIC_SetPriority(LTDC_IRQn, LTDC_IRQ_PRIO);
  NVIC_EnableIRQ(LTDC_IRQn);

  // Line event fires at start of VFP; enabled per-frame in startLcdRefresh()
  HAL_LTDC_ProgramLineEvent(&hltdc, lcd_phys_h + VBP + VSH);
}

void LCD_LayerInit() {
  auto& layer = hltdc.LayerCfg[0];

  layer.WindowX0 = 0;
  layer.WindowX1 = lcd_phys_w;
  layer.WindowY0 = 0;
  layer.WindowY1 = lcd_phys_h;

  layer.PixelFormat = LTDC_PIXEL_FORMAT_RGB565;
  layer.Alpha  = 255;
  layer.Alpha0 = 0;

  layer.Backcolor.Blue  = 0;
  layer.Backcolor.Green = 0;
  layer.Backcolor.Red   = 0;

  layer.BlendingFactor1 = LTDC_BLENDING_FACTOR1_CA;
  layer.BlendingFactor2 = LTDC_BLENDING_FACTOR2_CA;

  layer.ImageWidth  = lcd_phys_w;
  layer.ImageHeight = lcd_phys_h;
  layer.FBStartAdress = (intptr_t)initialFrameBuffer;

  HAL_LTDC_ConfigLayer(&hltdc, &hltdc.LayerCfg[0], 0);
  HAL_LTDC_EnableDither(&hltdc);
}

extern "C"
void lcdSetInitalFrameBuffer(void* fbAddress)
{
  initialFrameBuffer = fbAddress;
}

const char* boardLcdType = "";

extern "C"
void lcdInit(void)
{
  boardLcdType = "RGB LTDC 800x480";

  LCD_AF_GPIOConfig();
  __HAL_RCC_LTDC_CLK_ENABLE();
  LCD_Init_LTDC();
  LCD_LayerInit();
  __HAL_LTDC_ENABLE(&hltdc);
  lcdSetFlushCb(startLcdRefresh);
}

extern "C" void LTDC_IRQHandler(void)
{
  __HAL_LTDC_CLEAR_FLAG(&hltdc, LTDC_FLAG_LI);
  __HAL_LTDC_DISABLE_IT(&hltdc, LTDC_IT_LI);

  lcdFlushed();
}
