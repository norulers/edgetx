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

#ifndef _BOARD_H_
#define _BOARD_H_

#include "definitions.h"
#include "edgetx_constants.h"

#include "board_common.h"
#include "hal.h"
#include "hal/serial_port.h"
#include "hal/watchdog_driver.h"

// ============================================================================
// AT32F435ZMT7 board
// NOTE: The pin mapping below is a SCAFFOLD. Replace with the real board wiring
//       (see boards/at32f435/PORTING.md). Many values are placeholders.
// ============================================================================

#define FLASHSIZE                       0x3F0000   // 4032K on-chip flash
#define BOOTLOADER_SIZE                 0x10000
#define BOOTLOADER_ADDRESS              0x08000000
#define FIRMWARE_ADDRESS                0x08010000  // TODO: external flash?
#define FIRMWARE_LEN(fsize)             (fsize)
#define FIRMWARE_MAX_LEN                FLASHSIZE
#define APP_START_ADDRESS               (uint32_t)(FIRMWARE_ADDRESS)

#define MB                              *1024*1024
#define KB                              *1024
// Monochrome target: no external RAM, keep Lua memory modest
#define LUA_MEM_EXTRA_MAX               (256 KB)
#define LUA_MEM_MAX                     (512 KB)

// Monochrome 128x64 OLED (consistent with the T20V2 target)
#define LCD_W                           128
#define LCD_H                           64
#define OLED_SCREEN                     1

extern uint16_t sessionTimer;

#define SLAVE_MODE()                    (g_model.trainerData.mode == TRAINER_MODE_SLAVE)

// Board driver
void boardInit();
void boardOff();

#if defined(ROTARY_ENCODER_NAVIGATION)
// Rotary Encoder driver
void rotaryEncoderInit();
void rotaryEncoderCheck();
#endif

// CPU Unique ID
#define LEN_CPU_UID                     (3*8+2)
void getCPUUniqueID(char * s);

// Flash Write driver (AT32 on-chip flash)
#define FLASH_PAGESIZE 256
void unlockFlash();
void lockFlash();
void flashWrite(uint32_t * address, const uint32_t * buffer);
uint32_t isFirmwareStart(const uint8_t * buffer);
uint32_t isBootloaderStart(const uint8_t * buffer);

// Pulses driver
#if !defined(SIMU)
void INTERNAL_MODULE_ON();
void INTERNAL_MODULE_OFF();
void EXTERNAL_MODULE_ON();
void EXTERNAL_MODULE_OFF();
#define BLUETOOTH_MODULE_ON()           gpio_clear(BLUETOOTH_ON_GPIO)
#define BLUETOOTH_MODULE_OFF()          gpio_set(BLUETOOTH_ON_GPIO)
#define IS_INTERNAL_MODULE_ON()         (false)
#else
#define INTERNAL_MODULE_OFF()
#define INTERNAL_MODULE_ON()
#define EXTERNAL_MODULE_ON()
#define EXTERNAL_MODULE_OFF()
#define BLUETOOTH_MODULE_ON()
#define BLUETOOTH_MODULE_OFF()
#define IS_INTERNAL_MODULE_ON()         (false)
#define IS_EXTERNAL_MODULE_ON()         (false)
#endif // defined(SIMU)

#if defined(FUNCTION_SWITCHES)
#define NUM_FUNCTIONS_SWITCHES 6
#define NUM_FUNCTIONS_GROUPS   3
#define DEFAULT_FS_CONFIG                                          \
  (SWITCH_2POS << 10) + (SWITCH_2POS << 8) + (SWITCH_2POS << 6) + \
      (SWITCH_2POS << 4) + (SWITCH_2POS << 2) + (SWITCH_2POS << 0)
#define DEFAULT_FS_GROUPS                                 \
  (1 << 10) + (1 << 8) + (1 << 6) + (1 << 4) + (1 << 2) + \
      (1 << 0)
#define DEFAULT_FS_STARTUP_CONFIG                         \
  ((FS_START_PREVIOUS << 10) + (FS_START_PREVIOUS << 8) + \
   (FS_START_PREVIOUS << 6) + (FS_START_PREVIOUS << 4) +  \
   (FS_START_PREVIOUS << 2) + (FS_START_PREVIOUS << 0))
#else
#define NUM_FUNCTIONS_SWITCHES 0
#endif

#define NUM_TRIMS                       8
#define DEFAULT_STICK_DEADZONE          2

// TODO: set battery scaling to your divider
#define BATTERY_WARN                   74
#define BATTERY_MIN                    70
#define BATTERY_MAX                    86
#define VBAT_DIV_R1                    100
#define VBAT_DIV_R2                    32
#define VBAT_MOSFET_DROP                0

#if defined(__cplusplus) && !defined(SIMU)
extern "C" {
#endif

// Power driver
#define SOFT_PWR_CTRL
#ifndef PWR_BUTTON_PRESS
#define PWR_BUTTON_PRESS
#endif
#define POWER_ON_DELAY               10 // 1s
void pwrInit();
void extModuleInit();
uint32_t pwrCheck();
uint32_t lowPowerCheck();
void pwrOn();
void pwrSoftReboot();
void pwrOff();
void pwrResetHandler();
bool pwrPressed();
bool pwrOffPressed();
bool pwrForcePressed();
uint32_t pwrPressedDuration();

const etx_serial_port_t* auxSerialGetPort(int port_nr);
#define AUX_SERIAL_POWER_ON()
#define AUX_SERIAL_POWER_OFF()

// LED driver
void ledInit();
void ledOff();
void ledRed();
void ledBlue();
void ledGreen();

// LCD driver
void lcdSetInitalFrameBuffer(void* fbAddress);
void lcdInit();
void lcdCopy(void * dest, void * src);
void lcdOff();
void lcdOn();
void lcdRefresh(bool wait=true);
void lcdRefreshWait();
void lcdSetRefVolt(unsigned char val);
void lcdSetInvert(bool invert);
void lcdSetContrast(bool useDefault = false);

// Monochrome LCD contrast (taranis F407ZG consistent)
#define LCD_CONTRAST_OFFSET                 160
#define LCD_CONTRAST_MIN                    2
#define LCD_CONTRAST_MAX                    254
#define LCD_CONTRAST_DEFAULT                25

// Backlight driver
#define BACKLIGHT_LEVEL_MAX             100
#define BACKLIGHT_FORCED_ON             BACKLIGHT_LEVEL_MAX + 1
#define BACKLIGHT_LEVEL_MIN             1
extern bool boardBacklightOn;
void backlightLowInit(void);
void backlightInit();
void backlightEnable(uint8_t dutyCycle);
void backlightFullOn();
bool isBacklightEnabled();

#define BACKLIGHT_ENABLE()                                         \
  {                                                                \
    boardBacklightOn = true;                                       \
    backlightEnable(BACKLIGHT_LEVEL_MAX - currentBacklightBright); \
  }
#define BACKLIGHT_DISABLE()                                               \
  {                                                                       \
    boardBacklightOn = false;                                             \
    backlightEnable(0);                                                   \
  }

#if defined(__cplusplus) && !defined(SIMU)
}
#endif

int audioInit();
void audioConsumeCurrentBuffer();

// Telemetry driver
#define INTMODULE_FIFO_SIZE            512
#define TELEMETRY_FIFO_SIZE            512

// Haptic driver
void hapticInit();
void hapticDone();
void hapticOff();
void hapticOn(uint32_t pwmPercent);

#define DEBUG_BAUDRATE                  115200
#define LUA_DEFAULT_BAUDRATE            115200

extern uint8_t currentTrainerMode;
void checkTrainerSettings();

// USB Charger
#if defined(USB_CHARGER)
void usbChargerInit();
bool usbChargerLed();
#endif

#endif // _BOARD_H_
