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
 * AT32F435/437 SPI driver (AT32 API version of stm32_spi.h).
 *
 * Used by the SD-card SPI storage driver (sdcard_spi.cpp) which is ported
 * from targets/common/arm/stm32/sdcard_spi.cpp.
 */

#pragma once

#include "at32f435_437.h"
#include "hal/gpio.h"
#include <stdint.h>

// Data width passed to at32_spi_init() (kept for parity with LL_SPI_DATAWIDTH_*)
#define AT32_SPI_DATAWIDTH_8BIT   0
#define AT32_SPI_DATAWIDTH_16BIT  1

// Dummy byte used for read-only SPI transfers
#define AT32_SPI_DUMMY_BYTE       0xFF

struct at32_spi_t {
  spi_type*   SPIx;
  gpio_t      SCK;
  gpio_t      MISO;
  gpio_t      MOSI;
  gpio_t      CS;

  // AT32 GPIO mux selection (analogue of the STM32 AF number), e.g. GPIO_MUX_5
  uint32_t    GPIO_MUX;

  // DMA (kept for parity with stm32_spi_t).
  // NOTE: the current implementation uses blocking (polled) byte transfers.
  // To enable real DMA the board must provide the channel / DMAMUX request
  // mapping (see report). These fields are not used by the polled path.
  dma_type*          DMA;            // DMA controller, e.g. DMA1
  dma_channel_type*  txDMA_Channel;  // e.g. DMA1_CHANNEL2
  dma_channel_type*  rxDMA_Channel;  // e.g. DMA1_CHANNEL1
  dmamux_channel_type* txDMA_Mux;    // e.g. DMA1MUX_CHANNEL2
  dmamux_channel_type* rxDMA_Mux;    // e.g. DMA1MUX_CHANNEL1
};

void at32_spi_enable_clock(spi_type* SPIx);

void at32_spi_init(const at32_spi_t* spi, uint32_t data_width);
void at32_spi_deinit(const at32_spi_t* spi);

void at32_spi_select(const at32_spi_t* spi);
void at32_spi_unselect(const at32_spi_t* spi);

void at32_spi_set_max_baudrate(const at32_spi_t* spi, uint32_t baudrate);
void at32_spi_set_data_width(const at32_spi_t* spi, uint32_t data_width);

uint8_t  at32_spi_transfer_byte(const at32_spi_t* spi, uint8_t out);
uint16_t at32_spi_transfer_word(const at32_spi_t* spi, uint16_t out);

uint32_t at32_spi_transfer_bytes(const at32_spi_t* spi, const uint8_t* out,
                                 uint8_t* in, uint32_t length);

uint32_t at32_spi_dma_receive_bytes(const at32_spi_t* spi, uint8_t* data,
                                    uint32_t length);

uint32_t at32_spi_dma_transmit_bytes(const at32_spi_t* spi,
                                     const uint8_t* data, uint32_t length);

uint32_t at32_spi_dma_transmit_words(const at32_spi_t* spi,
                                     const uint16_t* data, uint32_t length);
