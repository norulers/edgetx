/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 */

#include "heartbeat_driver.h"

#if defined(INTMODULE_HEARTBEAT_GPIO)
  #include "hal.h"
  #include "mixer_scheduler.h"
  #include "debug.h"

  volatile HeartbeatCapture heartbeatCapture;

  static void trigger_intmodule_heartbeat()
  {
  #if defined(DEBUG_LATENCY)
    heartbeatCapture.count++;
  #endif
    mixerSchedulerSoftTrigger();
  }

  void init_intmodule_heartbeat()
  {
    TRACE("init_intmodule_heartbeat");
    // TODO: AT32 EXINT support on INTMODULE_HEARTBEAT_GPIO
    heartbeatCapture.valid = true;
  }

  void stop_intmodule_heartbeat()
  {
    TRACE("stop_intmodule_heartbeat");
    heartbeatCapture.valid = false;
  }
#else
  volatile HeartbeatCapture heartbeatCapture;

  void init_intmodule_heartbeat()
  {
  }

  void stop_intmodule_heartbeat()
  {
  }
#endif
