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

static volatile uint8_t _frame_addr_reloaded = 0;

static void startLcdRefresh(lv_disp_drv_t *disp_drv, uint16_t *buffer,
                            const rect_t &copy_area)
{
  (void)disp_drv;
  (void)copy_area;

  SCB_CleanDCache();

  LTDC_Layer1->CFBAR = (uint32_t)buffer;
  _frame_addr_reloaded = 0;
  LTDC->SRCR = LTDC_SRCR_VBR;

  __HAL_LTDC_ENABLE_IT(&hltdc, LTDC_IT_LI);

  while(_frame_addr_reloaded == 0);
}

static void LCD_AF_GPIOConfig(void)
{
  const gpio_t _lcd_af_gpios[] = {
      GPIO_PIN(GPIOI, 12), GPIO_PIN(GPIOI, 13), GPIO_PIN(GPIOI, 14),
      GPIO_PIN(GPIOI, 15),  // LTDC_R0  (was KEYS_TELE=PI15, moved to PI3)

      GPIO_PIN(GPIOJ, 0),   // LTDC_R1
      GPIO_PIN(GPIOJ, 1),  GPIO_PIN(GPIOJ, 2),  GPIO_PIN(GPIOJ, 3),
      GPIO_PIN(GPIOJ, 4),  GPIO_PIN(GPIOJ, 5),  GPIO_PIN(GPIOJ, 6),
      GPIO_PIN(GPIOJ, 7),   // LTDC_G0
      GPIO_PIN(GPIOJ, 8),   // LTDC_G1  (was ROTARY_ENC_B=PJ8, moved to PI4)
      GPIO_PIN(GPIOJ, 9),  GPIO_PIN(GPIOJ, 10), GPIO_PIN(GPIOJ, 11),
      GPIO_PIN(GPIOJ, 12),  // LTDC_B0  (was LCD_RESET=PJ12, moved to PA10)
      GPIO_PIN(GPIOJ, 13),  // LTDC_B1  (was TOUCH_RST=PJ13, moved to PI10)
      GPIO_PIN(GPIOJ, 14), GPIO_PIN(GPIOJ, 15),

      GPIO_PIN(GPIOK, 0),  GPIO_PIN(GPIOK, 1),  GPIO_PIN(GPIOK, 2),
      GPIO_PIN(GPIOK, 3),  GPIO_PIN(GPIOK, 4),  GPIO_PIN(GPIOK, 5),
      GPIO_PIN(GPIOK, 6),  GPIO_PIN(GPIOK, 7),
  };

  for (unsigned i = 0; i < sizeof(_lcd_af_gpios) / sizeof(_lcd_af_gpios[0]); i++) {
    gpio_init_af(_lcd_af_gpios[i], GPIO_AF14, GPIO_SPEED_FREQ_VERY_HIGH);
  }
}

void LCD_Init_LTDC() {
  hltdc.Instance = LTDC;

  /* LTDC clock is sourced from PLL3R (configured in system_clock.c):
   * HSE=25MHz / M=5 * N=66 / R=10 = 33MHz pixel clock */

  /* LTDC Configuration *********************************************************/
  /* Polarity configuration */
  /* Initialize the horizontal synchronization polarity as active low */
  hltdc.Init.HSPolarity = LTDC_HSPOLARITY_AL;
  /* Initialize the vertical synchronization polarity as active low */
  hltdc.Init.VSPolarity = LTDC_VSPOLARITY_AL;
  /* Initialize the data enable polarity as active low */
  hltdc.Init.DEPolarity = LTDC_DEPOLARITY_AL;
  /* Initialize the pixel clock polarity as input pixel clock */
  hltdc.Init.PCPolarity = LTDC_PCPOLARITY_IPC;

  /* Configure R,G,B component values for LCD background color */
  hltdc.Init.Backcolor.Red = 0;
  hltdc.Init.Backcolor.Green = 0;
  hltdc.Init.Backcolor.Blue = 0;

  /* Configure horizontal synchronization width */
  hltdc.Init.HorizontalSync = HSW-1;
  /* Configure vertical synchronization height */
  hltdc.Init.VerticalSync = VSH-1;
  /* Configure accumulated horizontal back porch */
  hltdc.Init.AccumulatedHBP = HSW+HBP-1;
  /* Configure accumulated vertical back porch */
  hltdc.Init.AccumulatedVBP = VSH+VBP-1;
  /* Configure accumulated active width */
  hltdc.Init.AccumulatedActiveW = lcd_phys_w + HBP+HSW-1;
  /* Configure accumulated active height */
  hltdc.Init.AccumulatedActiveH = lcd_phys_h + VBP+VSH-1;
  /* Configure total width */
  hltdc.Init.TotalWidth = lcd_phys_w + HBP + HFP + HSW -1;
  /* Configure total height */
  hltdc.Init.TotalHeigh = lcd_phys_h + VBP + VFP+VSH-1;

  HAL_LTDC_Init(&hltdc);

  // Configure IRQ (line)
  NVIC_SetPriority(LTDC_IRQn, LTDC_IRQ_PRIO);
  NVIC_EnableIRQ(LTDC_IRQn);

  // Trigger after last active line (first VFP line).
  // lcd_phys_h + VBP + VSH = AccumulatedActiveH + 1, which is the first
  // vertical front-porch line. Firing here ensures all active scan lines
  // have been read from the frame buffer before the CPU is unblocked.
  HAL_LTDC_ProgramLineEvent(&hltdc, lcd_phys_h + VBP + VSH);
  __HAL_LTDC_ENABLE_IT(&hltdc, LTDC_IT_LI);
}

void LCD_LayerInit() {
  auto& layer = hltdc.LayerCfg[0];

  /* Windowing configuration */
  layer.WindowX0 = 0;
  layer.WindowX1 = lcd_phys_w;
  layer.WindowY0 = 0;
  layer.WindowY1 = lcd_phys_h;

  /* Pixel Format configuration*/
  layer.PixelFormat = LTDC_PIXEL_FORMAT_RGB565;

  /* Alpha constant (255 totally opaque) */
  layer.Alpha = 255;

  /* Default Color configuration (configure A,R,G,B component values) */
  layer.Backcolor.Blue = 0;
  layer.Backcolor.Green = 0;
  layer.Backcolor.Red = 0;
  layer.Alpha0 = 0;

  /* Configure blending factors */
  layer.BlendingFactor1 = LTDC_BLENDING_FACTOR1_CA;
  layer.BlendingFactor2 = LTDC_BLENDING_FACTOR2_CA;

  layer.ImageWidth = lcd_phys_w;
  layer.ImageHeight = lcd_phys_h;

  /* Start Address configuration : the LCD Frame buffer is defined on SDRAM w/ Offset */
  layer.FBStartAdress = (intptr_t)initialFrameBuffer;

  /* Initialize LTDC layer 1 */
  HAL_LTDC_ConfigLayer(&hltdc, &hltdc.LayerCfg[0], 0);

  /* dithering activation */
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
  // Pure RGB LTDC screen - no SPI init or RST pin needed, LCD self-resets on power-on
  // (Reference board FK750M5-XBH6 also has no RST pin defined)
  boardLcdType = "RGB LTDC 800x480";

  /* Configure the LTDC AF GPIO pins */
  LCD_AF_GPIOConfig();

  __HAL_RCC_LTDC_CLK_ENABLE();

  LCD_Init_LTDC();
  LCD_LayerInit();

  // Enable LCD display
  __HAL_LTDC_ENABLE(&hltdc);

  lcdSetFlushCb(startLcdRefresh);
}

extern "C" void LTDC_IRQHandler(void)
{
  __HAL_LTDC_CLEAR_FLAG(&hltdc, LTDC_FLAG_LI);
  __HAL_LTDC_DISABLE_IT(&hltdc, LTDC_IT_LI);
  _frame_addr_reloaded = 1;
}
