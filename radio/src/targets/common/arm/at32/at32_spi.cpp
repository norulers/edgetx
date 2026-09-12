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
 *
 * AT32F435/437 SPI driver (AT32 API version of stm32_spi.cpp).
 *
 * Byte / block transfers are implemented with blocking (polled) access.
 * The "dma_*" entry points currently fall back to the polled byte path so
 * that the SD-card driver is correct without the user having to provide the
 * DMA channel / DMAMUX request mapping first (that mapping must be confirmed).
 */

#include "at32_spi.h"
#include "at32_gpio.h"
#include "delays_driver.h"

#include <stdint.h>

#define SPI_DUMMY_BYTE (AT32_SPI_DUMMY_BYTE)

void at32_spi_enable_clock(spi_type* SPIx)
{
  if (SPIx == SPI1) {
    crm_periph_clock_enable(CRM_SPI1_PERIPH_CLOCK, TRUE);
  }
#if defined(SPI2)
  else if (SPIx == SPI2) {
    crm_periph_clock_enable(CRM_SPI2_PERIPH_CLOCK, TRUE);
  }
#endif
#if defined(SPI3)
  else if (SPIx == SPI3) {
    crm_periph_clock_enable(CRM_SPI3_PERIPH_CLOCK, TRUE);
  }
#endif
#if defined(SPI4)
  else if (SPIx == SPI4) {
    crm_periph_clock_enable(CRM_SPI4_PERIPH_CLOCK, TRUE);
  }
#endif
}

// Return the APB clock frequency feeding the given SPI peripheral.
// SPI2 / SPI3 are on APB1, SPI1 / SPI4 are on APB2 (matching STM32F4 wiring).
static uint32_t _get_spi_apb_freq(spi_type* SPIx)
{
  crm_clocks_freq_type clocks;
  crm_clocks_freq_get(&clocks);

#if defined(SPI2)
  if (SPIx == SPI2) {
    return clocks.apb1_freq;
  }
#endif
#if defined(SPI3)
  if (SPIx == SPI3) {
    return clocks.apb1_freq;
  }
#endif
  return clocks.apb2_freq;
}

// Pick the AT32 master-clock divider so that the SPI clock stays <= max_freq.
static spi_mclk_freq_div_type _get_spi_prescaler(spi_type* SPIx, uint32_t max_freq)
{
  uint32_t pclk = _get_spi_apb_freq(SPIx);
  uint32_t divider = (pclk + max_freq) / max_freq;

  if (divider > 512)      return SPI_MCLK_DIV_1024;
  if (divider > 256)      return SPI_MCLK_DIV_512;
  if (divider > 128)      return SPI_MCLK_DIV_256;
  if (divider > 64)       return SPI_MCLK_DIV_128;
  if (divider > 32)       return SPI_MCLK_DIV_64;
  if (divider > 16)       return SPI_MCLK_DIV_32;
  if (divider > 8)        return SPI_MCLK_DIV_16;
  if (divider > 4)        return SPI_MCLK_DIV_8;
  if (divider > 2)        return SPI_MCLK_DIV_4;
  return SPI_MCLK_DIV_2;
}

// Configure the SPI pins.
//   SCK / MOSI -> muxed (AF) output
//   MISO       -> muxed input with internal pull-up
//   CS         -> push-pull output
static void _init_gpios(const at32_spi_t* spi)
{
  gpio_init_af(spi->SCK, (gpio_af_t)spi->GPIO_MUX, GPIO_PIN_SPEED_VERY_HIGH);
  gpio_init_af(spi->MOSI, (gpio_af_t)spi->GPIO_MUX, GPIO_PIN_SPEED_VERY_HIGH);

  // MISO: muxed input with a pull-up (helps when the card is absent)
  gpio_type* port = gpio_get_port(spi->MISO);
  uint16_t mask = (uint16_t)(1U << gpio_get_pin(spi->MISO));
  gpio_init_type init;
  gpio_default_para_init(&init);
  init.gpio_pins = mask;
  init.gpio_mode = GPIO_MODE_MUX;
  init.gpio_pull = GPIO_PULL_UP;
  init.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  init.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
  gpio_init(port, &init);
  gpio_pin_mux_config(port, (gpio_pins_source_type)gpio_get_pin(spi->MISO),
                      (gpio_mux_sel_type)spi->GPIO_MUX);

  gpio_init(spi->CS, GPIO_OUT, GPIO_PIN_SPEED_HIGH);
}

// Build an AT32 spi_init_type with the requested data width and divider.
static spi_init_type _make_spi_init(uint32_t data_width,
                                    spi_mclk_freq_div_type divider)
{
  spi_init_type spiInit;
  spi_default_para_init(&spiInit);
  spiInit.transmission_mode = SPI_TRANSMIT_FULL_DUPLEX;
  spiInit.master_slave_mode = SPI_MODE_MASTER;
  spiInit.mclk_freq_division = divider;
  spiInit.first_bit_transmission = SPI_FIRST_BIT_MSB;
  spiInit.frame_bit_num =
      (data_width == AT32_SPI_DATAWIDTH_16BIT) ? SPI_FRAME_16BIT : SPI_FRAME_8BIT;
  spiInit.clock_polarity = SPI_CLOCK_POLARITY_LOW;
  spiInit.clock_phase = SPI_CLOCK_PHASE_1EDGE;
  spiInit.cs_mode_selection = SPI_CS_SOFTWARE_MODE;
  return spiInit;
}

void at32_spi_init(const at32_spi_t* spi, uint32_t data_width)
{
  _init_gpios(spi);

  auto SPIx = spi->SPIx;
  at32_spi_enable_clock(SPIx);
  spi_i2s_reset(SPIx);

  spi_init_type spiInit = _make_spi_init(data_width, SPI_MCLK_DIV_8);
  spi_init(SPIx, &spiInit);
  spi_enable(SPIx, TRUE);
}

void at32_spi_deinit(const at32_spi_t* spi)
{
  auto SPIx = spi->SPIx;
  spi_enable(SPIx, FALSE);
  spi_i2s_reset(SPIx);
}

void at32_spi_select(const at32_spi_t* spi)
{
  gpio_clear(spi->CS);
}

void at32_spi_unselect(const at32_spi_t* spi)
{
  gpio_set(spi->CS);
}

void at32_spi_set_max_baudrate(const at32_spi_t* spi, uint32_t baudrate)
{
  auto* SPIx = spi->SPIx;
  spi_mclk_freq_div_type divider = _get_spi_prescaler(SPIx, baudrate);

  // Re-apply the divider (master mode / 8-bit frame stays as configured)
  spi_init_type spiInit = _make_spi_init(AT32_SPI_DATAWIDTH_8BIT, divider);
  spi_init(SPIx, &spiInit);
}

void at32_spi_set_data_width(const at32_spi_t* spi, uint32_t data_width)
{
  auto* SPIx = spi->SPIx;
  spi_frame_bit_num_type bit_num =
      (data_width == AT32_SPI_DATAWIDTH_16BIT) ? SPI_FRAME_16BIT : SPI_FRAME_8BIT;
  spi_frame_bit_num_set(SPIx, bit_num);
}

uint8_t at32_spi_transfer_byte(const at32_spi_t* spi, uint8_t out)
{
  auto* SPIx = spi->SPIx;

  while (spi_i2s_flag_get(SPIx, SPI_I2S_TDBE_FLAG) != SET) {}
  spi_i2s_data_transmit(SPIx, out);

  while (spi_i2s_flag_get(SPIx, SPI_I2S_RDBF_FLAG) != SET) {}
  return (uint8_t)spi_i2s_data_receive(SPIx);
}

uint32_t at32_spi_transfer_bytes(const at32_spi_t* spi, const uint8_t* out,
                                 uint8_t* in, uint32_t length)
{
  uint32_t trans_bytes = 0;
  uint8_t in_temp;

  for (trans_bytes = 0; trans_bytes < length; trans_bytes++) {
    if (out != nullptr) {
      in_temp = at32_spi_transfer_byte(spi, out[trans_bytes]);
    } else {
      in_temp = at32_spi_transfer_byte(spi, SPI_DUMMY_BYTE);
    }
    if (in != nullptr) {
      in[trans_bytes] = in_temp;
    }
  }

  return trans_bytes;
}

uint16_t at32_spi_transfer_word(const at32_spi_t* spi, uint16_t out)
{
  auto* SPIx = spi->SPIx;

  while (spi_i2s_flag_get(SPIx, SPI_I2S_TDBE_FLAG) != SET) {}
  spi_i2s_data_transmit(SPIx, out);

  while (spi_i2s_flag_get(SPIx, SPI_I2S_RDBF_FLAG) != SET) {}
  return spi_i2s_data_receive(SPIx);
}

// ---------------------------------------------------------------------------
// DMA entry points.
//
// These currently use the polled byte path for correctness. The AT32 DMA is
// routed through the DMAMUX, so a real implementation requires the board to
// provide the DMA channel + DMAMUX request IDs (SPI2_RX / SPI2_TX). Until that
// mapping is confirmed, the polled fallback guarantees a working SD driver.
// ---------------------------------------------------------------------------

uint32_t at32_spi_dma_receive_bytes(const at32_spi_t* spi, uint8_t* data,
                                    uint32_t length)
{
  return at32_spi_transfer_bytes(spi, nullptr, data, length);
}

uint32_t at32_spi_dma_transmit_bytes(const at32_spi_t* spi,
                                     const uint8_t* data, uint32_t length)
{
  return at32_spi_transfer_bytes(spi, data, nullptr, length);
}

uint32_t at32_spi_dma_transmit_words(const at32_spi_t* spi,
                                     const uint16_t* data, uint32_t length)
{
  // Word transfers are not used by the SD driver; convert to bytes so the
  // polled path still behaves correctly.
  return at32_spi_transfer_bytes(spi, (const uint8_t*)data, nullptr,
                                 length * sizeof(uint16_t));
}
