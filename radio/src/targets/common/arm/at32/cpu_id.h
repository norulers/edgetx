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

#include <inttypes.h>
#include "definitions.h"

EXTERN_C(void getCPUUniqueID(char * s));

#define LEN_CPU_UID                    (3*8+2)

#if defined(SIMU)
extern const uint32_t cpu_uid[3];
#else
extern const uint32_t * const cpu_uid;
#endif
