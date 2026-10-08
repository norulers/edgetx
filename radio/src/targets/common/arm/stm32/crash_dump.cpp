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
 * Crash / watchdog stall capture for STM32H74x/H75x, see hal/crash_dump.h.
 *
 * The records live at the end of the D3 SRAM (SRAM4), which neither the
 * firmware nor the bootloader use: the startup code does not clear it and it
 * keeps its content across the watchdog reset. Only 32-bit accesses are used
 * because the SRAM has ECC and is not initialised after power-on.
 */

// stm32_hal_ll.h first: definitions.h only defines UNUSED if not yet defined
#include "stm32_hal_ll.h"

#include "hal/crash_dump.h"
#include "hal/abnormal_reboot.h"
#include "hal/usb_driver.h"
#include "os/task.h"
#include "timers_driver.h"

#include "edgetx.h"
#include "fw_version.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#if !defined(D3_SRAM_BASE)
  #error "CRASH_DUMP needs the D3 SRAM of the STM32H74x/H75x"
#endif

extern uint32_t EXTRAM_START;
extern uint32_t _heap_end;
extern uint32_t _stext;
extern uint32_t _etext;

void* volatile crashDumpMixerLockOwner = nullptr;

namespace {

constexpr uint32_t CRASH_MAGIC = 0x43524432;  // "CRD2"

// The watchdog resets the radio about 500 ms after the last reload
constexpr uint32_t STALL_START_MS = 300;
constexpr uint32_t STALL_SAMPLE_MS = 40;
constexpr int STALL_SAMPLES = 4;

constexpr int NAME_WORDS = 4;    // up to 16 characters
constexpr int STACK_WORDS = 64;  // stack snapshot above the faulting SP

struct CrashRecord {
  uint32_t magic;
  uint32_t size;
  uint32_t type;
  uint32_t uptimeMs;
  uint32_t excReturn;
  uint32_t vector;  // exception active when captured
  uint32_t r0, r1, r2, r3, r12, lr, pc, xpsr, sp;
  uint32_t cfsr, hfsr, mmfar, bfar;
  uint32_t sinceKickMs;
  uint32_t task[NAME_WORDS];
  uint32_t lockOwner[NAME_WORDS];
  uint32_t samples;
  uint32_t samplePc[STALL_SAMPLES];
  uint32_t sampleLr[STALL_SAMPLES];
  uint32_t sampleCtx[STALL_SAMPLES];  // interrupted exception (0 = task)
  uint32_t regsValid;
  uint32_t r4_11[8];
  uint32_t stackWords;
  uint32_t stack[STACK_WORDS];
  uint32_t checksum;
};

constexpr uint32_t RECORD_WORDS = sizeof(CrashRecord) / 4;
constexpr uint32_t SLOT_SIZE = 512;
static_assert(sizeof(CrashRecord) <= SLOT_SIZE, "crash record too large");

// Two slots at the end of SRAM4: 0 = capture, 1 = waiting to be reported
constexpr uint32_t D3_SRAM_SIZE = 0x10000;
constexpr uint32_t SLOTS_BASE = D3_SRAM_BASE + D3_SRAM_SIZE - 2 * SLOT_SIZE;

inline volatile CrashRecord* slot(int i)
{
  return (volatile CrashRecord*)(SLOTS_BASE + i * SLOT_SIZE);
}

// stall detector state (1 ms interrupt)
uint32_t lastKickCount = 0;
uint32_t sinceKick = 0;
bool armed = false;
bool stallActive = false;

volatile bool inFault = false;

void flush(volatile CrashRecord* r)
{
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
  SCB_CleanDCache_by_Addr((volatile void*)r, SLOT_SIZE);
#endif
}

// covers everything but the magic (set last) and the checksum itself
uint32_t checksum(volatile const CrashRecord* r)
{
  auto w = (volatile const uint32_t*)r;
  uint32_t h = 0x811C9DC5;
  for (uint32_t i = 1; i < RECORD_WORDS - 1; i++) {
    h ^= w[i];
    h *= 0x01000193;
  }
  return h;
}

bool isValid(volatile const CrashRecord* r)
{
  return r->magic == CRASH_MAGIC && r->size == sizeof(CrashRecord) &&
         r->checksum == checksum(r);
}

void seal(volatile CrashRecord* r)
{
  r->size = sizeof(CrashRecord);
  r->checksum = checksum(r);
  r->magic = CRASH_MAGIC;
  flush(r);
}

void clear(volatile CrashRecord* r)
{
  auto w = (volatile uint32_t*)r;
  for (uint32_t i = 0; i < SLOT_SIZE / 4; i++) w[i] = 0;
  flush(r);
}

bool inRam(uint32_t addr, uint32_t len)
{
  static const uint32_t ranges[][2] = {
      {0x20000000, 0x20020000},  // DTCM
      {0x24000000, 0x24080000},  // AXI SRAM
      {0x30000000, 0x30048000},  // D2 SRAM
      {0x38000000, 0x38010000},  // D3 SRAM
  };
  if (addr + len < addr) return false;
  for (auto& rg : ranges) {
    if (addr >= rg[0] && addr + len <= rg[1]) return true;
  }
  return addr >= (uint32_t)&EXTRAM_START && addr + len <= (uint32_t)&_heap_end;
}

bool frameValid(const uint32_t* frame)
{
  return frame && ((uint32_t)frame & 3) == 0 && inRam((uint32_t)frame, 32);
}

void captureFrame(volatile CrashRecord* r, const uint32_t* frame,
                  uint32_t excReturn)
{
  if (frameValid(frame)) {
    r->r0 = frame[0];
    r->r1 = frame[1];
    r->r2 = frame[2];
    r->r3 = frame[3];
    r->r12 = frame[4];
    r->lr = frame[5];
    r->pc = frame[6];
    r->xpsr = frame[7];
    // stack pointer before the exception entry (FP context, alignment pad)
    uint32_t size = (excReturn & 0x10) ? 32 : 104;
    if (frame[7] & (1u << 9)) size += 4;
    r->sp = (uint32_t)frame + size;
  } else {
    r->r0 = r->r1 = r->r2 = r->r3 = r->r12 = 0;
    r->lr = r->pc = r->xpsr = r->sp = 0;
  }
}

void putName(volatile uint32_t* dst, void* handle)
{
  uint32_t words[NAME_WORDS] = {};
  if (handle && xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED &&
      inRam((uint32_t)handle, sizeof(StaticTask_t))) {
    const char* name = pcTaskGetName((TaskHandle_t)handle);
    const uint32_t len = configMAX_TASK_NAME_LEN < NAME_WORDS * 4
                             ? configMAX_TASK_NAME_LEN
                             : NAME_WORDS * 4;
    if (inRam((uint32_t)name, len)) {
      for (uint32_t i = 0; i < len && name[i]; i++)
        words[i / 4] |= (uint32_t)(uint8_t)name[i] << (8 * (i % 4));
    }
  }
  for (int i = 0; i < NAME_WORDS; i++) dst[i] = words[i];
}

void clearName(volatile uint32_t* dst)
{
  for (int i = 0; i < NAME_WORDS; i++) dst[i] = 0;
}

void* currentTask()
{
  if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) return nullptr;
  return xTaskGetCurrentTaskHandle();
}

void captureRegs(volatile CrashRecord* r, const uint32_t* regs)
{
  const bool ok = regs && ((uint32_t)regs & 3) == 0 && inRam((uint32_t)regs, 32);
  r->regsValid = ok;
  for (int i = 0; i < 8; i++) r->r4_11[i] = ok ? regs[i] : 0;
}

// Copy of the stack above the pre-exception SP: return addresses found there
// give the call chain
void captureStack(volatile CrashRecord* r)
{
  uint32_t n = 0;
  const uint32_t sp = r->sp;
  if (sp && (sp & 3) == 0) {
    for (; n < (uint32_t)STACK_WORDS; n++) {
      const uint32_t addr = sp + 4 * n;
      if (!inRam(addr, 4)) break;
      r->stack[n] = *(const volatile uint32_t*)addr;
    }
  }
  r->stackWords = n;
  for (uint32_t i = n; i < (uint32_t)STACK_WORDS; i++) r->stack[i] = 0;
}

}  // namespace

//-----------------------------------------------------------------------------
// Capture (fault handlers / 1 ms interrupt)
//-----------------------------------------------------------------------------

extern "C" void crashDumpFault(uint32_t type, const uint32_t* frame,
                               uint32_t excReturn, const uint32_t* regs)
{
  // a fault inside the fault handler: keep the first one
  if (inFault) return;
  inFault = true;

  volatile CrashRecord* r = slot(0);
  r->magic = 0;
  r->type = type;
  r->uptimeMs = timersGetMsTick();
  r->excReturn = excReturn;
  r->vector = __get_IPSR() & 0x1FF;
  captureFrame(r, frame, excReturn);
  captureRegs(r, regs);
  r->cfsr = SCB->CFSR;
  r->hfsr = SCB->HFSR;
  r->mmfar = SCB->MMFAR;
  r->bfar = SCB->BFAR;
  r->sinceKickMs = sinceKick;
  r->samples = 0;
  r->stackWords = 0;
  clearName(r->task);
  clearName(r->lockOwner);
  seal(r);

  // stack and task names last: reading them could fault again
  captureStack(r);
  seal(r);
  putName(r->task, currentTask());
  putName(r->lockOwner, crashDumpMixerLockOwner);
  seal(r);
}

extern "C" void crashDumpTimerTick(const uint32_t* frame, uint32_t excReturn)
{
  const uint32_t kick = watchdogResetCount;
  if (kick != lastKickCount) {
    lastKickCount = kick;
    sinceKick = 0;
    armed = true;
    if (stallActive) {
      // reloaded in time after all: not a crash, drop the record
      stallActive = false;
      clear(slot(0));
    }
    return;
  }

  if (!armed) return;
  if (++sinceKick < STALL_START_MS) return;
  if ((sinceKick - STALL_START_MS) % STALL_SAMPLE_MS) return;

  volatile CrashRecord* r = slot(0);
  if (!stallActive) {
    if (inFault || isValid(r)) return;
    stallActive = true;
    r->magic = 0;
    r->type = CRASH_TYPE_WATCHDOG_STALL;
    r->uptimeMs = timersGetMsTick();
    r->excReturn = excReturn;
    r->vector = frameValid(frame) ? (frame[7] & 0x1FF) : 0;
    captureFrame(r, frame, excReturn);
    r->cfsr = SCB->CFSR;
    r->hfsr = SCB->HFSR;
    r->mmfar = SCB->MMFAR;
    r->bfar = SCB->BFAR;
    r->samples = 0;
    captureRegs(r, nullptr);
    captureStack(r);
    putName(r->task, currentTask());
    putName(r->lockOwner, crashDumpMixerLockOwner);
  }

  const uint32_t n = r->samples;
  if (n < STALL_SAMPLES) {
    const bool ok = frameValid(frame);
    r->samplePc[n] = ok ? frame[6] : 0;
    r->sampleLr[n] = ok ? frame[5] : 0;
    r->sampleCtx[n] = ok ? (frame[7] & 0x1FF) : 0;
    r->samples = n + 1;
  }
  r->sinceKickMs = sinceKick;
  seal(r);
}

//-----------------------------------------------------------------------------
// Boot / report (firmware tasks)
//-----------------------------------------------------------------------------

void crashDumpBoot()
{
  volatile CrashRecord* cap = slot(0);
  volatile CrashRecord* pending = slot(1);

  // After power-on / brown-out the SRAM content and its ECC are random: never
  // read it then, only initialise it. The reset flags are still set here, the
  // bootloader does not clear them.
  if (LL_RCC_IsActiveFlag_PORRST() || LL_RCC_IsActiveFlag_BORRST()) {
    clear(pending);
    clear(cap);
    return;
  }

  if (isValid(cap)) {
    auto src = (volatile const uint32_t*)cap;
    auto dst = (volatile uint32_t*)pending;
    for (uint32_t i = 0; i < SLOT_SIZE / 4; i++) dst[i] = src[i];
    flush(pending);
  } else if (!isValid(pending)) {
    clear(pending);
  }
  clear(cap);
}

namespace {

const char* typeName(uint32_t type)
{
  switch (type) {
    case CRASH_TYPE_HARDFAULT: return "HardFault";
    case CRASH_TYPE_MEMMANAGE: return "MemManage fault";
    case CRASH_TYPE_BUSFAULT: return "BusFault";
    case CRASH_TYPE_USAGEFAULT: return "UsageFault";
    case CRASH_TYPE_UNHANDLED_IRQ: return "Unhandled interrupt";
    case CRASH_TYPE_WATCHDOG_STALL: return "Watchdog stall (no reload for too long)";
    default: return "Unknown";
  }
}

const char* resetName(uint32_t cause)
{
  switch (cause) {
    case ARC_Watchdog: return "watchdog";
    case ARC_Software: return "software";
    case ARC_None: return "power-on / pin";
    default: return "unknown";
  }
}

void decodeFaultStatus(char* buf, size_t len, uint32_t cfsr, uint32_t hfsr)
{
  static const struct {
    uint32_t    mask;
    const char* name;
  } bits[] = {
      {1u << 0, "IACCVIOL"},    {1u << 1, "DACCVIOL"},   {1u << 3, "MUNSTKERR"},
      {1u << 4, "MSTKERR"},     {1u << 5, "MLSPERR"},    {1u << 7, "MMARVALID"},
      {1u << 8, "IBUSERR"},     {1u << 9, "PRECISERR"},  {1u << 10, "IMPRECISERR"},
      {1u << 11, "UNSTKERR"},   {1u << 12, "STKERR"},    {1u << 13, "LSPERR"},
      {1u << 15, "BFARVALID"},  {1u << 16, "UNDEFINSTR"}, {1u << 17, "INVSTATE"},
      {1u << 18, "INVPC"},      {1u << 19, "NOCP"},      {1u << 24, "UNALIGNED"},
      {1u << 25, "DIVBYZERO"},
  };
  size_t pos = 0;
  buf[0] = '\0';
  for (auto& b : bits) {
    if ((cfsr & b.mask) && pos < len)
      pos += snprintf(buf + pos, len - pos, " %s", b.name);
  }
  if ((hfsr & (1u << 30)) && pos < len)
    pos += snprintf(buf + pos, len - pos, " FORCED");
  if ((hfsr & (1u << 1)) && pos < len)
    snprintf(buf + pos, len - pos, " VECTTBL");
}

void nameToString(const uint32_t* words, char* out)
{
  for (int i = 0; i < NAME_WORDS * 4; i++)
    out[i] = (char)(words[i / 4] >> (8 * (i % 4)));
  out[NAME_WORDS * 4] = '\0';
  if (!out[0]) strcpy(out, "-");
}

__attribute__((format(__printf__, 2, 3)))
void fput(FIL* f, const char* fmt, ...)
{
  char line[192];
  va_list args;
  va_start(args, fmt);
  vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);
  f_puts(line, f);
}

inline unsigned long hex(uint32_t v) { return v; }

bool writeReport(const CrashRecord& r)
{
  if (sdCheckAndCreateDirectory(LOGS_PATH) != nullptr) return false;

  struct gtm t;
  gettime(&t);
  char path[64];
  snprintf(path, sizeof(path), LOGS_PATH "/crash-%04d-%02d-%02d-%02d%02d%02d.txt",
           t.tm_year + TM_YEAR_BASE, t.tm_mon + 1, t.tm_mday, t.tm_hour,
           t.tm_min, t.tm_sec);

  FIL file;
  FIL* f = &file;
  if (f_open(f, path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) return false;

  char name[NAME_WORDS * 4 + 1];

  f_puts("EdgeTX crash report\n", f);
  fput(f, "firmware  : %s\n", vers_stamp);
  fput(f, "reset     : %s\n", resetName(abnormalRebootGetCause()));
  fput(f, "type      : %s\n", typeName(r.type));
  fput(f, "uptime    : %lu ms\n", hex(r.uptimeMs));
  nameToString(r.task, name);
  fput(f, "task      : %s\n", name);
  nameToString(r.lockOwner, name);
  fput(f, "mixer lock: %s\n", name);
  fput(f, "no reload : %lu ms\n", hex(r.sinceKickMs));
  fput(f, "exception : %lu (EXC_RETURN 0x%08lX, %s stack)\n", hex(r.vector),
       hex(r.excReturn), (r.excReturn & 4) ? "task/PSP" : "handler/MSP");
  f_puts("\n", f);
  fput(f, "PC  : 0x%08lX\n", hex(r.pc));
  fput(f, "LR  : 0x%08lX\n", hex(r.lr));
  fput(f, "SP  : 0x%08lX  xPSR: 0x%08lX\n", hex(r.sp), hex(r.xpsr));
  fput(f, "R0  : 0x%08lX  R1: 0x%08lX  R2: 0x%08lX  R3: 0x%08lX  R12: 0x%08lX\n",
       hex(r.r0), hex(r.r1), hex(r.r2), hex(r.r3), hex(r.r12));
  if (r.regsValid) {
    fput(f, "R4  : 0x%08lX  R5: 0x%08lX  R6: 0x%08lX  R7: 0x%08lX\n",
         hex(r.r4_11[0]), hex(r.r4_11[1]), hex(r.r4_11[2]), hex(r.r4_11[3]));
    fput(f, "R8  : 0x%08lX  R9: 0x%08lX  R10: 0x%08lX  R11: 0x%08lX\n",
         hex(r.r4_11[4]), hex(r.r4_11[5]), hex(r.r4_11[6]), hex(r.r4_11[7]));
  }

  char bits[160];
  decodeFaultStatus(bits, sizeof(bits), r.cfsr, r.hfsr);
  fput(f, "CFSR: 0x%08lX  HFSR: 0x%08lX %s\n", hex(r.cfsr), hex(r.hfsr), bits);
  if (r.cfsr & (1u << 7)) fput(f, "MMFAR (bad address): 0x%08lX\n", hex(r.mmfar));
  if (r.cfsr & (1u << 15)) fput(f, "BFAR  (bad address): 0x%08lX\n", hex(r.bfar));

  const uint32_t samples = r.samples <= STALL_SAMPLES ? r.samples : STALL_SAMPLES;
  if (samples) {
    f_puts("\nstall samples (PC / LR / interrupted exception, 0 = task):\n", f);
    for (uint32_t i = 0; i < samples; i++) {
      fput(f, "  %lu ms: 0x%08lX  0x%08lX  %lu\n",
           hex(STALL_START_MS + i * STALL_SAMPLE_MS), hex(r.samplePc[i]),
           hex(r.sampleLr[i]), hex(r.sampleCtx[i]));
    }
  }

  // return addresses on the stack (Thumb bit set, inside the code section)
  const uint32_t stackWords =
      r.stackWords <= (uint32_t)STACK_WORDS ? r.stackWords : STACK_WORDS;
  const uint32_t textStart = (uint32_t)&_stext, textEnd = (uint32_t)&_etext;
  uint32_t calls[STACK_WORDS];
  uint32_t nCalls = 0;
  for (uint32_t i = 0; i < stackWords; i++) {
    const uint32_t w = r.stack[i];
    if ((w & 1) && w > textStart && w < textEnd) calls[nCalls++] = w & ~1u;
  }

  if (stackWords) {
    fput(f, "\nstack at 0x%08lX (%lu words):\n", hex(r.sp), hex(stackWords));
    for (uint32_t i = 0; i < stackWords; i += 8) {
      fput(f, "  +%03lX:", hex(i * 4));
      for (uint32_t j = i; j < i + 8 && j < stackWords; j++)
        fput(f, " %08lX", hex(r.stack[j]));
      f_puts("\n", f);
    }
  }

  f_puts("\ndecode with the firmware.elf of this exact build:\n", f);
  fput(f, "  arm-none-eabi-addr2line -e firmware.elf -f -C -i 0x%08lX 0x%08lX",
       hex(r.pc), hex(r.lr));
  for (uint32_t i = 0; i < samples; i++)
    fput(f, " 0x%08lX 0x%08lX", hex(r.samplePc[i]), hex(r.sampleLr[i]));
  f_puts("\n", f);
  if (nCalls) {
    f_puts("possible call chain (return addresses found on the stack):\n", f);
    f_puts("  arm-none-eabi-addr2line -e firmware.elf -f -C -i", f);
    for (uint32_t i = 0; i < nCalls; i++) fput(f, " 0x%08lX", hex(calls[i]));
    f_puts("\n", f);
  }

  return f_close(f) == FR_OK;
}

}  // namespace

void crashDumpPoll()
{
  static bool done = false;
  // leave the radio settle after boot first
  if (done || timersGetMsTick() < 5000) return;

  volatile CrashRecord* pending = slot(1);
  if (!isValid(pending)) {
    done = true;
    return;
  }

  // the card belongs to the host while in USB storage mode: retry later
  if (usbPluggedInStorageMode()) return;
  done = true;

  CrashRecord rec;
  auto src = (volatile const uint32_t*)pending;
  auto dst = (uint32_t*)&rec;
  for (uint32_t i = 0; i < RECORD_WORDS; i++) dst[i] = src[i];

  // emergency mode does not mount the SD card: mount it just for the report
  bool mountedHere = false;
  if (!sdMounted()) {
    if (!sdMountRaw()) return;  // keep the record for the next boot
    mountedHere = true;
  }

  const bool ok = writeReport(rec);
  if (mountedHere) sdUnmountRaw();

  if (ok) {
    TRACE("crash report saved");
    clear(pending);
  }
}
