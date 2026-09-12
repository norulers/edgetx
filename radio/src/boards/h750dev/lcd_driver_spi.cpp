/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

// SPI LCD driver for H750DEV — ST7789 2.00" 240x320 RGB565
// SPI5: SCK=PK0(AF5) MOSI=PJ10(AF5) CS=PH5(GPIO) DC=PJ11(GPIO)
// BL=PH6 (TIM12_CH1, managed by backlight driver)
// Clock: PLL3Q 165 MHz ÷ MBR(4) ≈ 41 MHz SCK

#include "stm32_hal_ll.h"
#include "stm32_hal.h"
#include "hal.h"
#include "hal/gpio.h"
#include "stm32_gpio.h"
#include "delays_driver.h"
#include "lcd_driver_spi.h"
#include "board.h"

// DC: LOW = command, HIGH = data  (BSRR: upper 16 bits = reset, lower = set)
#define LCD_SPI_DC_LOW()   (GPIOJ->BSRR = (1U << 27))
#define LCD_SPI_DC_HIGH()  (GPIOJ->BSRR = (1U << 11))

// CS: LOW = selected (active LOW)
#define LCD_SPI_CS_LOW()   (GPIOH->BSRR = (1U << 21))
#define LCD_SPI_CS_HIGH()  (GPIOH->BSRR = (1U << 5))

static void spi5Init(void)
{
  // SPI45 kernel clock → PLL3Q.  Use LL not HAL (rcc_ex not included).
  LL_RCC_SetSPIClockSource(LL_RCC_SPI45_CLKSOURCE_PLL3Q);

  __HAL_RCC_SPI5_FORCE_RESET();
  __HAL_RCC_SPI5_RELEASE_RESET();
  __HAL_RCC_SPI5_CLK_ENABLE();
  __HAL_RCC_GPIOK_CLK_ENABLE();
  __HAL_RCC_GPIOJ_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();

  GPIO_InitTypeDef g = {};

  // SCK (PK0), MOSI (PJ10) — AF5 for SPI5
  g.Mode      = GPIO_MODE_AF_PP;
  g.Pull      = GPIO_NOPULL;
  g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
  g.Alternate = GPIO_AF5_SPI5;
  g.Pin = GPIO_PIN_0;  HAL_GPIO_Init(GPIOK, &g);  // SCK
  g.Pin = GPIO_PIN_10; HAL_GPIO_Init(GPIOJ, &g);  // MOSI

  // DC (PJ11), CS (PH5) — push-pull GPIO, start inactive
  g.Mode = GPIO_MODE_OUTPUT_PP; g.Pull = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_LOW; g.Alternate = 0;
  g.Pin = GPIO_PIN_11; HAL_GPIO_Init(GPIOJ, &g);  // DC
  LCD_SPI_DC_HIGH();
  g.Pin = GPIO_PIN_5;  HAL_GPIO_Init(GPIOH, &g);  // CS
  LCD_SPI_CS_HIGH();

  // *** CRITICAL ORDER: SSI=1 must be written BEFORE SSM=1 is set.
  // If SSM=1 is written while SSI=0, hardware immediately triggers MODF,
  // which auto-clears SPE and blocks all future TXP events.
  CLEAR_BIT(SPI5->CR1, SPI_CR1_SPE);
  SPI5->CR1  = SPI_CR1_SSI;   // SSI=1 first (internal NSS high)

  // CFG1: 8-bit frames, MBR=001 (÷4) → PLL3Q(165MHz)/4 ≈ 41 MHz SCK
  SPI5->CFG1 = (7UL << SPI_CFG1_DSIZE_Pos) |
               (1UL << SPI_CFG1_MBR_Pos);
  // CFG2: master, SSM=1, simplex TX (COMM=01)
  SPI5->CFG2 = SPI_CFG2_MASTER | SPI_CFG2_SSM | SPI_CFG2_COMM_0;

  // Clear MODF in case it was transiently set during any register write
  SPI5->IFCR = SPI_IFCR_MODFC;
}

#define SPI5_DWT_1MS  (480000U)  // 1 ms @ 480 MHz

static bool _spi5_wait_flag(uint32_t flag, uint32_t timeout)
{
  uint32_t t0 = DWT->CYCCNT;
  while (!(SPI5->SR & flag)) {
    if ((DWT->CYCCNT - t0) > timeout) return false;
  }
  return true;
}

static bool _spi5_tx8(uint8_t byte)
{
  SPI5->CR2 = 1;
  SET_BIT(SPI5->CR1, SPI_CR1_SPE);
  SET_BIT(SPI5->CR1, SPI_CR1_CSTART);

  if (!_spi5_wait_flag(SPI_SR_TXP, SPI5_DWT_1MS)) {
    CLEAR_BIT(SPI5->CR1, SPI_CR1_SPE);
    return false;
  }
  *(__IO uint8_t *)&SPI5->TXDR = byte;

  if (!_spi5_wait_flag(SPI_SR_EOT, SPI5_DWT_1MS)) {
    CLEAR_BIT(SPI5->CR1, SPI_CR1_SPE);
    return false;
  }
  SPI5->IFCR = SPI_IFCR_EOTC | SPI_IFCR_TXTFC;
  CLEAR_BIT(SPI5->CR1, SPI_CR1_SPE);
  return true;
}

static inline void _spi5_tx16(uint16_t val)
{
  _spi5_tx8((uint8_t)(val >> 8));
  _spi5_tx8((uint8_t)(val & 0xFF));
}

// EOT wait: a full chunk (up to 0xFFFF 16-bit frames) takes ~26 ms at 41 MHz
// SCK, so allow up to 40 ms (vs. 1 ms for the per-frame TXP wait).
#define SPI5_DWT_EOT  (SPI5_DWT_1MS * 40)

// Bulk fill: switches to 16-bit frames, then restores 8-bit
static void _spi5_fill16(uint16_t color, uint32_t count)
{
  if (!count) return;
  bool ok = true;
  CLEAR_BIT(SPI5->CR1, SPI_CR1_SPE);
  MODIFY_REG(SPI5->CFG1, SPI_CFG1_DSIZE_Msk, 0x0FUL << SPI_CFG1_DSIZE_Pos);
  while (count > 0 && ok) {
    uint32_t chunk = (count > 0xFFFFU) ? 0xFFFFU : count;
    SPI5->CR2 = chunk;
    SET_BIT(SPI5->CR1, SPI_CR1_SPE);
    SET_BIT(SPI5->CR1, SPI_CR1_CSTART);
    for (uint32_t i = 0; i < chunk; i++) {
      if (!_spi5_wait_flag(SPI_SR_TXP, SPI5_DWT_1MS)) { ok = false; break; }
      *(__IO uint16_t *)&SPI5->TXDR = color;
    }
    if (ok && !_spi5_wait_flag(SPI_SR_EOT, SPI5_DWT_EOT)) ok = false;
    if (ok) {
      SPI5->IFCR = SPI_IFCR_EOTC | SPI_IFCR_TXTFC;
    }
    CLEAR_BIT(SPI5->CR1, SPI_CR1_SPE);
    count -= chunk;
  }
  MODIFY_REG(SPI5->CFG1, SPI_CFG1_DSIZE_Msk, 0x07UL << SPI_CFG1_DSIZE_Pos);
}

// Bulk buffer send in 16-bit frames
static void _spi5_buf16(const uint16_t *buf, uint32_t count)
{
  if (!count) return;
  bool ok = true;
  CLEAR_BIT(SPI5->CR1, SPI_CR1_SPE);
  MODIFY_REG(SPI5->CFG1, SPI_CFG1_DSIZE_Msk, 0x0FUL << SPI_CFG1_DSIZE_Pos);
  while (count > 0 && ok) {
    uint32_t chunk = (count > 0xFFFFU) ? 0xFFFFU : count;
    SPI5->CR2 = chunk;
    SET_BIT(SPI5->CR1, SPI_CR1_SPE);
    SET_BIT(SPI5->CR1, SPI_CR1_CSTART);
    for (uint32_t i = 0; i < chunk; i++) {
      if (!_spi5_wait_flag(SPI_SR_TXP, SPI5_DWT_1MS)) { ok = false; break; }
      *(__IO uint16_t *)&SPI5->TXDR = buf[i];
    }
    if (ok && !_spi5_wait_flag(SPI_SR_EOT, SPI5_DWT_EOT)) ok = false;
    if (ok) {
      SPI5->IFCR = SPI_IFCR_EOTC | SPI_IFCR_TXTFC;
    }
    CLEAR_BIT(SPI5->CR1, SPI_CR1_SPE);
    buf   += chunk;
    count -= chunk;
  }
  MODIFY_REG(SPI5->CFG1, SPI_CFG1_DSIZE_Msk, 0x07UL << SPI_CFG1_DSIZE_Pos);
}

// Public write helpers — CS and DC managed here

void lcdSpiWriteCmd(uint8_t cmd)
{
  LCD_SPI_CS_LOW();
  LCD_SPI_DC_LOW();
  _spi5_tx8(cmd);
  LCD_SPI_CS_HIGH();
}

void lcdSpiWriteData8(uint8_t data)
{
  LCD_SPI_CS_LOW();
  LCD_SPI_DC_HIGH();
  _spi5_tx8(data);
  LCD_SPI_CS_HIGH();
}

void lcdSpiWriteData16(uint16_t data)
{
  LCD_SPI_CS_LOW();
  LCD_SPI_DC_HIGH();
  _spi5_tx16(data);
  LCD_SPI_CS_HIGH();
}

void lcdSpiWriteBuf(const uint16_t *buf, uint32_t len)
{
  LCD_SPI_CS_LOW();
  LCD_SPI_DC_HIGH();
  _spi5_buf16(buf, len);
  LCD_SPI_CS_HIGH();
}

// Burst helpers: CS stays LOW between BeginData and EndData
void lcdSpiBeginData(void) { LCD_SPI_CS_LOW(); LCD_SPI_DC_HIGH(); }
void lcdSpiPushBuf(const uint16_t *buf, uint32_t len) { _spi5_buf16(buf, len); }
void lcdSpiEndData(void)   { LCD_SPI_CS_HIGH(); }

// ST7789 register init sequence
void lcdSpiInit(void)
{
  spi5Init();

  delay_ms(10);  // wait for panel power-on

  lcdSpiWriteCmd(0x01);        // Software Reset
  delay_ms(150);               // mandatory ≥5 ms; 150 ms for safety

  lcdSpiWriteCmd(0x36);        // Memory Access Control
  lcdSpiWriteData8(0x70);      // landscape: MY=0, MX=1, MV=1, ML=1 → 320×240 (MX=1 fixes H-flip)

  lcdSpiWriteCmd(0x3A);        // Interface Pixel Format
  lcdSpiWriteData8(0x05);      // 16-bit RGB565

  lcdSpiWriteCmd(0xB2);        // Porch Setting
  lcdSpiWriteData8(0x0C);
  lcdSpiWriteData8(0x0C);
  lcdSpiWriteData8(0x00);
  lcdSpiWriteData8(0x33);
  lcdSpiWriteData8(0x33);

  lcdSpiWriteCmd(0xB7);        // Gate Control  VGH=13.26V VGL=-10.43V
  lcdSpiWriteData8(0x35);

  lcdSpiWriteCmd(0xBB);        // VCOM Setting  VCOM=1.35V
  lcdSpiWriteData8(0x19);

  lcdSpiWriteCmd(0xC0);        // LCM Control
  lcdSpiWriteData8(0x2C);

  lcdSpiWriteCmd(0xC2);        // VDV and VRH Command Enable
  lcdSpiWriteData8(0x01);

  lcdSpiWriteCmd(0xC3);        // VRH Set
  lcdSpiWriteData8(0x12);

  lcdSpiWriteCmd(0xC4);        // VDV Set
  lcdSpiWriteData8(0x20);

  lcdSpiWriteCmd(0xC6);        // Frame Rate — 60 Hz
  lcdSpiWriteData8(0x0F);

  lcdSpiWriteCmd(0xD0);        // Power Control 1
  lcdSpiWriteData8(0xA4);
  lcdSpiWriteData8(0xA1);

  lcdSpiWriteCmd(0xE0);        // Positive Voltage Gamma
  lcdSpiWriteData8(0xD0); lcdSpiWriteData8(0x04); lcdSpiWriteData8(0x0D);
  lcdSpiWriteData8(0x11); lcdSpiWriteData8(0x13); lcdSpiWriteData8(0x2B);
  lcdSpiWriteData8(0x3F); lcdSpiWriteData8(0x54); lcdSpiWriteData8(0x4C);
  lcdSpiWriteData8(0x18); lcdSpiWriteData8(0x0D); lcdSpiWriteData8(0x0B);
  lcdSpiWriteData8(0x1F); lcdSpiWriteData8(0x23);

  lcdSpiWriteCmd(0xE1);        // Negative Voltage Gamma
  lcdSpiWriteData8(0xD0); lcdSpiWriteData8(0x04); lcdSpiWriteData8(0x0C);
  lcdSpiWriteData8(0x11); lcdSpiWriteData8(0x13); lcdSpiWriteData8(0x2C);
  lcdSpiWriteData8(0x3F); lcdSpiWriteData8(0x44); lcdSpiWriteData8(0x51);
  lcdSpiWriteData8(0x2F); lcdSpiWriteData8(0x1F); lcdSpiWriteData8(0x1F);
  lcdSpiWriteData8(0x20); lcdSpiWriteData8(0x23);

  lcdSpiWriteCmd(0x21);        // Display Inversion On (normally-black panel)

  lcdSpiWriteCmd(0x11);        // Sleep Out
  delay_ms(120);               // mandatory 120 ms

  lcdSpiWriteCmd(0x29);        // Display On
}

// Set pixel window and issue RAMWR. H-flip is done in hardware via MADCTL MX=1.
void lcdSpiSetWindow(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
  lcdSpiWriteCmd(0x2A);        // CASET – column address
  lcdSpiWriteData16(x1);
  lcdSpiWriteData16(x2);

  lcdSpiWriteCmd(0x2B);        // RASET – row address
  lcdSpiWriteData16(y1);
  lcdSpiWriteData16(y2);

  lcdSpiWriteCmd(0x2C);        // RAMWR – begin pixel write
}

void lcdSpiFlushArea(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2,
                     const uint16_t *buf)
{
  lcdSpiSetWindow(x1, y1, x2, y2);
  lcdSpiWriteBuf(buf, (uint32_t)(x2 - x1 + 1) * (y2 - y1 + 1));
}

void lcdSpiClear(uint16_t color)
{
  lcdSpiFillRect(0, 0, LCD_SPI_W, LCD_SPI_H, color);
}

__attribute__((optimize("O3")))
void lcdSpiFillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
  if (w == 0 || h == 0) return;
  lcdSpiSetWindow(x, y, x + w - 1, y + h - 1);
  LCD_SPI_CS_LOW();
  LCD_SPI_DC_HIGH();
  _spi5_fill16(color, (uint32_t)w * h);
  LCD_SPI_CS_HIGH();
}
