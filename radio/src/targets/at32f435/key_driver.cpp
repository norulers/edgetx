/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 */

#include "hal/key_driver.h"

#include "board.h"
#include "hal.h"

#include "at32f435_437.h"

/*
 * AT32F435 key driver.
 *
 * The current AT32F435 board scaffold only wires a BIND key on PD.09
 * (see boards/at32f435/hal.h). Navigation on the monochrome 128x64 UI is
 * primarily handled by the rotary encoder.
 *
 * TODO: map the remaining physical keys/buttons to EdgeTX key events as the
 *       hardware becomes known (see boards/at32f435/PORTING.md).
 */

#if !defined(SIMU)

void keysInit()
{
  gpio_init(GPIO_PIN(KEYS_GPIO_REG_BIND, KEYS_GPIO_PIN_BIND), GPIO_IN_PU, GPIO_PIN_SPEED_LOW);
}

void pollKeys()
{
}

uint32_t readKeys()
{
  uint32_t keys = 0;
  // BIND key (PD.09, active low)
  if (!gpio_read(GPIO_PIN(KEYS_GPIO_REG_BIND, KEYS_GPIO_PIN_BIND)))
    keys |= (1U << KEY_BIND);
  return keys;
}

uint32_t readTrims()
{
  // No hardware trim buttons on the scaffold yet
  return 0;
}

#endif // !defined(SIMU)
