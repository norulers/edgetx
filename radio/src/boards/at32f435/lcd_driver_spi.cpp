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
#include "lcd.h"

#include "at32f435_437.h"

#if !defined(BOOT)
  #include "myeeprom.h"
#endif

/*
 * AT32F435 212x64 monochrome LCD driver.
 *
 * Consistent with the F407ZG taranis target: ST7565-class controller driven
 * over SPI (SPI3, PC.10/PC.11/PC.12, NCS PA.15, RST PD.15).
 *
 * NOTE (WIP): byte-wise (blocking) SPI write is implemented. A DMA-based,
 * interrupt driven refresh (like taranis) can be added later for speed.
 */

#define LCD_RESET_WAIT_DELAY_MS   300

#define LCD_NCS_HIGH()            gpio_set(LCD_NCS_GPIO)
#define LCD_NCS_LOW()             gpio_clear(LCD_NCS_GPIO)
#define LCD_A0_HIGH()             gpio_set(LCD_A0_GPIO)
#define LCD_A0_LOW()              gpio_clear(LCD_A0_GPIO)
#define LCD_RST_HIGH()            gpio_set(LCD_RST_GPIO)
#define LCD_RST_LOW()             gpio_clear(LCD_RST_GPIO)

static void lcdHardwareInit()
{
  // SPI3 + GPIO clocks
  crm_periph_clock_enable(CRM_SPI3_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_GPIOC_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_GPIOD_PERIPH_CLOCK, TRUE);

  // SPI3 master, 8-bit, CPOL/CPHA, prescaler
  spi_init_type spi;
  spi_default_para_init(&spi);
  spi.transmission_mode = SPI_TRANSMIT_FULL_DUPLEX;
  spi.master_slave_mode = SPI_MODE_MASTER;
  spi.mclk_freq_division = SPI_MCLK_DIV_8;
  spi.first_bit_transmission = SPI_FIRST_BIT_MSB;
  spi.frame_bit_num = SPI_FRAME_8BIT;
  spi.clock_polarity = SPI_CLOCK_POLARITY_HIGH;
  spi.clock_phase = SPI_CLOCK_PHASE_2EDGE;
  spi.cs_mode_selection = SPI_CS_SOFTWARE_MODE;
  spi_init(LCD_SPI, &spi);
  spi_enable(LCD_SPI, TRUE);

  // MOSI / CLK as muxed (AF), NCS/A0/RST as output
  gpio_init_af(LCD_MOSI_GPIO, (gpio_af_t)LCD_GPIO_MUX, GPIO_PIN_SPEED_HIGH);
  gpio_init_af(LCD_CLK_GPIO, (gpio_af_t)LCD_GPIO_MUX, GPIO_PIN_SPEED_HIGH);

  gpio_init(LCD_NCS_GPIO, GPIO_OUT, GPIO_PIN_SPEED_LOW);
  gpio_init(LCD_A0_GPIO, GPIO_OUT, GPIO_PIN_SPEED_LOW);
  gpio_init(LCD_RST_GPIO, GPIO_OUT, GPIO_PIN_SPEED_LOW);

  LCD_NCS_HIGH();
  LCD_A0_HIGH();
  LCD_RST_HIGH();
}

static void lcdSPIWrite(uint8_t byte)
{
  while (spi_i2s_flag_get(LCD_SPI, SPI_I2S_TDBE_FLAG) != SET) {}
  spi_i2s_data_transmit(LCD_SPI, byte);
  while (spi_i2s_flag_get(LCD_SPI, SPI_I2S_TDBE_FLAG) != SET) {}
  (void)spi_i2s_data_receive(LCD_SPI); // Drain RX
}

static void lcdWriteCommand(uint8_t cmd)
{
  LCD_A0_LOW();
  LCD_NCS_LOW();
  lcdSPIWrite(cmd);
  LCD_NCS_HIGH();
}

// taranis 128x64 ST7565-class init sequence
static void lcdStart()
{
  lcdWriteCommand(0xe2); // Soft reset
  delay_ms(20);
  lcdWriteCommand(0xa1); // Set seg
  lcdWriteCommand(0xc0); // Set com
  lcdWriteCommand(0xf8); // Set booster
  lcdWriteCommand(0x00); // 5x
  lcdWriteCommand(0xa3); // Set bias=1/6
  lcdWriteCommand(0x22); // Set internal rb/ra=5.0
  lcdWriteCommand(0x2f); // All built-in power circuits on
  lcdWriteCommand(0x81); // Set contrast
#if defined(BOOT)
  lcdWriteCommand(LCD_CONTRAST_OFFSET + LCD_CONTRAST_DEFAULT);
#else
  lcdWriteCommand(LCD_CONTRAST_OFFSET + g_eeGeneral.contrast);
#endif
  lcdWriteCommand(0xa6); // Set display mode
}

static bool lcdInitFinished = false;

void lcdInit()
{
  lcdHardwareInit();

  // Reset sequence
  LCD_RST_LOW();
  delay_ms(LCD_RESET_WAIT_DELAY_MS);
  LCD_RST_HIGH();
  delay_ms(LCD_RESET_WAIT_DELAY_MS);

  lcdStart();
  lcdInitFinished = true;
}

void lcdRefresh(bool wait)
{
  if (!lcdInitFinished) {
    lcdInit();
  }

  // Push the rendering buffer page-by-page (8 pages of LCD_W bytes, 128x64)
  uint8_t* p = displayBuf;
  for (uint8_t y = 0; y < 8; y++, p += LCD_W) {
    lcdWriteCommand(0x10);          // Column address high (start col 0)
    lcdWriteCommand(0xB0 | y);      // Page address y
    lcdWriteCommand(0x04);          // Column address low
    LCD_NCS_LOW();
    LCD_A0_HIGH();
    for (uint16_t i = 0; i < LCD_W; i++) {
      lcdSPIWrite(p[i]);
    }
    LCD_NCS_HIGH();
    LCD_A0_HIGH();
  }
}

void lcdRefreshWait()
{
  // Byte-wise writes are synchronous; nothing to wait for (unless DMA is added)
}

void lcdSetRefVolt(uint8_t val)
{
  lcdWriteCommand(0x81);                          // Set contrast
  lcdWriteCommand(val + LCD_CONTRAST_OFFSET);     // 0-255
}

void lcdSetInvert(bool invert)
{
  lcdWriteCommand(invert ? 0xA7 : 0xA6);
}
