/*
 * Copyright (C) EdgeTX
 *
 * Based on code named
 *   opentx - https://github.com/opentx/opentx
 *   th9x - http://code.google.com/p/th9x
 *   er9x - http://code.google.com/p/er9x
 *   gruvin9x - https://github.com/gruvin9x
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
 */

#pragma once

#include "hal/serial_driver.h"

// Fixed baudrate used by the App Config serial mode
#define APP_CONFIG_BAUDRATE 115200

// External application configuration protocol ("App Config" serial mode).
//
// Binds / unbinds the UART backing the App Config mode. Called by serial.cpp
// whenever the port mode changes (both when the mode is selected and when it
// is left). Pass (nullptr, nullptr) to detach.
//
// The protocol itself is documented in docs/development/app-config-protocol.md
void appConfigSetSerialDriver(void* ctx, const etx_serial_driver_t* drv);

// Called by storageDirty() to queue an unsolicited "settings changed" event
// for hosts that subscribed to it. Safe to call from any task context.
void appConfigNotifyDirty(uint8_t msk);
