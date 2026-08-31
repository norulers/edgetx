/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 */

#pragma once

#include <stdint.h>

struct HeartbeatCapture {
  uint8_t valid;
#if defined(DEBUG_LATENCY)
  uint32_t count;
#endif
};

extern volatile HeartbeatCapture heartbeatCapture;

void init_intmodule_heartbeat();
void stop_intmodule_heartbeat();
