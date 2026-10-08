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
#include "hal/key_driver.h"
#include "hal/rotary_encoder.h"

#if !defined(BOOT)
  #include "myeeprom.h"
#endif

#if ROTARY_ENCODER_GRANULARITY == 2
  #define ON_DETENT(p) ((p == 3) || (p == 0))
#elif ROTARY_ENCODER_GRANULARITY == 4
  #define ON_DETENT(p) (p == 3)
#else
#error "Unknown ROTARY_ENCODER_GRANULARITY"
#endif

volatile rotenc_t rotencValue = 0;
volatile uint32_t rotencDt = 0;

// Last encoder pins state
static uint8_t lastPins = 0;
// Record encoder position change between detents
int8_t reChgPos = 0;
// Used on start to ignore movement until encoder position on detent
bool skipUntilDetent = false;

rotenc_t rotaryEncoderGetValue()
{
  return rotencValue;
}

void rotaryEncoderCheck()
{
  // Value increment for each state transition of the RE pins
#if defined(ROTARY_ENCODER_INVERTED)
  static int8_t reInc[4][4] = {
    // Prev = 0
    {  0, -1,  1, -2 },
    // Prev = 1
    {  1,  0,  0, -1 },
    // Prev = 2
    { -1,  0,  0,  1 },
    // Prev = 3
    {  2,  1, -1,  0 },
  };
#else
  static int8_t reInc[4][4] = {
    // Prev = 0
    {  0,  1, -1,  2 },
    // Prev = 1
    { -1,  0,  0,  1 },
    // Prev = 2
    {  1,  0,  0, -1 },
    // Prev = 3
    { -2, -1,  1,  0 },
  };
#endif

  uint8_t pins = ROTARY_ENCODER_POSITION;

  // No change - do nothing
  if (pins == lastPins) {
    return;
  }

  // Handle case where radio started with encoder not on detent position
  if (skipUntilDetent) {
    if (ON_DETENT(pins)) {
      lastPins = pins;
      skipUntilDetent = false;
    }
    return;
  }

  // Get increment value for pin state transition
  int inc = reInc[lastPins][pins];

#if !defined(BOOT)
  // ROTARY_ENCODER_MODE_INVERT_BOTH (from edgetx.h RotaryEncoderMode)
  if (g_eeGeneral.rotEncMode == 1)
    inc = -inc;
#endif

  // Update position change between detents
  reChgPos += inc;

  // Update reported value on full detent change
  if (reChgPos >= ROTARY_ENCODER_GRANULARITY) {
    // If ENTER pressed - ignore scrolling
    if ((readKeys() & (1 << KEY_ENTER)) == 0) {
      rotencValue += 1;
    }
    reChgPos -= ROTARY_ENCODER_GRANULARITY;
  } else if (reChgPos <= -ROTARY_ENCODER_GRANULARITY) {
    // If ENTER pressed - ignore scrolling
    if ((readKeys() & (1 << KEY_ENTER)) == 0) {
      rotencValue -= 1;
    }
    reChgPos += ROTARY_ENCODER_GRANULARITY;
  }

  lastPins = pins;

#if !defined(BOOT) && defined(COLORLCD)
  static uint32_t last_tick = 0;
  static rotenc_t last_value = 0;

  rotenc_t value = rotencValue;
  rotenc_t diff = (value - last_value);

  if (diff != 0) {
    uint32_t now = timersGetMsTick();
    uint32_t dt = now - last_tick;
    // pre-compute accumulated dt (dx/dt is done later in LVGL driver)
    rotencDt += dt;
    last_tick = now;
    last_value = value;
  }
#endif
}

void rotaryEncoderInit()
{
#if defined(ROTARY_ENCODER_NAVIGATION)
  // Configure the encoder pins as inputs with pull-up (polled, no EXTI yet)
  gpio_init(GPIO_PIN(ROTARY_ENCODER_GPIO, ROTARY_ENCODER_GPIO_PIN_A), GPIO_IN_PU, GPIO_PIN_SPEED_LOW);
  gpio_init(GPIO_PIN(ROTARY_ENCODER_GPIO, ROTARY_ENCODER_GPIO_PIN_B), GPIO_IN_PU, GPIO_PIN_SPEED_LOW);
#endif
}
