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
 */

#include "board.h"
#include "hal.h"
#include "system_clock.h"
#include "pwr.h"

#include <cstring>
#include "at32f435_437.h"

/*
 * AT32F435 board implementation.
 *
 * NOTE (WIP): this is a scaffold. The real board driver wiring (keys, backlight,
 * touch, audio, SD, module bay, ...) must be filled in using the board schematic.
 * See boards/at32f435/PORTING.md.
 */

#if !defined(SIMU)

extern "C" void bspGpioInit();

void boardInit()
{
  // System / peripheral clocks
  systemClockInit();
  bspGpioInit();

  // Monitor LCD
  lcdInit();

  // TODO: initialise backlight, keys, SD card, audio, trainer, USB...
  // TODO: initialise watchdog
  // watchdogInit();
}

void boardOff()
{
  // TODO: power off sequence for the board
}

uint32_t lowPowerCheck()
{
  return e_power_on;
}

void pwrSoftReboot()
{
  // TODO
}

bool pwrForcePressed()
{
  return false;
}

void extModuleInit()
{
  // TODO: enable external module power
}

void getCPUUniqueID(char* s)
{
  // AT32F435 unique ID (96-bit UID in a specific register area)
  // TODO: read the 3x32-bit UID from the AT32 UID registers and format it
  strcpy(s, "AT32F435");
}

#else // SIMU

void boardInit() {}
void boardOff() {}

#endif

// Power macros referenced from board.h
void INTERNAL_MODULE_ON() {}
void INTERNAL_MODULE_OFF() {}
void EXTERNAL_MODULE_ON() {}
void EXTERNAL_MODULE_OFF() {}

// LED driver stubs
void ledInit() {}
void ledOff() {}
void ledRed() {}
void ledBlue() {}
void ledGreen() {}

// Function-switch LED stubs (no FS LEDs wired on this scaffold yet)
#if defined(FUNCTION_SWITCHES)
void fsLedOff(uint8_t index) { (void)index; }
void fsLedOn(uint8_t index) { (void)index; }
bool fsLedState(uint8_t index) { (void)index; return false; }
#endif

// LCD stubs (lcdInit/lcdRefresh provided by lcd_driver_spi.cpp)
void lcdSetInitalFrameBuffer(void* fbAddress) {}
void lcdCopy(void* dest, void* src) {}
void lcdOff() {}
void lcdOn() {}

// Backlight stubs
bool boardBacklightOn = false;
void backlightLowInit(void) {}
void backlightInit() {}
void backlightEnable(uint8_t dutyCycle) {}
void backlightFullOn() {}
bool isBacklightEnabled() { return false; }

// Audio stubs
int audioInit() { return 0; }
void audioConsumeCurrentBuffer() {}
void audioSetVolume(uint8_t volume) { (void)volume; }

// Haptic stubs
void hapticInit() {}
void hapticDone() {}
void hapticOff() {}
void hapticOn(uint32_t pwmPercent) {}

// Trainer: provided by trainer.cpp (currentTrainerMode / checkTrainerSettings)
#if defined(USB_CHARGER)
void usbChargerInit() {}
bool usbChargerLed() { return false; }
#endif

// ADC / VBat bridge (WIP: implement on the AT32 ADC + VBAT rail once known)
bool isVBatBridgeEnabled() { return false; }
void enableVBatBridge() {}
void disableVBatBridge() {}

// Battery voltage (hundredths of a volt / *10 mV) - no ADC bridge wired yet
uint16_t getBatteryVoltage() { return 0; }
uint16_t getRTCBatteryVoltage() { return 0; }
