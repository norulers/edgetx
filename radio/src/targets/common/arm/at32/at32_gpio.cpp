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
 * AT32F435/437 implementation of the EdgeTX function-based GPIO API
 * (hal/gpio.h). The GPIO port is encoded in gpio_t as:
 *   gpio_t = port_base_address | pin_number(0-15)
 */

#include "hal/gpio.h"
#include "at32_gpio.h"

#include "at32f435_437.h"

static inline gpio_type* _port(gpio_t pin)
{
  return (gpio_type*)(pin & ~(0x0f));
}

static inline uint16_t _pin_mask(gpio_t pin)
{
  return (uint16_t)(1U << (pin & 0x0f));
}

static inline uint16_t _pin_num(gpio_t pin)
{
  return (pin & 0x0f);
}

static inline void _enable_clock(gpio_type* port)
{
  // Clock enable based on the port base address (GPIOA..GPIOH)
  uint32_t reg = (uint32_t)port;
  crm_periph_clock_type clk;

  if (reg == GPIOA_BASE)      clk = CRM_GPIOA_PERIPH_CLOCK;
  else if (reg == GPIOB_BASE) clk = CRM_GPIOB_PERIPH_CLOCK;
  else if (reg == GPIOC_BASE) clk = CRM_GPIOC_PERIPH_CLOCK;
  else if (reg == GPIOD_BASE) clk = CRM_GPIOD_PERIPH_CLOCK;
  else if (reg == GPIOE_BASE) clk = CRM_GPIOE_PERIPH_CLOCK;
  else if (reg == GPIOF_BASE) clk = CRM_GPIOF_PERIPH_CLOCK;
  else if (reg == GPIOG_BASE) clk = CRM_GPIOG_PERIPH_CLOCK;
  else                        clk = CRM_GPIOH_PERIPH_CLOCK;

  crm_periph_clock_enable(clk, TRUE);
}

void gpio_init(gpio_t pin, gpio_mode_t mode, gpio_speed_t speed)
{
  gpio_type* port = _port(pin);
  uint16_t mask = _pin_mask(pin);

  _enable_clock(port);

  gpio_init_type init;
  gpio_default_para_init(&init);
  init.gpio_pins = mask;

  // mode bits 0-1 -> input/output
  uint8_t io = mode & 0x3;
  if (io == 0) {
    init.gpio_mode = GPIO_MODE_INPUT;
  } else {
    init.gpio_mode = GPIO_MODE_OUTPUT;
  }

  // pull bits 2-3
  uint8_t pr = (mode >> 2) & 0x3;
  init.gpio_pull = (pr == 1) ? GPIO_PULL_UP : (pr == 2) ? GPIO_PULL_DOWN : GPIO_PULL_NONE;

  // output type bit 4
  uint8_t ot = (mode >> 4) & 0x1;
  init.gpio_out_type = ot ? GPIO_OUTPUT_OPEN_DRAIN : GPIO_OUTPUT_PUSH_PULL;

  init.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;

  gpio_init(port, &init);

  // speed -> output drive strength
  ((void)speed);
}

void gpio_init_af(gpio_t pin, gpio_af_t af, gpio_speed_t speed)
{
  gpio_type* port = _port(pin);
  uint16_t mask = _pin_mask(pin);
  uint16_t num = _pin_num(pin);

  _enable_clock(port);

  gpio_init_type init;
  gpio_default_para_init(&init);
  init.gpio_pins = mask;
  init.gpio_mode = GPIO_MODE_MUX;
  init.gpio_pull = GPIO_PULL_NONE;
  init.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  init.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
  gpio_init(port, &init);

  gpio_pin_mux_config(port, (gpio_pins_source_type)num, (gpio_mux_sel_type)af);
  ((void)speed);
}

void gpio_set_af(gpio_t pin, gpio_af_t af)
{
  gpio_type* port = _port(pin);
  uint16_t num = _pin_num(pin);
  gpio_pin_mux_config(port, (gpio_pins_source_type)num, (gpio_mux_sel_type)af);
}

void gpio_init_int(gpio_t pin, gpio_mode_t mode, gpio_flank_t flank, gpio_cb_t cb)
{
  (void)pin;
  (void)mode;
  (void)flank;
  (void)cb;
  // TODO: EXINT support not yet implemented for AT32
}

void gpio_int_disable(gpio_t pin)
{
  (void)pin;
  // TODO: EXINT support not yet implemented for AT32
}

void gpio_init_analog(gpio_t pin)
{
  gpio_type* port = _port(pin);
  uint16_t mask = _pin_mask(pin);
  _enable_clock(port);

  gpio_init_type init;
  gpio_default_para_init(&init);
  init.gpio_pins = mask;
  init.gpio_mode = GPIO_MODE_ANALOG;
  init.gpio_pull = GPIO_PULL_NONE;
  gpio_init(port, &init);
}

gpio_mode_t gpio_get_mode(gpio_t pin)
{
  gpio_type* port = _port(pin);
  uint16_t num = _pin_num(pin);

  uint32_t cfgr = port->cfgr;
  cfgr >>= (2 * num);
  uint8_t mode = cfgr & 0x3;

  return (mode == GPIO_MODE_INPUT) ? GPIO_IN :
         (mode == GPIO_MODE_OUTPUT) ? GPIO_OUT :
         (mode == GPIO_MODE_MUX)    ? GPIO_OUT :
                                      GPIO_IN;
}

int gpio_read(gpio_t pin)
{
  gpio_type* port = _port(pin);
  uint16_t mask = _pin_mask(pin);
  return gpio_input_data_bit_read(port, mask);
}

void gpio_set(gpio_t pin)
{
  gpio_type* port = _port(pin);
  uint16_t mask = _pin_mask(pin);
  gpio_bits_set(port, mask);
}

void gpio_clear(gpio_t pin)
{
  gpio_type* port = _port(pin);
  uint16_t mask = _pin_mask(pin);
  gpio_bits_reset(port, mask);
}

void gpio_toggle(gpio_t pin)
{
  if (gpio_read(pin)) {
    gpio_clear(pin);
  } else {
    gpio_set(pin);
  }
}

void gpio_write(gpio_t pin, int value)
{
  if (value) {
    gpio_set(pin);
  } else {
    gpio_clear(pin);
  }
}

gpio_type* gpio_get_port(gpio_t pin)
{
  return _port(pin);
}

uint32_t gpio_get_pin(gpio_t pin)
{
  return _pin_num(pin);
}
