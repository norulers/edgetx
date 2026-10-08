/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

// SPI LCD driver for H750DEV — ST7789, 2.00", 240x320, RGB565
// Interface: SPI5 (PK0=SCK, PJ10=MOSI, PH5=NSS hardware CS)
// Control:   PJ11=DC, PH6=BL (shared with backlight PWM)

#pragma once

#include <stdint.h>

// Panel physical dimensions (landscape: ST7789 rotated 90°)
#define LCD_SPI_W  320
#define LCD_SPI_H  240

void lcdSpiInit(void);
void lcdSpiWriteCmd(uint8_t cmd);
void lcdSpiWriteData8(uint8_t data);
void lcdSpiWriteData16(uint16_t data);
void lcdSpiWriteBuf(const uint16_t *buf, uint32_t len);

// Low-level burst helpers: call BeginData, then one or more PushBuf, then EndData.
// CS is held LOW for the entire sequence — use these when sending multi-part
// pixel data without re-issuing RAMWR (e.g. drawing a glyph row by row).
void lcdSpiBeginData(void);
void lcdSpiPushBuf(const uint16_t *buf, uint32_t len);
void lcdSpiEndData(void);

// Set pixel window then begin RAMWR (next WriteBuf call fills it)
void lcdSpiSetWindow(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);

// Fill a rectangular region with a single RGB565 colour
void lcdSpiFillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

// Flush an RGB565 buffer into a rectangular region
void lcdSpiFlushArea(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2,
                     const uint16_t *buf);

// Fill the whole panel with a single RGB565 colour (e.g. 0x0000 = black)
void lcdSpiClear(uint16_t color);
