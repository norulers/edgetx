/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 */

#include "hal/serial_port.h"
#include "board.h"
#include "dataconstants.h"

/*
 * AT32F435 board auxiliary (AUX) serial ports.
 *
 * NOTE (WIP): the AUX USART pins / driver are not wired up yet on the AT32
 * scaffold. The generic serial code still calls auxSerialGetPort(), so we
 * provide the symbol here and report no aux serial ports until the board
 * schematic is confirmed.
 */
const etx_serial_port_t* auxSerialGetPort(int port_nr)
{
  (void)port_nr;
  return nullptr;
}
