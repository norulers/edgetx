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

#include "hal/gpio.h"
#include "stm32_gpio.h"
#include "stm32_hal_ll.h"
#include "stm32_hal.h"
#include "stm32_gpio_driver.h"
#include "stm32_exti_driver.h"

#include "hal.h"
#include "timers_driver.h"
#include "tp_gt911.h"
#include "delays_driver.h"

#include "os/sleep.h"
#include "edgetx_types.h"
#include "debug.h"

#include <stdlib.h>
#include <string.h>

#define TP_GT911_ID "911"


bool touchGT911Flag = false;
volatile static bool touchEventOccured = false;
struct TouchData touchData;
uint16_t touchGT911fwver = 0;
uint32_t touchGT911hiccups = 0;

static tmr10ms_t downTime = 0;
static tmr10ms_t tapTime = 0;
static short tapCount = 0;

static TouchState internalTouchState = {};

static void _gt911_exti_isr(void)
{
  touchEventOccured = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Software I2C for GT911
// SCL = PG3 (TOUCH_I2C_SCL_GPIO)  SDA = PG7 (TOUCH_I2C_SDA_GPIO)
// Both pins are open-drain with internal pull-up (GPIO_OD_PU).
// STM32 IDR always reflects the actual line voltage even in OD output mode,
// so no pin-mode switching is needed when reading SDA (e.g. for ACK).
// ─────────────────────────────────────────────────────────────────────────────

#define GT911_WRITE_ADDR   ((GT911_I2C_ADDR) << 1)         // 0x28
#define GT911_READ_ADDR    (((GT911_I2C_ADDR) << 1) | 1u) // 0x29

// ~5 µs half-bit at 480 MHz (200 counts × ~3 cycles/count ≈ 1250 ns → ~400 kHz)
static inline void _i2c_half_delay(void)
{
  for (volatile int _d = 0; _d < 200; _d++) { __NOP(); }
}

static inline void _scl_high() { gpio_set(TOUCH_I2C_SCL_GPIO); }
static inline void _scl_low()  { gpio_clear(TOUCH_I2C_SCL_GPIO); }
static inline void _sda_high() { gpio_set(TOUCH_I2C_SDA_GPIO); }
static inline void _sda_low()  { gpio_clear(TOUCH_I2C_SDA_GPIO); }
static inline uint32_t _sda_read() { return gpio_read(TOUCH_I2C_SDA_GPIO); }

void I2C_Init_Radio(void)
{
  TRACE("GT911 SW-I2C Init (PG3=SCL, PG7=SDA)");
  // Open-drain with internal pull-up: drive 0 to pull low, drive 1 to release
  gpio_init(TOUCH_I2C_SCL_GPIO, GPIO_OD_PU, GPIO_PIN_SPEED_LOW);
  gpio_init(TOUCH_I2C_SDA_GPIO, GPIO_OD_PU, GPIO_PIN_SPEED_LOW);
  _scl_high();
  _sda_high();
  _i2c_half_delay();
}

static void _gt911_start(void)
{
  _sda_high(); _scl_high(); _i2c_half_delay();
  _sda_low();  _i2c_half_delay();
  _scl_low();  _i2c_half_delay();
}

static void _gt911_stop(void)
{
  _sda_low();  _i2c_half_delay();
  _scl_high(); _i2c_half_delay();
  _sda_high(); _i2c_half_delay();
}

// Returns true if slave ACK'd (SDA held low)
static bool _gt911_write_byte(uint8_t byte)
{
  for (int bit = 7; bit >= 0; bit--) {
    if (byte & (1u << bit)) _sda_high(); else _sda_low();
    _i2c_half_delay();
    _scl_high(); _i2c_half_delay();
    _scl_low();  _i2c_half_delay();
  }
  // Release SDA, then clock in the ACK bit driven by slave
  _sda_high(); _i2c_half_delay();
  _scl_high(); _i2c_half_delay();
  bool ack = (_sda_read() == 0);
  _scl_low();  _i2c_half_delay();
  return ack;
}

// send_ack=true → ACK (more bytes), send_ack=false → NACK (last byte)
static uint8_t _gt911_read_byte(bool send_ack)
{
  uint8_t byte = 0;
  _sda_high();  // release SDA so slave can drive it
  for (int bit = 7; bit >= 0; bit--) {
    _i2c_half_delay();
    _scl_high(); _i2c_half_delay();
    if (_sda_read()) byte |= (1u << bit);
    _scl_low();
  }
  // Send ACK or NACK
  if (send_ack) _sda_low(); else _sda_high();
  _i2c_half_delay();
  _scl_high(); _i2c_half_delay();
  _scl_low();  _i2c_half_delay();
  _sda_high();
  return byte;
}

bool I2C_GT911_WriteRegister(uint16_t reg, uint8_t *buf, uint8_t len)
{
  _gt911_start();
  if (!_gt911_write_byte(GT911_WRITE_ADDR)) {
    _gt911_stop();
    TRACE("SW-I2C GT911: NACK on address (write)");
    return false;
  }
  _gt911_write_byte((uint8_t)(reg >> 8));
  _gt911_write_byte((uint8_t)(reg & 0xFF));
  for (int i = 0; i < len; i++) {
    _gt911_write_byte(buf[i]);
  }
  _gt911_stop();
  return true;
}

bool I2C_GT911_ReadRegister(uint16_t reg, uint8_t *buf, uint8_t len)
{
  // Phase 1: write register address
  _gt911_start();
  if (!_gt911_write_byte(GT911_WRITE_ADDR)) {
    _gt911_stop();
    TRACE("SW-I2C GT911: NACK on address (write phase of read)");
    return false;
  }
  _gt911_write_byte((uint8_t)(reg >> 8));
  _gt911_write_byte((uint8_t)(reg & 0xFF));
  // Phase 2: repeated-start then read
  _gt911_start();
  if (!_gt911_write_byte(GT911_READ_ADDR)) {
    _gt911_stop();
    TRACE("SW-I2C GT911: NACK on address (read)");
    return false;
  }
  for (int i = 0; i < len; i++) {
    buf[i] = _gt911_read_byte(i < len - 1);  // NACK on last byte
  }
  _gt911_stop();
  return true;
}

void touchPanelDeInit(void)
{
  gpio_int_disable(TOUCH_INT_GPIO);
  touchGT911Flag = false;
}

uint8_t tp_gt911_cfgVer = GT911_CFG_NUMBER;

bool touchPanelInit(void)
{
  uint8_t tmp[4] = {0};

  if (touchGT911Flag) {
    gpio_init_int(TOUCH_INT_GPIO, GPIO_IN_PU, GPIO_RISING, _gt911_exti_isr);
    return true;
  } else {
    TRACE("Touchpanel init start ...");

    gpio_init(TOUCH_RST_GPIO, GPIO_OUT, GPIO_PIN_SPEED_LOW);
    gpio_init(TOUCH_INT_GPIO, GPIO_OUT, GPIO_PIN_SPEED_LOW);
    I2C_Init_Radio();

    gpio_clear(TOUCH_RST_GPIO);
    gpio_set(TOUCH_INT_GPIO);
    delay_us(200);

    gpio_set(TOUCH_RST_GPIO);
    delay_ms(6);

    gpio_clear(TOUCH_INT_GPIO);
    delay_ms(55);

    gpio_init(TOUCH_INT_GPIO, GPIO_IN_PU, GPIO_PIN_SPEED_LOW);

    delay_ms(50);

    TRACE("Reading Touch registry");
    if (!I2C_GT911_ReadRegister(GT911_PRODUCT_ID_REG, tmp, 4)) {
      TRACE("GT911 ERROR: Product ID read failed");
    }

    if (strcmp((char *)tmp, TP_GT911_ID) == 0) {
      TRACE("GT911 chip detected");
      tmp[0] = 0X02;
      if (!I2C_GT911_WriteRegister(GT911_COMMAND_REG, tmp, 1)) {
        TRACE("GT911 ERROR: write to control register failed");
      }
      if (!I2C_GT911_ReadRegister(GT911_CONFIG_REG, tmp, 1)) {
        TRACE("GT911 ERROR: configuration register read failed");
      }

      TRACE("Chip config Ver:%x", tmp[0]);
      if ((tp_gt911_cfgVer == 0) || (tmp[0] < tp_gt911_cfgVer)) { // Config ver
        TRACE("Sending new config %d", GT911_CFG_NUMBER);
        if (!I2C_GT911_ReadRegister(GT911_CONFIG_REG, tmp, 1)) {
          TRACE("GT911 ERROR: configuration register read failed");
        }
        tp_gt911_cfgVer = tmp[0];
      }

      if (!I2C_GT911_ReadRegister(GT911_FIRMWARE_VERSION_REG, tmp, 2)) {
        TRACE("GT911 ERROR: reading firmware version failed");
      } else {
        touchGT911fwver = (tmp[1] << 8) + tmp[0];
        TRACE("GT911 FW version: %u", touchGT911fwver);
      }

      delay_ms(10);
      tmp[0] = 0X00;
      if (!I2C_GT911_WriteRegister(GT911_COMMAND_REG, tmp, 1)) { // end reset
        TRACE("GT911 ERROR: write to command register failed");
      }
      touchGT911Flag = true;

      gpio_init_int(TOUCH_INT_GPIO, GPIO_IN_PU, GPIO_RISING, _gt911_exti_isr);

      return true;
    }
    TRACE("GT911 chip NOT FOUND");
    return false;
  }
}

bool I2C_ReInit(void)
{
  TRACE("SW-I2C GT911 ReInit");
  touchPanelDeInit();
  // Re-initialise GPIO lines (no hardware peripheral to deinit)
  I2C_Init_Radio();

  // If DeInit fails, try to re-init anyway
  if (!touchPanelInit()) {
    TRACE("I2C B1 ReInit - touchPanelInit failed");
    return false;
  }
  return true;
}

#if defined(SIMU) || defined(SEMIHOSTING) || defined(DEBUG)
static const char *event2str(uint8_t ev)
{
  switch (ev) {
    case TE_NONE:
      return "NONE";
    case TE_UP:
      return "UP";
    case TE_DOWN:
      return "DOWN";
    case TE_SLIDE_END:
      return "SLIDE_END";
    case TE_SLIDE:
      return "SLIDE";
    default:
      return "UNKNOWN";
  }
}
#endif

struct TouchState touchPanelRead()
{
  uint8_t state = 0;

  if (!touchEventOccured) return internalTouchState;

  touchEventOccured = false;

  uint32_t startReadStatus = timersGetMsTick();
  do {
    if (!I2C_GT911_ReadRegister(GT911_READ_XY_REG, &state, 1)) {
      // ledRed();
      touchGT911hiccups++;
      TRACE("GT911 I2C read XY error");
      if (!I2C_ReInit()) TRACE("I2C B1 ReInit failed");
      return internalTouchState;
    }

    if (state & 0x80u) {
      // ready
      break;
    }
    sleep_ms(1);
  } while (timersGetMsTick() - startReadStatus < GT911_TIMEOUT);

  internalTouchState.deltaX = 0;
  internalTouchState.deltaY = 0;
  TRACE("touch state = 0x%x", state);
  if (state & 0x80u) {
    uint8_t pointsCount = (state & 0x0Fu);
    uint32_t now = timersGetMsTick();
    internalTouchState.tapCount = 0;

    if (pointsCount > 0 && pointsCount <= GT911_MAX_TP) {
      if (!I2C_GT911_ReadRegister(GT911_READ_XY_REG + 1, touchData.data,
                                  pointsCount * sizeof(TouchPoint))) {
        // ledRed();
        touchGT911hiccups++;
        TRACE("GT911 I2C data read error");
        if (!I2C_ReInit()) TRACE("I2C B1 ReInit failed");
        return internalTouchState;
      }
        
      if (internalTouchState.event == TE_NONE ||
          internalTouchState.event == TE_UP ||
          internalTouchState.event == TE_SLIDE_END) {
        internalTouchState.event = TE_DOWN;
        internalTouchState.startX = internalTouchState.x =
            touchData.points[0].x;
        internalTouchState.startY = internalTouchState.y =
            touchData.points[0].y;
        downTime = now;
      } else {
        internalTouchState.deltaX =
            touchData.points[0].x - internalTouchState.x;
        internalTouchState.deltaY =
            touchData.points[0].y - internalTouchState.y;
        if (internalTouchState.event == TE_SLIDE ||
            abs(internalTouchState.deltaX) >= SLIDE_RANGE ||
            abs(internalTouchState.deltaY) >= SLIDE_RANGE) {
          internalTouchState.event = TE_SLIDE;
          internalTouchState.x = touchData.points[0].x;
          internalTouchState.y = touchData.points[0].y;
        }
      }
    } else {
      if (internalTouchState.event == TE_SLIDE) {
        internalTouchState.event = TE_SLIDE_END;
      } else if (internalTouchState.event == TE_DOWN) {
        internalTouchState.event = TE_UP;
        if (now - downTime <= GT911_TAP_TIME) {
          if (now - tapTime > GT911_TAP_TIME)
            tapCount = 1;
          else
            tapCount++;
          internalTouchState.tapCount = tapCount;
          tapTime = now;
        }
      } else {
        internalTouchState.event = TE_NONE;
      }
    }
  }

  uint8_t zero = 0;
  if (!I2C_GT911_WriteRegister(GT911_READ_XY_REG, &zero, 1)) {
    TRACE("GT911 ERROR: clearing XY register failed");
  }

  TRACE("touch event = %s", event2str(internalTouchState.event));
  return internalTouchState;
}

bool touchPanelEventOccured()
{
  return touchEventOccured;
}

TouchState getInternalTouchState()
{
  return internalTouchState;
}

