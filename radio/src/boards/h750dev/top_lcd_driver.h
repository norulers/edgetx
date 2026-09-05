/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

// H750DEV secondary SPI LCD status panel driver
// Hardware: ST7789 via SPI5, landscape 320×240 RGB565

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void toplcdInit(void);
void toplcdOff(void);

// Called before/after updating all values in one frame
void toplcdRefreshStart(void);
void toplcdRefreshEnd(void);

// Status data setters (called from main loop, same API as taranis X9E)
void setTopFirstTimer(int32_t value);          // seconds, negative = counting down
void setTopSecondTimer(uint32_t value);        // total elapsed seconds (op time)
void setTopRssi(uint32_t rssi);                // signal strength 0..100
void setTopBatteryState(int state, uint8_t blinking); // 0..10 bars, blink=warning
void setTopBatteryValue(uint32_t volts);       // 100mV units (e.g. 116 = 11.6 V)

#ifdef __cplusplus
}
#endif
