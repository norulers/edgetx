/*
 * Copyright (C) EdgeTX
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

/*
 * Crash / watchdog stall diagnostics.
 *
 * Fault handlers and the 1 ms timer interrupt capture the CPU context into a
 * memory area that survives the watchdog reset. On the next boot the record
 * is written to the SD card (LOGS/crash-*.txt), including in emergency mode.
 *
 * The capture hooks are weak references in the shared driver / bootloader
 * code (null when not linked); firmware builds with CRASH_DUMP defined link
 * the real implementation.
 */

#pragma once

#include <stdint.h>

enum CrashDumpType {
  CRASH_TYPE_NONE = 0,
  CRASH_TYPE_HARDFAULT = 1,
  CRASH_TYPE_MEMMANAGE = 2,
  CRASH_TYPE_BUSFAULT = 3,
  CRASH_TYPE_USAGEFAULT = 4,
  CRASH_TYPE_UNHANDLED_IRQ = 5,
  CRASH_TYPE_WATCHDOG_STALL = 6,
};

#if defined(__cplusplus)
extern "C" {
#endif

// Called from a fault handler. 'frame' points to the stacked exception frame
// (r0-r3, r12, lr, pc, xpsr) and 'regs' to r4-r11 of the faulting context;
// either may be null when not known.
void crashDumpFault(uint32_t type, const uint32_t* frame, uint32_t excReturn,
                    const uint32_t* regs);

// Called every millisecond from the 1 ms timer interrupt with the frame of the
// interrupted context.
void crashDumpTimerTick(const uint32_t* frame, uint32_t excReturn);

// Optional board hook called from the fault handlers after the capture
// (e.g. to print the fault on a debug UART).
void boardFaultHook(uint32_t type);

// Incremented on every watchdog reload, used to detect watchdog stalls.
extern volatile uint32_t watchdogResetCount;

#if defined(__cplusplus)
}

// Firmware side (CRASH_DUMP builds only)

// Move a record captured before the last reset aside, to be reported later.
// Must be called early in main(), before any new capture can happen.
void crashDumpBoot();

// Called periodically from the UI task: writes a pending record to the SD
// card once the radio has settled after boot.
void crashDumpPoll();

// Task currently holding the mixer lock (nullptr if none).
extern void* volatile crashDumpMixerLockOwner;
#endif
