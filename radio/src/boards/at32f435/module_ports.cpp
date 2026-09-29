/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * AT32 module port table.
 *
 * NOTE (WIP): the internal (CRSF) and external module hardware (USART / timer
 * pins, DMA, power, ...) is not yet described for AT32F435 -- see
 * boards/at32f435/PORTING.md. These placeholders let the firmware link; the
 * module ports must be filled in once the schematic is known.
 */

#include "hal/module_port.h"

#include "definitions.h"  // for DIM()

#if defined(HARDWARE_INTERNAL_MODULE)
static const etx_module_port_t _internal_ports[] = {};
static const etx_module_t _internal_module = {
  .ports = _internal_ports,
  .set_pwr = nullptr,
  .set_bootcmd = nullptr,
  .n_ports = 0,
};
#endif

#if defined(HARDWARE_EXTERNAL_MODULE)
static const etx_module_port_t _external_ports[] = {};
static const etx_module_t _external_module = {
  .ports = _external_ports,
  .set_pwr = nullptr,
  .set_bootcmd = nullptr,
  .n_ports = 0,
};
#endif

BEGIN_MODULES()
#if defined(HARDWARE_INTERNAL_MODULE)
  &_internal_module,
#else
  nullptr,
#endif
#if defined(HARDWARE_EXTERNAL_MODULE)
  &_external_module,
#else
  nullptr,
#endif
END_MODULES()
