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
 
#include "stm32_adc.h"
#include "stm32_gpio.h"
#include "stm32_i2c_driver.h"
#include "stm32_hal.h"
#include "stm32_hal_ll.h"
#include "stm32_ws2812.h"
#include "stm32_spi.h"

#include "flash_driver.h"
#include "extflash_driver.h"

#include "board.h"
#include "boards/generic_stm32/module_ports.h"
#include "boards/generic_stm32/rgb_leds.h"
#include "bsp_io.h"

#include "hal/adc_driver.h"
#include "hal/flash_driver.h"
#include "hal/trainer_driver.h"
#include "hal/rotary_encoder.h"
#include "hal/switch_driver.h"
#include "hal/abnormal_reboot.h"
#include "hal/watchdog_driver.h"
#include "hal/usb_driver.h"
#include "hal/gpio.h"
#include "hal/rgbleds.h"

#include "globals.h"
#include "sdcard.h"
#include "debug.h"
#include "keys.h"

#include "flysky_gimbal_driver.h"
#include "timers_driver.h"
#include "delays_driver.h"

#include "touch_driver.h"

#include <string.h>

// common ADC driver
extern const etx_hal_adc_driver_t _adc_driver;

// RGB LED timer
extern const stm32_pulse_timer_t _led_timer;

static void led_strip_off()
{
  for (uint8_t i = 0; i < LED_STRIP_LENGTH; i++) {
    ws2812_set_color(i, 0, 0, 0);
  }
  ws2812_update(&_led_timer);
}

void INTERNAL_MODULE_ON()
{
  gpio_set(INTMODULE_PWR_GPIO);
}

void INTERNAL_MODULE_OFF()
{
  gpio_clear(INTMODULE_PWR_GPIO);
}

void EXTERNAL_MODULE_ON()
{
  gpio_set(EXTMODULE_PWR_GPIO);
}

void EXTERNAL_MODULE_OFF()
{
  gpio_clear(EXTMODULE_PWR_GPIO);
}

// ---------- DEBUG: USART1 serial output (P1 connector: PA9=TX, 115200 8N1) ----------
// Connect a USB-serial adapter: GND→P1-3, PA9(TX)→P1-6 (RX of adapter), 3.3V logic
// PCLK2 = HCLK/2 = 120MHz → BRR = 120000000/115200 = 1042
static void bl_uart_init(void)
{
  LL_AHB4_GRP1_EnableClock(LL_AHB4_GRP1_PERIPH_GPIOA);
  LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_USART1);

  // PA9 → AF7 (USART1_TX), push-pull, no pull
  LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_9, LL_GPIO_MODE_ALTERNATE);
  LL_GPIO_SetAFPin_8_15(GPIOA, LL_GPIO_PIN_9, LL_GPIO_AF_7);
  LL_GPIO_SetPinOutputType(GPIOA, LL_GPIO_PIN_9, LL_GPIO_OUTPUT_PUSHPULL);
  LL_GPIO_SetPinPull(GPIOA, LL_GPIO_PIN_9, LL_GPIO_PULL_NO);
  LL_GPIO_SetPinSpeed(GPIOA, LL_GPIO_PIN_9, LL_GPIO_SPEED_FREQ_MEDIUM);

  // Force USART1/6 kernel clock to HSI (always 64 MHz, immune to PLL / peripheral
  // clock changes made by ledInit/boardInitModulePorts/etc.).
  // HSI must be running; it's normally always on after reset, but ensure it:
  if (!LL_RCC_HSI_IsReady()) {
    LL_RCC_HSI_Enable();
    while (!LL_RCC_HSI_IsReady()) {}
  }
  // Must disable USART before changing clock source (RM0433 §52.4.3).
  USART1->CR1 &= ~USART_CR1_UE;
  LL_RCC_SetUSARTClockSource(LL_RCC_USART16_CLKSOURCE_HSI);

  USART1->CR1 = 0;
  USART1->CR2 = 0;
  USART1->CR3 = 0;
  USART1->BRR = 556;  // 115200 @ 64 MHz HSI  (64000000 / 115200 = 555.6 → 556)
  USART1->CR1 = USART_CR1_TE | USART_CR1_UE;
}

void bl_uart_puts(const char* s)
{
  while (*s) {
    while (!(USART1->ISR & USART_ISR_TXE_TXFNF)) {}
    USART1->TDR = (uint8_t)*s++;
  }
  // Wait for transmission complete so last byte is fully on the wire
  // before the caller returns (guards against crash cutting off last bytes)
  while (!(USART1->ISR & USART_ISR_TC)) {}
}

#define BL_LOG(msg)  bl_uart_puts("[BL] " msg "\r\n")

// Override weak fault handlers to report via USART1
// HardFault is already non-weak in cortex_m_isr.c (modified to report there)
static void _bl_print_hex(uint32_t v)
{
  const char hex[] = "0123456789ABCDEF";
  char buf[11] = {'0','x',0,0,0,0,0,0,0,0,'\0'};
  for (int i = 7; i >= 0; i--) { buf[2 + i] = hex[v & 0xF]; v >>= 4; }
  bl_uart_puts(buf);
}

extern "C" void bl_print_hex(uint32_t v) { _bl_print_hex(v); }

extern "C" void BusFault_Handler(void)
{
  bl_uart_puts("\r\n!!! BusFault !!!");
  bl_uart_puts(" CFSR="); _bl_print_hex(SCB->CFSR);
  bl_uart_puts(" BFAR="); _bl_print_hex(SCB->BFAR);
  bl_uart_puts("\r\n");
  while (1) {}
}
extern "C" void UsageFault_Handler(void)
{
  bl_uart_puts("\r\n!!! UsageFault !!!");
  bl_uart_puts(" CFSR="); _bl_print_hex(SCB->CFSR);
  bl_uart_puts("\r\n");
  while (1) {}
}
extern "C" void MemManage_Handler(void)
{
  bl_uart_puts("\r\n!!! MemManageFault !!!");
  bl_uart_puts(" CFSR="); _bl_print_hex(SCB->CFSR);
  bl_uart_puts(" MMFAR="); _bl_print_hex(SCB->MMFAR);
  bl_uart_puts("\r\n");
  while (1) {}
}

void boardBLEarlyInit()
{
  // TAS2505 requires reset pin to be low on power on
  gpio_init(AUDIO_RESET_PIN, GPIO_OUT, GPIO_PIN_SPEED_LOW);
  gpio_clear(AUDIO_RESET_PIN);

  timersInit();
  delaysInit();
  bl_uart_init();
  // Enable BusFault, UsageFault, MemManage fault handlers independently
  // (without this they all escalate to HardFault)
  SCB->SHCSR |= SCB_SHCSR_BUSFAULTENA_Msk | SCB_SHCSR_USGFAULTENA_Msk | SCB_SHCSR_MEMFAULTENA_Msk;
  BL_LOG("boardBLEarlyInit start");

  // NOTE: bsp_io_init() (I2C expanders) is NOT called here.
  // It would try to enumerate PCA95xx over I2C and can hang if expanders
  // are unresponsive or I2C bus is stuck. Switches are not needed in bootloader.
  usbChargerInit();
  BL_LOG("boardBLEarlyInit done");
}

// Dev board: short PH12 (LSD) + PH11 (LSU) to GND BEFORE inserting USB to enter bootloader.
// Sample multiple times to avoid false triggers from pin settling on power-up.
bool boardBLStartCondition()
{
  // Enable GPIOH clock explicitly (may not be done yet at this early stage)
  LL_AHB4_GRP1_EnableClock(LL_AHB4_GRP1_PERIPH_GPIOH);

  // Single-pin trigger: PH12 (P2-38 header) = TRIMS_LSD
  // Short P2-38 to GND before powering on to enter bootloader.
  // PH11 (LSU) has no accessible header pin so is NOT used here.
  gpio_init(GPIO_PIN(GPIOH, 12), GPIO_IN_PU, GPIO_PIN_SPEED_LOW);

  // Wait for pull-up to settle
  for (volatile int i = 0; i < 50000; i++) {}

  // Require PH12 LOW on 3 consecutive samples
  for (int i = 0; i < 3; i++) {
    if (LL_GPIO_IsInputPinSet(TRIMS_GPIO_REG_LSD, TRIMS_GPIO_PIN_LSD)) {
      return false;
    }
    for (volatile int j = 0; j < 10000; j++) {}
  }
  return true;
}

void boardBLPreJump()
{
  // TAS2505 requires reset pin to be high only after power on
  // https://www.ti.com/lit/ug/slau472c/slau472c.pd fig 4.2
  gpio_set(AUDIO_RESET_PIN);
  ExtFLASH_Init();
  SDRAM_Init();

  // Stop 1ms timer
  MS_TIMER->CR1 &= ~TIM_CR1_CEN;
}

void boardBLInit()
{
  BL_LOG("boardBLInit entered");

  ExtFLASH_Init();
  BL_LOG("ExtFLASH_Init done");

  SDRAM_Init();
  BL_LOG("SDRAM_Init done");

  // --- SDRAM write/read test ---
  volatile uint32_t* sdram = (volatile uint32_t*)0xC0000000;
  const uint32_t PATTERN = 0xA5A5A5A5;
  sdram[0] = PATTERN;
  sdram[1] = ~PATTERN;
  sdram[1024] = PATTERN;
  __DSB();
  if (sdram[0] == PATTERN && sdram[1] == (~PATTERN & 0xFFFFFFFF) && sdram[1024] == PATTERN) {
    BL_LOG("SDRAM test PASS");
  } else {
    BL_LOG("SDRAM test FAIL -- halted");
    while (1) {}
  }

  // register external FLASH for DFU
  usbRegisterDFUMedia((void*)extflash_dfu_media);

  // register internal & external FLASH for UF2
  flashRegisterDriver(FLASH_BANK1_BASE, BOOTLOADER_SIZE, &stm32_flash_driver);
  flashRegisterDriver(QSPI_BASE, QSPI_FLASH_SIZE, &extflash_driver);
  BL_LOG("boardBLInit done");
}

void boardInit()
{
  // enable interrupts
  __enable_irq();

  bl_uart_puts("[FW] boardInit start\r\n");

#if defined(KCX_BTAUDIO)
  btAudioInit();
#endif

  ledInit();
  boardInitModulePorts();

  pwrInit();
  delaysInit();
  timersInit();
  // Re-init UART: force USART1 kernel clock to HSI (64 MHz, immune to any
  // clock-source changes made by ledInit/boardInitModulePorts above).
  bl_uart_init();
  bl_uart_puts("[FW] timersInit done\r\n");

  usbChargerInit();
  gpio_set(LED_BLUE_GPIO);

  ExtFLASH_InitRuntime();
  bl_uart_puts("[FW] ExtFLASH_InitRuntime done\r\n");

  // register internal & external FLASH for UF2
  flashRegisterDriver(FLASH_BANK1_BASE, BOOTLOADER_SIZE, &stm32_flash_driver);
  flashRegisterDriver(QSPI_BASE, QSPI_FLASH_SIZE, &extflash_driver);

  // init_trainer();

#if defined(FLYSKY_GIMBAL)
  auto inittime = flysky_gimbal_init();
  if (inittime)
    TRACE("Serial gimbal detected in %d ms", inittime);
  else
    TRACE("No serial gimbal detected");
#endif

  usbInit();
  bl_uart_puts("[FW] usbInit done\r\n");

  rgbLedInit();
  led_strip_off();

  bl_uart_puts("[FW] keysInit start\r\n");
  keysInit();
  bl_uart_puts("[FW] keysInit done\r\n");

  bl_uart_puts("[FW] switchInit start\r\n");
  switchInit();
  bl_uart_puts("[FW] switchInit done\r\n");

  rotaryEncoderInit();
  bl_uart_puts("[FW] rotaryEncoderInit done\r\n");

  bl_uart_puts("[FW] touchPanelInit start\r\n");
  touchPanelInit();
  bl_uart_puts("[FW] touchPanelInit done\r\n");

  bl_uart_puts("[FW] audioInit start\r\n");
  audioInit();
  bl_uart_puts("[FW] audioInit done\r\n");

  adcInit(&_adc_driver);
  hapticInit();
  rtcInit();
  bl_uart_puts("[FW] boardInit done\r\n");
}

extern void rtcDisableBackupReg();

void boardOff()
{
  lcdOff();

#if defined(PWR_SWITCH_GPIO)
  while (pwrPressed()) {
    WDG_RESET();
  }
#endif

  SysTick->CTRL = 0; // turn off systick

  // Shutdown the Haptic
  hapticDone();

  rtcDisableBackupReg();

  pwrOff();

  // We reach here only in forced power situations, such as hw-debugging with external power  
  // Enter STM32 stop mode / deep-sleep
  // Code snippet from ST Nucleo PWR_EnterStopMode example
#define PDMode             0x00000000U
#if defined(PWR_CR1_MRUDS) && defined(PWR_CR1_LPUDS) && defined(PWR_CR1_FPDS)
  MODIFY_REG(PWR->CR1, (PWR_CR1_PDDS | PWR_CR1_LPDS | PWR_CR1_FPDS | PWR_CR1_LPUDS | PWR_CR1_MRUDS), PDMode);
#elif defined(PWR_CR_MRLVDS) && defined(PWR_CR_LPLVDS) && defined(PWR_CR_FPDS)
  MODIFY_REG(PWR->CR1, (PWR_CR1_PDDS | PWR_CR1_LPDS | PWR_CR1_FPDS | PWR_CR1_LPLVDS | PWR_CR1_MRLVDS), PDMode);
#else
//  MODIFY_REG(PWR->CR1, (PWR_CR1_P_PDDS| PWR_CR1_LPDS), PDMode);
#endif /* PWR_CR_MRUDS && PWR_CR_LPUDS && PWR_CR_FPDS */

/* Set SLEEPDEEP bit of Cortex System Control Register */
  SET_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));
  
  // To avoid HardFault at return address, end in an endless loop
  while (1) {

  }
}
