// Audio codec TAS2505 and I2S are NOT populated on this dev board.
// All I2C/I2S/DMA audio hardware is stubbed out.
// audioConsumeCurrentBuffer() drains the queue silently so the audio
// subsystem runs without crashing; no sound is produced.

#include "drivers/tas2505.h"

#include "stm32_dma.h"
#include "stm32_i2s.h"
#include "stm32_i2c_driver.h"

#include "stm32_hal_ll.h"
#include "timers_driver.h"

#include "audio.h"
#include "debug.h"
#include "hal/gpio.h"
#include "hal.h"

#define DMA_BUFFER_HALF_LEN AUDIO_BUFFER_SIZE
#define DMA_BUFFER_LEN (DMA_BUFFER_HALF_LEN * 2)

int audioInit()
{
  gpio_init(AUDIO_HP_DETECT_PIN, GPIO_IN_PU, GPIO_PIN_SPEED_LOW);
  return 0;
}

static volatile uint32_t _dma_buffer_offset = 0;

void audioConsumeCurrentBuffer()
{
  // I2S / TAS2505 not initialised on this dev board.
  // Drain the filled buffer queue so audioQueue.wakeup() doesn't block,
  // but do NOT touch any DMA or I2S registers.
  audioQueue.buffersFifo.freeNextFilledBuffer();
}


bool audioHeadphoneDetect()
{
#if defined(KCX_BTAUDIO)
  return gpio_read(AUDIO_HP_DETECT_PIN) || btAudioLinked();
#else
  return gpio_read(AUDIO_HP_DETECT_PIN);
#endif
}

void audioSetVolume(uint8_t volume)
{
  // No-op: TAS2505 not populated on this dev board.
  (void)volume;
}

extern "C" void DMA1_Stream6_IRQHandler(void)
{
  // DMA IRQ not enabled (enable_dma_irqs() not called), should never fire.
}
