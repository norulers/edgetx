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

#ifndef _HAL_H_
#define _HAL_H_

#include "at32f435_437.h"

/*
 * AT32F435ZMT7 (LQFP144) hardware abstraction layer.
 *
 * The GPIO (port + pin) assignments below follow the existing taranis
 * STM32F407ZG (LQFP144) target so that a 144-pin board layout can be reused.
 * AT32F435 is pin-compatible with STM32F407, so the port/pin numbers match.
 *
 * NOTE: still a scaffold - confirm each assignment against your board schematic.
 */

// Reset/clock control is provided by the AT32 SDK (crm_*) and device support.
// SystemInit() is defined in the AT32 SDK system_at32f435_437.c.

// Core clock frequency (Hz) - must match systemClockInit() PLL output (288 MHz)
#define CPU_FREQ                        288000000

// APB1 / timer clocks (288 MHz core, APB1 = /2 -> timer x2)
#define PERI1_FREQUENCY                 (CPU_FREQ / 2)
#define TIMER_MULT_APB1                 2

// 1 ms system tick timer (TMR14, APB1 domain)
// Note: on AT32F435, TMR14 shares the TRG/HALL interrupt line with TMR8
#define MS_TIMER                        TMR14
#define MS_TIMER_IRQn                   TMR8_TRG_HALL_TMR14_IRQn
#define MS_TIMER_IRQHandler             TMR8_TRG_HALL_TMR14_IRQHandler

// Mixer scheduler timer (TMR12, APB1 domain - shares BRK line with TMR8)
#define MIXER_SCHEDULER_TIMER            TMR12
#define MIXER_SCHEDULER_TIMER_IRQn       TMR8_BRK_TMR12_IRQn
#define MIXER_SCHEDULER_TIMER_IRQHandler TMR8_BRK_TMR12_IRQHandler
#define MIXER_SCHEDULER_TIMER_FREQ       (PERI1_FREQUENCY * TIMER_MULT_APB1)

// ---- GPIO port/pin abstraction (AT32) ---------------------------------------
// EdgeTX function-based GPIO API. GPIO_PIN encodes port base | pin number (0-15)
// and the functions are implemented in targets/common/arm/at32/at32_gpio.cpp.
#include "at32_gpio.h"

// ---- Pin map (mirrors taranis F407ZG / LQFP144) ------------------------------

// Rotary encoder (T20V2 family: PE.09 / PE.11)
#define ROTARY_ENCODER_NAVIGATION
#define ROTARY_ENCODER_GPIO            GPIOE
#define ROTARY_ENCODER_GPIO_PIN_A      9    // PE.09
#define ROTARY_ENCODER_GPIO_PIN_B      11   // PE.11
#define ROTARY_ENCODER_POSITION        (((GPIOE->idt >> 10) & 0x02) + ((GPIOE->idt >> 9) & 0x01))

// Keys (F407ZG taranis uses keys on GPIOD / GPIOE) -- consistent with F407ZG
#define KEYS_GPIO_REG_BIND             GPIOD
#define KEYS_GPIO_PIN_BIND             9    // PD.09

// External module power (taranis F407ZG: e.g. PD.08)
#define EXTMODULE_PWR_GPIO             GPIO_PIN(GPIOD, 8)   // PD.08
#define EXTERNAL_MODULE_PWR_ON()       gpio_set(EXTMODULE_PWR_GPIO)
#define EXTERNAL_MODULE_PWR_OFF()      gpio_clear(EXTMODULE_PWR_GPIO)
#define IS_EXTERNAL_MODULE_ON()        gpio_read(EXTMODULE_PWR_GPIO)

// Internal module / Bluetooth
#define BLUETOOTH_ON_GPIO              GPIO_PIN(GPIOB, 0)   // PB.00
#define BLUETOOTH_MODULE_ON()          gpio_clear(BLUETOOTH_ON_GPIO)
#define BLUETOOTH_MODULE_OFF()         gpio_set(BLUETOOTH_ON_GPIO)

// Backlight (taranis F407ZG uses TIM PWM on e.g. PD.13/PD.15 or PE.05/PE.06)
// TODO: set the AT32 timer + pin used by your backlight PWM
// #define BACKLIGHT_GPIO               GPIO_PIN(GPIOC, 13) // example

// USB charger detect (taranis F407ZG: PB.05)
#define USB_CHARGER_GPIO               GPIO_PIN(GPIOB, 5)   // PB.05

// ---- USB OTG1 (full-speed) ------------------------------------------------
// The AT32F435 OTG1 FS data lines are on PA.11 / PA.12 (pin-compatible with the
// STM32F407 OTG FS). Confirm the VBUS sense pin against your board schematic.
#define USB_GPIO_DM                    GPIO_PIN(GPIOA, 11)  // PA.11  OTGFS1_DM
#define USB_GPIO_DP                    GPIO_PIN(GPIOA, 12)  // PA.12  OTGFS1_DP
#define USB_GPIO_VBUS                  GPIO_PIN(GPIOA, 9)   // PA.09  OTGFS1_VBUS (TODO: verify)
// AT32 GPIO mux for the OTG1 FS pins (verified against the AT32F435 AF table)
#define USB_GPIO_AF                    GPIO_MUX_8

// LED indicators (taranis F407ZG: LED on PC.13/PC.14/PC.15)
#define GPIO_LED_RED                   GPIO_PIN(GPIOC, 13)
#define GPIO_LED_GREEN                 GPIO_PIN(GPIOC, 14)
#define GPIO_LED_BLUE                  GPIO_PIN(GPIOC, 15)

// LCD (128x64 monochrome OLED, consistent with T20V2, over SPI3)
#define LCD_SPI                        SPI3
#define LCD_MOSI_GPIO                  GPIO_PIN(GPIOC, 12)  // PC.12
#define LCD_CLK_GPIO                   GPIO_PIN(GPIOC, 10)  // PC.10
#define LCD_A0_GPIO                    GPIO_PIN(GPIOC, 11)  // PC.11
#define LCD_NCS_GPIO                   GPIO_PIN(GPIOA, 15)  // PA.15
#define LCD_RST_GPIO                   GPIO_PIN(GPIOA, 14)  // PA.14  (T20V2)
// AT32 GPIO mux (analogue to STM32 AF) for the SPI pins - set per datasheet
#define LCD_GPIO_MUX                   GPIO_MUX_6

// T20V2: monochrome OLED display -> no backlight driver required
// (no BACKLIGHT_* or backlight driver definitions needed)

// ---- SD card (SPI mode, from the T20V2/taranis reference) ----------------
// SD card is driven over SPI2 (shared with the taranis F407ZG pin mapping).
// STORAGE_USE_SDCARD_SPI is defined by the build (CMake add_definitions).
#define SD_GPIO_PIN_CS   GPIO_PIN(GPIOB, 12)   // PB.12
#define SD_GPIO_PIN_SCK  GPIO_PIN(GPIOB, 13)   // PB.13
#define SD_GPIO_PIN_MISO GPIO_PIN(GPIOB, 14)   // PB.14
#define SD_GPIO_PIN_MOSI GPIO_PIN(GPIOB, 15)   // PB.15
#define SD_SPI           SPI2
#define SD_SPI_DMA       DMA1
// AT32 GPIO mux for the SPI2 pins (PB.13/14/15). Matches the STM32F4 AF5
// convention used for SPI2 (the LCD on SPI3 uses GPIO_MUX_6 for the same
// reason). Confirm against the AT32F435 datasheet alternate-function table.
#define SD_GPIO_MUX      GPIO_MUX_5

// ---- CRSF / module telemetry frame interrupt ---------------------------------
// EXINT line used for the CRSF telemetry RX frame interrupt.
// NOTE: this must match the actual module telemetry pin; adjust per schematic.
#define TELEMETRY_RX_FRAME_EXTI_LINE     4

#endif // _HAL_H_
