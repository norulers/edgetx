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

#include "app_config.h"

// Queued storage-change notification, see appConfigNotifyDirty()
static volatile uint8_t appConfigEventMask = 0;

void appConfigNotifyDirty(uint8_t msk)
{
  appConfigEventMask |= msk;
}

#if !defined(BOOT)

#include "edgetx.h"
#include "myeeprom.h"
#include "mixes.h"
#include "curves.h"
#include "os/timer.h"
#include "storage/storage.h"
#include "tasks/mixer_task.h"
#include "hal/adc_driver.h"
#include "hal/audio_driver.h"
#include "input_mapping.h"
#include "settings_tree.h"
#include "sdcard.h"
#include "stamp.h"

// The native (simulator / test) build has no board backlight limits
#if !defined(BACKLIGHT_LEVEL_MIN)
  #define BACKLIGHT_LEVEL_MIN 1
#endif
#if !defined(BACKLIGHT_LEVEL_MAX)
  #define BACKLIGHT_LEVEL_MAX 100
#endif

#if defined(GVARS)
  #include "gvars.h"
#endif

#if defined(COLORLCD)
  #include "storage/ui_screens_yaml.h"
#endif

#if defined(STORAGE_MODELSLIST)
  #include "storage/modelslist.h"
#endif

#include <string.h>

//
// Wire format (little endian for all multi-byte fields):
//
//   +-------+-------+-------+-------+-------+---------+---------+--------+
//   | SYNC0 | SYNC1 |  CMD  |  SEQ  |  LEN  | PAYLOAD | CRC16lo | CRC16hi|
//   | 0xA5  | 0x5A  |       |       | 0..64 |         |         |        |
//   +-------+-------+-------+-------+-------+---------+---------+--------+
//
// CRC16/CCITT-FALSE (poly 0x1021, init 0xFFFF) over CMD, SEQ, LEN, PAYLOAD.
//
// Responses echo CMD and SEQ. PAYLOAD[0] is a status byte, the remaining
// bytes are command specific data.
//
// Mutating commands carry a sequence number: if the very same (CMD, SEQ)
// pair is received again the cached response is resent without re-executing
// the command, so a retransmission is harmless.
//

#define APP_SYNC0               0xA5
#define APP_SYNC1               0x5A

#define APP_MAX_PAYLOAD         64
#define APP_MAX_FRAME           (8 + APP_MAX_PAYLOAD)

#define APP_PROTO_VERSION       1

#define APP_TIMER_PERIOD_MS     10

// ---------------------------------------------------------------------------
// Status codes
// ---------------------------------------------------------------------------
enum {
  APP_STATUS_OK = 0,
  APP_STATUS_ERROR,
  APP_STATUS_BAD_CMD,
  APP_STATUS_BAD_PARAM,
  APP_STATUS_RANGE,
  APP_STATUS_UNSUPPORTED,
  APP_STATUS_NOT_READY,
};

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------
enum {
  CMD_GET_INFO        = 0x01,

  // unsolicited notification sent by the radio (sequence number 0)
  CMD_EVENT           = 0x00,

  CMD_MIX_COUNT       = 0x10,
  CMD_MIX_GET         = 0x11,
  CMD_MIX_INSERT      = 0x12,
  CMD_MIX_SET         = 0x13,
  CMD_MIX_DELETE      = 0x14,
  CMD_MIX_MOVE        = 0x15,

  CMD_INPUT_COUNT     = 0x16,
  CMD_INPUT_GET       = 0x17,
  CMD_INPUT_INSERT    = 0x18,
  CMD_INPUT_SET       = 0x19,
  CMD_INPUT_DELETE    = 0x1A,
  CMD_INPUT_MOVE      = 0x1B,
  CMD_INPUT_GET_NAME  = 0x1C,
  CMD_INPUT_SET_NAME  = 0x1D,

  CMD_OUTPUT_GET      = 0x20,
  CMD_OUTPUT_SET      = 0x21,
  CMD_OUTPUT_SET_NAME = 0x22,

  CMD_CURVE_GET       = 0x23,
  CMD_CURVE_SET       = 0x24,
  CMD_CURVE_SET_POINT = 0x25,
  CMD_CURVE_CLEAR     = 0x26,
  CMD_CURVE_MIRROR    = 0x27,

  CMD_LS_GET          = 0x28,
  CMD_LS_SET          = 0x29,

  CMD_CF_GET          = 0x2B,
  CMD_CF_SET          = 0x2C,

  CMD_MODEL_GET_NAME  = 0x30,
  CMD_MODEL_SET_NAME  = 0x31,
  CMD_TIMER_GET       = 0x32,
  CMD_TIMER_SET       = 0x33,
  CMD_MODEL_LIST      = 0x34,
  CMD_MODEL_SELECT    = 0x35,
  CMD_MODEL_CREATE    = 0x36,
  CMD_MODEL_DUPLICATE = 0x37,
  CMD_MODEL_DELETE    = 0x38,
  CMD_MODEL_RENAME    = 0x39,

  CMD_GENERAL_GET     = 0x40,
  CMD_GENERAL_SET     = 0x41,
  CMD_RF_POWER_GET    = 0x42,
  CMD_RF_POWER_SET    = 0x43,

  CMD_FM_GET          = 0x50,
  CMD_FM_SET          = 0x51,
  CMD_FM_SET_TRIM     = 0x52,

  CMD_GET_STATUS      = 0x60,

  CMD_SUBSCRIBE       = 0x70,

  // Generic settings access (see settings_tree.h)
  CMD_PARAM_LIST      = 0x80,
  CMD_PARAM_GET       = 0x81,
  CMD_PARAM_SET       = 0x82,
  CMD_PARAM_EXPORT    = 0x83,

  // Global variable runtime values
  CMD_GVAR_GET        = 0x86,
  CMD_GVAR_SET        = 0x87,
};

// Event types (CMD_EVENT payload[0])
enum {
  EVENT_SETTINGS_CHANGED = 1,
};

// Subscription bits (CMD_SUBSCRIBE)
#define SUBSCRIBE_SETTINGS_CHANGED     0x01

// General settings ids (CMD_GENERAL_GET / CMD_GENERAL_SET)
enum {
  GEN_ID_VOLUME = 1,   // 0 .. VOLUME_LEVEL_MAX
  GEN_ID_BEEP_VOLUME,
  GEN_ID_WAV_VOLUME,
  GEN_ID_VARIO_VOLUME,
  GEN_ID_BACKGROUND_VOLUME,
  GEN_ID_HAPTIC_STRENGTH,
  GEN_ID_HAPTIC_LENGTH,
  GEN_ID_BEEP_LENGTH,
  GEN_ID_BACKLIGHT,    // 1 .. BACKLIGHT_LEVEL_MAX
};

// Mixer line blob: fixed part + optional name
#define MIX_BLOB_FIXED          20

// Input (expo) line blob: fixed part + optional name
#define EXPO_BLOB_FIXED         18

// Special function blob: fixed part + optional name
#define CF_BLOB_FIXED           15

#define MAX_CURVE_EDIT_POINTS   17

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static inline uint16_t getU16(const uint8_t* p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static inline void putU16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)(v & 0xFF); p[1] = (uint8_t)(v >> 8); }

static inline uint32_t getU32(const uint8_t* p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline void putU32(uint8_t* p, uint32_t v)
{
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
  p[2] = (uint8_t)((v >> 16) & 0xFF);
  p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static uint16_t crc16(const uint8_t* data, uint32_t len)
{
  uint16_t crc = 0xFFFF;
  while (len--) {
    crc ^= (uint16_t)(*data++) << 8;
    for (uint8_t i = 0; i < 8; i++) {
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
  }
  return crc;
}

static uint8_t copyNameOut(uint8_t* dst, const char* src, uint8_t maxLen)
{
  uint8_t len = 0;
  while (len < maxLen && src[len] != '\0') len++;
  memcpy(dst, src, len);
  return len;
}

static void setNameIn(char* dst, uint8_t dstSize, const uint8_t* src, uint8_t len)
{
  if (dstSize == 0) return;
  // keep one byte for the terminating NUL, like the on-screen editors do
  if (len > (uint8_t)(dstSize - 1)) len = dstSize - 1;
  memset(dst, 0, dstSize);
  memcpy(dst, src, len);
}

// Only allow edits once the mixer has been started at least once, otherwise
// mixerTaskStop()/mixerTaskStart() would falsely mark the mixer as running.
static inline bool modelEditable() { return mixerTaskStarted(); }

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static void* appCtx = nullptr;
static const etx_serial_driver_t* appDrv = nullptr;
static timer_handle_t appTimer = TIMER_INITIALIZER;

// Frame parser
enum {
  PARSE_SYNC0 = 0,
  PARSE_SYNC1,
  PARSE_CMD,
  PARSE_SEQ,
  PARSE_LEN,
  PARSE_PAYLOAD,
  PARSE_CRC_LO,
  PARSE_CRC_HI,
};

static uint8_t rxFrame[APP_MAX_FRAME];
static uint8_t rxState = PARSE_SYNC0;
static uint8_t rxPayloadIdx = 0;
static uint8_t rxCrcLo = 0;

// Cached answer of the last mutating command, used for retransmissions
static uint8_t lastWriteCmd = 0;
static uint8_t lastWriteSeq = 0;
static bool lastWriteValid = false;
static uint8_t lastWriteResp[APP_MAX_FRAME];
static uint8_t lastWriteRespLen = 0;

// Unsolicited event subscription (see CMD_SUBSCRIBE)
static uint8_t subscribedEvents = 0;

// Pending model change: the radio reboots once the settings have been stored
static uint16_t modelSwitchCountdown = 0;
static uint16_t eventCooldown = 0;

// ---------------------------------------------------------------------------
// Transmission
// ---------------------------------------------------------------------------
static void sendRaw(const uint8_t* buf, uint8_t len)
{
  if (!appDrv) return;

  if (appDrv->sendBuffer) {
    appDrv->sendBuffer(appCtx, buf, len);
  } else if (appDrv->sendByte) {
    for (uint8_t i = 0; i < len; i++) {
      appDrv->sendByte(appCtx, buf[i]);
    }
  }
}

// ---------------------------------------------------------------------------
// Mixer helpers
// ---------------------------------------------------------------------------
static uint8_t mixFirstLine(uint8_t ch)
{
  uint8_t count = getMixCount();
  for (uint8_t i = 0; i < count; i++) {
    auto mix = mixAddress(i);
    if (!mix->srcRaw || mix->destCh >= ch) return i;
  }
  return count;
}

static uint8_t mixLineCount(uint8_t ch)
{
  uint8_t count = getMixCount();
  uint8_t lines = 0;
  for (uint8_t i = mixFirstLine(ch); i < count; i++) {
    auto mix = mixAddress(i);
    if (!mix->srcRaw || mix->destCh != ch) break;
    lines++;
  }
  return lines;
}

static uint8_t readMixBlob(const MixData* mix, uint8_t* out, uint8_t* outLen)
{
  putU16(&out[0], (uint16_t)(int16_t)mix->srcRaw);
  putU16(&out[2], (uint16_t)sourceNumValToLuaInt(mix->weight));
  putU16(&out[4], (uint16_t)sourceNumValToLuaInt(mix->offset));
  putU16(&out[6], (uint16_t)(int16_t)mix->swtch);
  out[8] = (uint8_t)mix->curve.type;
  putU16(&out[9], (uint16_t)sourceNumValToLuaInt(mix->curve.value));
  out[11] = (uint8_t)mix->mltpx;
  putU16(&out[12], (uint16_t)mix->flightModes);
  out[14] = (mix->carryTrim ? 0x01 : 0x00) |
            ((uint8_t)(mix->mixWarn & 0x03) << 1) |
            (mix->delayPrec ? 0x08 : 0x00) |
            (mix->speedPrec ? 0x10 : 0x00);
  out[15] = mix->delayUp;
  out[16] = mix->delayDown;
  out[17] = mix->speedUp;
  out[18] = mix->speedDown;
  out[19] = copyNameOut(&out[20], mix->name, (uint8_t)sizeof(mix->name));
  *outLen = MIX_BLOB_FIXED + out[19];
  return APP_STATUS_OK;
}

static uint8_t writeMixBlob(MixData* mix, const uint8_t* in, uint8_t inLen)
{
  if (inLen < MIX_BLOB_FIXED) return APP_STATUS_BAD_PARAM;

  mix->srcRaw = (int16_t)getU16(&in[0]);
  mix->weight = luaIntToSourceNumval((int16_t)getU16(&in[2]));
  mix->offset = luaIntToSourceNumval((int16_t)getU16(&in[4]));
  mix->swtch = (int16_t)getU16(&in[6]);
  mix->curve.type = in[8] & 0x1F;
  mix->curve.value = luaIntToSourceNumval((int16_t)getU16(&in[9]));
  mix->mltpx = in[11] & 0x03;
  mix->flightModes = getU16(&in[12]) & 0x01FF;
  mix->carryTrim = in[14] & 0x01;
  mix->mixWarn = (in[14] >> 1) & 0x03;
  mix->delayPrec = (in[14] >> 3) & 0x01;
  mix->speedPrec = (in[14] >> 4) & 0x01;
  mix->delayUp = in[15];
  mix->delayDown = in[16];
  mix->speedUp = in[17];
  mix->speedDown = in[18];

  uint8_t nameLen = in[19];
  if (nameLen) {
    if ((uint16_t)MIX_BLOB_FIXED + nameLen > inLen) return APP_STATUS_BAD_PARAM;
    setNameIn(mix->name, (uint8_t)sizeof(mix->name), &in[MIX_BLOB_FIXED], nameLen);
  }

  return APP_STATUS_OK;
}

// ---------------------------------------------------------------------------
// Input (expo) helpers
// ---------------------------------------------------------------------------
static uint8_t inputFirstLine(uint8_t ch)
{
  for (uint8_t i = 0; i < MAX_EXPOS; i++) {
    ExpoData* expo = expoAddress(i);
    if (!expo->srcRaw || expo->chn >= ch) return i;
  }
  return MAX_EXPOS;
}

static uint8_t inputLineCount(uint8_t ch)
{
  uint8_t lines = 0;
  for (uint8_t i = inputFirstLine(ch); i < MAX_EXPOS; i++) {
    ExpoData* expo = expoAddress(i);
    if (!expo->srcRaw || expo->chn != ch) break;
    lines++;
  }
  return lines;
}

static bool inputIsUsed(uint8_t ch)
{
  for (uint8_t i = 0; i < MAX_EXPOS; i++) {
    ExpoData* expo = expoAddress(i);
    if (expo->mode && expo->chn == ch) return true;
  }
  return false;
}

static uint8_t expoCount()
{
  uint8_t count = 0;
  for (uint8_t i = 0; i < MAX_EXPOS; i++) {
    if (EXPO_VALID(expoAddress(i))) count++;
  }
  return count;
}

static uint8_t readExpoBlob(const ExpoData* expo, uint8_t* out, uint8_t* outLen)
{
  putU16(&out[0], (uint16_t)(int16_t)expo->srcRaw);
  putU16(&out[2], (uint16_t)expo->scale);
  putU16(&out[4], (uint16_t)sourceNumValToLuaInt(expo->weight));
  putU16(&out[6], (uint16_t)sourceNumValToLuaInt(expo->offset));
  putU16(&out[8], (uint16_t)(int16_t)expo->swtch);
  out[10] = (uint8_t)expo->curve.type;
  putU16(&out[11], (uint16_t)sourceNumValToLuaInt(expo->curve.value));
  out[13] = (uint8_t)(int8_t)expo->trimSource;
  out[14] = (uint8_t)expo->mode;
  putU16(&out[15], (uint16_t)expo->flightModes);
  out[17] = copyNameOut(&out[18], expo->name, (uint8_t)sizeof(expo->name));
  *outLen = EXPO_BLOB_FIXED + out[17];
  return APP_STATUS_OK;
}

static uint8_t writeExpoBlob(ExpoData* expo, const uint8_t* in, uint8_t inLen)
{
  if (inLen < EXPO_BLOB_FIXED) return APP_STATUS_BAD_PARAM;

  expo->srcRaw = (int16_t)getU16(&in[0]);
  expo->scale = getU16(&in[2]) & 0x3FFF;
  expo->weight = luaIntToSourceNumval((int16_t)getU16(&in[4]));
  expo->offset = luaIntToSourceNumval((int16_t)getU16(&in[6]));
  expo->swtch = (int16_t)getU16(&in[8]);
  expo->curve.type = in[10] & 0x07;
  expo->curve.value = luaIntToSourceNumval((int16_t)getU16(&in[11]));
  expo->trimSource = (int8_t)in[13];
  expo->mode = in[14] & 0x03;
  expo->flightModes = getU16(&in[15]) & 0x01FF;

  uint8_t nameLen = in[17];
  if (nameLen) {
    if ((uint16_t)EXPO_BLOB_FIXED + nameLen > inLen) return APP_STATUS_BAD_PARAM;
    setNameIn(expo->name, (uint8_t)sizeof(expo->name), &in[EXPO_BLOB_FIXED], nameLen);
  }

  return APP_STATUS_OK;
}

// Mirrors the UI implementation in gui/*/model_inputs.cpp
static void expoInsertLine(uint8_t idx, uint8_t input)
{
  mixerTaskStop();
  ExpoData* expo = expoAddress(idx);
  memmove(expo + 1, expo, (MAX_EXPOS - (idx + 1)) * sizeof(ExpoData));
  memset(expo, 0, sizeof(ExpoData));
  if (input >= adcGetMaxInputs(ADC_INPUT_MAIN)) {
    expo->srcRaw = MIXSRC_FIRST_STICK + input;
  } else {
    expo->srcRaw = MIXSRC_FIRST_STICK + inputMappingChannelOrder(input);
  }
  expo->curve.type = CURVE_REF_EXPO;
  expo->mode = 3;  // pos+neg
  expo->chn = input;
  expo->weight = 100;
  mixerTaskStart();
  storageDirty(EE_MODEL);
}

static void expoDeleteLine(uint8_t idx)
{
  mixerTaskStop();
  ExpoData* expo = expoAddress(idx);
  uint8_t input = expo->chn;
  memmove(expo, expo + 1, (MAX_EXPOS - (idx + 1)) * sizeof(ExpoData));
  memset(&g_model.expoData[MAX_EXPOS - 1], 0, sizeof(ExpoData));
  mixerTaskStart();
  if (input < MAX_INPUTS && !inputIsUsed(input)) {
    memset(g_model.inputNames[input], 0, sizeof(g_model.inputNames[input]));
  }
  storageDirty(EE_MODEL);
}

// Returns the new line index within the input, or `idx` if it did not move.
static uint8_t expoMoveLine(uint8_t idx, bool up)
{
  ExpoData* x = expoAddress(idx);
  int8_t tgt = up ? (int8_t)idx - 1 : (int8_t)idx + 1;

  mixerTaskLock();

  if (tgt < 0) {
    if (x->chn == 0) { mixerTaskUnlock(); return idx; }
    x->chn--;
    mixerTaskUnlock();
    storageDirty(EE_MODEL);
    return idx;
  }

  if (tgt >= MAX_EXPOS) {
    if (x->chn >= MAX_INPUTS - 1) { mixerTaskUnlock(); return idx; }
    x->chn++;
    mixerTaskUnlock();
    storageDirty(EE_MODEL);
    return idx;
  }

  ExpoData* y = expoAddress(tgt);
  if (x->chn != y->chn || !EXPO_VALID(y)) {
    if (up) {
      if (x->chn == 0) { mixerTaskUnlock(); return idx; }
      x->chn--;
    } else {
      if (x->chn >= MAX_INPUTS - 1) { mixerTaskUnlock(); return idx; }
      x->chn++;
    }
    mixerTaskUnlock();
    storageDirty(EE_MODEL);
    return idx;
  }

  ExpoData tmp;
  memcpy(&tmp, x, sizeof(ExpoData));
  memcpy(x, y, sizeof(ExpoData));
  memcpy(y, &tmp, sizeof(ExpoData));
  mixerTaskUnlock();

  storageDirty(EE_MODEL);
  return tgt;
}

// ---------------------------------------------------------------------------
// Custom function helpers
// ---------------------------------------------------------------------------
static bool cfnUsesName(uint8_t func)
{
  return func == FUNC_PLAY_TRACK || func == FUNC_BACKGND_MUSIC ||
         func == FUNC_PLAY_SCRIPT || func == FUNC_RGB_LED;
}

// ---------------------------------------------------------------------------
// Command handlers
// ---------------------------------------------------------------------------
static uint8_t cmdGetInfo(uint8_t* out, uint8_t* outLen)
{
  out[0] = APP_PROTO_VERSION;
  out[1] = VERSION_MAJOR;
  out[2] = VERSION_MINOR;
  out[3] = VERSION_REVISION;
  out[4] = (uint8_t)MAX_OUTPUT_CHANNELS;
  out[5] = (uint8_t)MAX_MIXERS;
  out[6] = (uint8_t)MAX_FLIGHT_MODES;
  out[7] = (uint8_t)MAX_TIMERS;
  out[8] = (uint8_t)NUM_MODULES;
  out[9] = copyNameOut(&out[10], g_model.header.name,
                       (uint8_t)sizeof(g_model.header.name));
  *outLen = 10 + out[9];
  return APP_STATUS_OK;
}

static uint8_t cmdMixGet(const uint8_t* in, uint8_t inLen, uint8_t* out, uint8_t* outLen)
{
  if (inLen < 2) return APP_STATUS_BAD_PARAM;
  uint8_t ch = in[0];
  uint8_t line = in[1];
  if (ch >= MAX_OUTPUT_CHANNELS) return APP_STATUS_RANGE;
  if (line >= mixLineCount(ch)) return APP_STATUS_RANGE;

  return readMixBlob(mixAddress(mixFirstLine(ch) + line), out, outLen);
}

static uint8_t cmdMixInsert(const uint8_t* in, uint8_t inLen, uint8_t* out, uint8_t* outLen)
{
  if (!modelEditable()) return APP_STATUS_NOT_READY;
  if (inLen < 2) return APP_STATUS_BAD_PARAM;

  uint8_t ch = in[0];
  uint8_t line = in[1];
  if (ch >= MAX_OUTPUT_CHANNELS) return APP_STATUS_RANGE;

  uint8_t count = mixLineCount(ch);
  if (line > count) return APP_STATUS_RANGE;
  if (getMixCount() >= MAX_MIXERS) return APP_STATUS_RANGE;

  uint8_t idx = mixFirstLine(ch) + line;
  insertMix(idx, ch);

  uint8_t status = writeMixBlob(mixAddress(idx), &in[2], (uint8_t)(inLen - 2));
  if (status != APP_STATUS_OK) {
    // do not leave a half initialised line behind
    deleteMix(idx);
    return status;
  }

  storageDirty(EE_MODEL);
  out[0] = idx;
  *outLen = 1;
  return APP_STATUS_OK;
}

static uint8_t cmdMixSet(const uint8_t* in, uint8_t inLen, uint8_t* out, uint8_t* outLen)
{
  if (!modelEditable()) return APP_STATUS_NOT_READY;
  if (inLen < 2) return APP_STATUS_BAD_PARAM;

  uint8_t ch = in[0];
  uint8_t line = in[1];
  if (ch >= MAX_OUTPUT_CHANNELS) return APP_STATUS_RANGE;
  if (line >= mixLineCount(ch)) return APP_STATUS_RANGE;

  MixData* mix = mixAddress(mixFirstLine(ch) + line);

  // insertMix()/deleteMix() lock the mixer themselves, but an in-place update
  // needs to be protected against a concurrent mixer calculation.
  mixerTaskLock();
  uint8_t status = writeMixBlob(mix, &in[2], (uint8_t)(inLen - 2));
  mixerTaskUnlock();

  if (status != APP_STATUS_OK) return status;

  storageDirty(EE_MODEL);
  return APP_STATUS_OK;
}

static uint8_t cmdMixDelete(const uint8_t* in, uint8_t inLen, uint8_t* out, uint8_t* outLen)
{
  if (!modelEditable()) return APP_STATUS_NOT_READY;
  if (inLen < 2) return APP_STATUS_BAD_PARAM;

  uint8_t ch = in[0];
  uint8_t line = in[1];
  if (ch >= MAX_OUTPUT_CHANNELS) return APP_STATUS_RANGE;
  if (line >= mixLineCount(ch)) return APP_STATUS_RANGE;

  deleteMix(mixFirstLine(ch) + line);
  return APP_STATUS_OK;
}

static uint8_t cmdMixMove(const uint8_t* in, uint8_t inLen, uint8_t* out, uint8_t* outLen)
{
  if (!modelEditable()) return APP_STATUS_NOT_READY;
  if (inLen < 3) return APP_STATUS_BAD_PARAM;

  uint8_t ch = in[0];
  uint8_t line = in[1];
  if (ch >= MAX_OUTPUT_CHANNELS) return APP_STATUS_RANGE;
  if (line >= mixLineCount(ch)) return APP_STATUS_RANGE;

  uint8_t newIdx = moveMix(mixFirstLine(ch) + line, in[2] == 0);
  out[0] = newIdx - mixFirstLine(ch);
  *outLen = 1;
  return APP_STATUS_OK;
}

static uint8_t cmdOutputGet(const uint8_t* in, uint8_t inLen, uint8_t* out, uint8_t* outLen)
{
  if (inLen < 1) return APP_STATUS_BAD_PARAM;
  uint8_t ch = in[0];
  if (ch >= MAX_OUTPUT_CHANNELS) return APP_STATUS_RANGE;

  const LimitData* lim = &g_model.limitData[ch];
  putU16(&out[0], (uint16_t)(lim->min - 1000));
  putU16(&out[2], (uint16_t)(lim->max + 1000));
  putU16(&out[4], (uint16_t)lim->offset);
  putU16(&out[6], (uint16_t)lim->ppmCenter);
  out[8] = lim->symetrical;
  out[9] = lim->revert;
  out[10] = (uint8_t)lim->curve;
  out[11] = copyNameOut(&out[12], lim->name, (uint8_t)sizeof(lim->name));
  *outLen = 12 + out[11];
  return APP_STATUS_OK;
}

static uint8_t cmdOutputSet(const uint8_t* in, uint8_t inLen, uint8_t* out, uint8_t* outLen)
{
  if (inLen < 11) return APP_STATUS_BAD_PARAM;
  uint8_t ch = in[0];
  if (ch >= MAX_OUTPUT_CHANNELS) return APP_STATUS_RANGE;

  int16_t min = (int16_t)getU16(&in[1]);
  int16_t max = (int16_t)getU16(&in[3]);
  if (min > 0 || max < 0) return APP_STATUS_RANGE;

  LimitData* lim = &g_model.limitData[ch];
  mixerTaskLock();
  lim->min = limit<int32_t>(-1024, min + 1000, 1023);
  lim->max = limit<int32_t>(-1024, max - 1000, 1023);
  lim->offset = (int16_t)getU16(&in[5]);
  lim->ppmCenter = (int16_t)getU16(&in[7]);
  lim->symetrical = in[9] ? 1 : 0;
  lim->revert = in[10] ? 1 : 0;
  mixerTaskUnlock();

  storageDirty(EE_MODEL);
  return APP_STATUS_OK;
}

static uint8_t cmdOutputSetName(const uint8_t* in, uint8_t inLen, uint8_t* out, uint8_t* outLen)
{
  if (inLen < 2) return APP_STATUS_BAD_PARAM;
  uint8_t ch = in[0];
  if (ch >= MAX_OUTPUT_CHANNELS) return APP_STATUS_RANGE;

  uint8_t nameLen = in[1];
  if ((uint16_t)2 + nameLen > inLen) return APP_STATUS_BAD_PARAM;

  LimitData* lim = &g_model.limitData[ch];
  setNameIn(lim->name, (uint8_t)sizeof(lim->name), &in[2], nameLen);

  storageDirty(EE_MODEL);
  return APP_STATUS_OK;
}

static uint8_t cmdModelGetName(uint8_t* out, uint8_t* outLen)
{
  out[0] = copyNameOut(&out[1], g_model.header.name,
                       (uint8_t)sizeof(g_model.header.name));
  *outLen = 1 + out[0];
  return APP_STATUS_OK;
}

static uint8_t cmdModelSetName(const uint8_t* in, uint8_t inLen, uint8_t* out, uint8_t* outLen)
{
  if (inLen < 1) return APP_STATUS_BAD_PARAM;
  uint8_t nameLen = in[0];
  if ((uint16_t)1 + nameLen > inLen) return APP_STATUS_BAD_PARAM;

  setNameIn(g_model.header.name, (uint8_t)sizeof(g_model.header.name), &in[1], nameLen);
  storageDirty(EE_MODEL);
  return APP_STATUS_OK;
}

static uint8_t cmdTimerGet(const uint8_t* in, uint8_t inLen, uint8_t* out, uint8_t* outLen)
{
  if (inLen < 1) return APP_STATUS_BAD_PARAM;
  uint8_t idx = in[0];
  if (idx >= MAX_TIMERS) return APP_STATUS_RANGE;

  const TimerData& timer = g_model.timers[idx];
  out[0] = (uint8_t)timer.mode;
  putU32(&out[1], timer.start);
  putU32(&out[5], (uint32_t)timer.value);
  out[9] = (uint8_t)timer.countdownStart;
  out[10] = (uint8_t)timer.countdownBeep;
  out[11] = timer.minuteBeep ? 1 : 0;
  out[12] = (uint8_t)timer.persistent;
  out[13] = timer.showElapsed ? 1 : 0;
  putU16(&out[14], (uint16_t)(int16_t)timer.swtch);
  out[16] = copyNameOut(&out[17], timer.name, (uint8_t)sizeof(timer.name));
  *outLen = 17 + out[16];
  return APP_STATUS_OK;
}

static uint8_t cmdTimerSet(const uint8_t* in, uint8_t inLen, uint8_t* out, uint8_t* outLen)
{
  if (inLen < 17) return APP_STATUS_BAD_PARAM;
  uint8_t idx = in[0];
  if (idx >= MAX_TIMERS) return APP_STATUS_RANGE;

  TimerData& timer = g_model.timers[idx];
  uint8_t mode = in[1];
  if (mode > 3) return APP_STATUS_RANGE;

  mixerTaskLock();
  timer.mode = mode;
  timer.start = getU32(&in[2]) & 0x3FFFFF;
  timer.value = (int32_t)getU32(&in[6]);
  timer.countdownStart = in[10] & 0x03;
  timer.countdownBeep = in[11] & 0x03;
  timer.minuteBeep = in[12] ? 1 : 0;
  timer.persistent = in[13] & 0x03;
  timer.showElapsed = in[14] ? 1 : 0;
  timer.swtch = (int16_t)getU16(&in[15]);
  mixerTaskUnlock();

  if (inLen > 17) {
    uint8_t nameLen = in[17];
    if (nameLen) {
      if ((uint16_t)18 + nameLen > inLen) return APP_STATUS_BAD_PARAM;
      setNameIn(timer.name, (uint8_t)sizeof(timer.name), &in[18], nameLen);
    }
  }

  storageDirty(EE_MODEL);
  return APP_STATUS_OK;
}

static uint8_t cmdGeneralGet(const uint8_t* in, uint8_t inLen, uint8_t* out, uint8_t* outLen)
{
  if (inLen < 1) return APP_STATUS_BAD_PARAM;

  int32_t value = 0;
  switch (in[0]) {
  case GEN_ID_VOLUME:
    value = g_eeGeneral.speakerVolume + VOLUME_LEVEL_DEF;
    break;
  case GEN_ID_BEEP_VOLUME:
    value = g_eeGeneral.beepVolume + 2;
    break;
  case GEN_ID_WAV_VOLUME:
    value = g_eeGeneral.wavVolume + 2;
    break;
  case GEN_ID_VARIO_VOLUME:
    value = g_eeGeneral.varioVolume + 2;
    break;
  case GEN_ID_BACKGROUND_VOLUME:
    value = g_eeGeneral.backgroundVolume + 2;
    break;
  case GEN_ID_HAPTIC_STRENGTH:
    value = g_eeGeneral.hapticStrength + 2;
    break;
  case GEN_ID_HAPTIC_LENGTH:
    value = g_eeGeneral.hapticLength + 2;
    break;
  case GEN_ID_BEEP_LENGTH:
    value = g_eeGeneral.beepLength + 2;
    break;
  case GEN_ID_BACKLIGHT:
    value = g_eeGeneral.backlightBright;
    break;
  default:
    return APP_STATUS_BAD_PARAM;
  }

  putU32(out, (uint32_t)value);
  *outLen = 4;
  return APP_STATUS_OK;
}

static uint8_t cmdGeneralSet(const uint8_t* in, uint8_t inLen, uint8_t* out, uint8_t* outLen)
{
  if (inLen < 5) return APP_STATUS_BAD_PARAM;
  int32_t value = (int32_t)getU32(&in[1]);

  switch (in[0]) {
  case GEN_ID_VOLUME:
    if (value < 0 || value > VOLUME_LEVEL_MAX) return APP_STATUS_RANGE;
    g_eeGeneral.speakerVolume = (int8_t)(value - VOLUME_LEVEL_DEF);
    break;
  case GEN_ID_BEEP_VOLUME:
    if (value < 0 || value > 4) return APP_STATUS_RANGE;
    g_eeGeneral.beepVolume = (int8_t)(value - 2);
    break;
  case GEN_ID_WAV_VOLUME:
    if (value < 0 || value > 4) return APP_STATUS_RANGE;
    g_eeGeneral.wavVolume = (int8_t)(value - 2);
    break;
  case GEN_ID_VARIO_VOLUME:
    if (value < 0 || value > 4) return APP_STATUS_RANGE;
    g_eeGeneral.varioVolume = (int8_t)(value - 2);
    break;
  case GEN_ID_BACKGROUND_VOLUME:
    if (value < 0 || value > 4) return APP_STATUS_RANGE;
    g_eeGeneral.backgroundVolume = (int8_t)(value - 2);
    break;
  case GEN_ID_HAPTIC_STRENGTH:
    if (value < 0 || value > 4) return APP_STATUS_RANGE;
    g_eeGeneral.hapticStrength = (int8_t)(value - 2);
    break;
  case GEN_ID_HAPTIC_LENGTH:
    if (value < 0 || value > 4) return APP_STATUS_RANGE;
    g_eeGeneral.hapticLength = (int8_t)(value - 2);
    break;
  case GEN_ID_BEEP_LENGTH:
    if (value < 0 || value > 4) return APP_STATUS_RANGE;
    g_eeGeneral.beepLength = (int8_t)(value - 2);
    break;
  case GEN_ID_BACKLIGHT:
    if (value < BACKLIGHT_LEVEL_MIN || value > BACKLIGHT_LEVEL_MAX) return APP_STATUS_RANGE;
    g_eeGeneral.backlightBright = (uint8_t)value;
    break;
  default:
    return APP_STATUS_BAD_PARAM;
  }

  storageDirty(EE_GENERAL);
  return APP_STATUS_OK;
}

static bool rfPowerGet(uint8_t module, uint8_t* type, int16_t* power)
{
  if (module >= NUM_MODULES) return false;
  const ModuleData& mod = g_model.moduleData[module];
  *type = mod.type;

  switch (mod.type) {
  case MODULE_TYPE_MULTIMODULE:
    *power = mod.multi.lowPowerMode ? 1 : 0;
    return true;
  case MODULE_TYPE_XJT_PXX1:
  case MODULE_TYPE_ISRM_PXX2:
  case MODULE_TYPE_R9M_PXX1:
  case MODULE_TYPE_R9M_PXX2:
  case MODULE_TYPE_R9M_LITE_PXX1:
  case MODULE_TYPE_R9M_LITE_PXX2:
  case MODULE_TYPE_R9M_LITE_PRO_PXX2:
  case MODULE_TYPE_XJT_LITE_PXX2:
    *power = mod.pxx.power;
    return true;
  case MODULE_TYPE_FLYSKY_AFHDS3:
    *power = mod.afhds3.rfPower;
    return true;
  default:
    return false;
  }
}

static uint8_t cmdRfPowerGet(const uint8_t* in, uint8_t inLen, uint8_t* out, uint8_t* outLen)
{
  if (inLen < 1) return APP_STATUS_BAD_PARAM;

  uint8_t type = 0;
  int16_t power = 0;
  if (!rfPowerGet(in[0], &type, &power)) return APP_STATUS_UNSUPPORTED;

  out[0] = type;
  out[1] = (uint8_t)power;
  *outLen = 2;
  return APP_STATUS_OK;
}

static uint8_t cmdRfPowerSet(const uint8_t* in, uint8_t inLen, uint8_t* out, uint8_t* outLen)
{
  if (inLen < 2) return APP_STATUS_BAD_PARAM;
  uint8_t module = in[0];
  uint8_t power = in[1];
  if (module >= NUM_MODULES) return APP_STATUS_RANGE;

  ModuleData& mod = g_model.moduleData[module];
  switch (mod.type) {
  case MODULE_TYPE_MULTIMODULE:
    if (power > 1) return APP_STATUS_RANGE;
    mod.multi.lowPowerMode = power;
    break;
  case MODULE_TYPE_XJT_PXX1:
  case MODULE_TYPE_ISRM_PXX2:
  case MODULE_TYPE_R9M_PXX1:
  case MODULE_TYPE_R9M_PXX2:
  case MODULE_TYPE_R9M_LITE_PXX1:
  case MODULE_TYPE_R9M_LITE_PXX2:
  case MODULE_TYPE_R9M_LITE_PRO_PXX2:
  case MODULE_TYPE_XJT_LITE_PXX2:
    if (power > 3) return APP_STATUS_RANGE;
    mod.pxx.power = power;
    break;
  case MODULE_TYPE_FLYSKY_AFHDS3:
    mod.afhds3.rfPower = power;
    break;
  default:
    return APP_STATUS_UNSUPPORTED;
  }

  storageDirty(EE_MODEL);
  return APP_STATUS_OK;
}

static uint8_t cmdFmGet(const uint8_t* in, uint8_t inLen, uint8_t* out, uint8_t* outLen)
{
  if (inLen < 1) return APP_STATUS_BAD_PARAM;
  uint8_t idx = in[0];
  if (idx >= MAX_FLIGHT_MODES) return APP_STATUS_RANGE;

  const FlightModeData* fm = &g_model.flightModeData[idx];
  uint8_t pos = 0;
  putU16(&out[pos], (uint16_t)(int16_t)fm->swtch);
  pos += 2;
  out[pos++] = fm->fadeIn;
  out[pos++] = fm->fadeOut;
  uint8_t nameLen = copyNameOut(&out[pos + 1], fm->name, (uint8_t)sizeof(fm->name));
  out[pos] = nameLen;
  pos += 1 + nameLen;

  // trim values / modes are only meaningful for the flight modes above FM0
  if (idx > 0) {
    out[pos++] = MAX_TRIMS;
    for (uint8_t t = 0; t < MAX_TRIMS; t++) {
      putU16(&out[pos], (uint16_t)(int16_t)fm->trim[t].value);
      pos += 2;
    }
    for (uint8_t t = 0; t < MAX_TRIMS; t++) {
      out[pos++] = (uint8_t)fm->trim[t].mode;
    }
  } else {
    out[pos++] = 0;
  }

  *outLen = pos;
  return APP_STATUS_OK;
}

static uint8_t cmdFmSet(const uint8_t* in, uint8_t inLen, uint8_t* out, uint8_t* outLen)
{
  if (inLen < 5) return APP_STATUS_BAD_PARAM;
  uint8_t idx = in[0];
  if (idx >= MAX_FLIGHT_MODES) return APP_STATUS_RANGE;

  FlightModeData* fm = &g_model.flightModeData[idx];
  uint16_t swtch = getU16(&in[1]);
  // FM0 is always active
  if (idx == 0 && swtch != 0) return APP_STATUS_RANGE;

  mixerTaskLock();
  fm->swtch = (int16_t)swtch;
  fm->fadeIn = in[3];
  fm->fadeOut = in[4];
  mixerTaskUnlock();

  if (inLen > 5) {
    uint8_t nameLen = in[5];
    if (nameLen) {
      if ((uint16_t)6 + nameLen > inLen) return APP_STATUS_BAD_PARAM;
      setNameIn(fm->name, (uint8_t)sizeof(fm->name), &in[6], nameLen);
    }
  }

  storageDirty(EE_MODEL);
  return APP_STATUS_OK;
}

static uint8_t cmdFmSetTrim(const uint8_t* in, uint8_t inLen, uint8_t* out, uint8_t* outLen)
{
  if (inLen < 5) return APP_STATUS_BAD_PARAM;
  uint8_t idx = in[0];
  uint8_t trim = in[1];
  if (idx >= MAX_FLIGHT_MODES || idx == 0) return APP_STATUS_RANGE;
  if (trim >= MAX_TRIMS) return APP_STATUS_RANGE;

  FlightModeData* fm = &g_model.flightModeData[idx];
  mixerTaskLock();
  fm->trim[trim].value = (int16_t)getU16(&in[2]);
  fm->trim[trim].mode = in[4] & 0x1F;
  mixerTaskUnlock();

  storageDirty(EE_MODEL);
  return APP_STATUS_OK;
}

// ---------------------------------------------------------------------------
// Inputs (expo)
// ---------------------------------------------------------------------------
static uint8_t cmdInputCount(const uint8_t* in, uint8_t inLen, uint8_t* out, uint8_t* outLen)
{
  if (inLen < 1) return APP_STATUS_BAD_PARAM;
  if (in[0] >= MAX_INPUTS) return APP_STATUS_RANGE;
  out[0] = inputLineCount(in[0]);
  *outLen = 1;
  return APP_STATUS_OK;
}

static uint8_t cmdInputGet(const uint8_t* in, uint8_t inLen, uint8_t* out, uint8_t* outLen)
{
  if (inLen < 2) return APP_STATUS_BAD_PARAM;
  uint8_t ch = in[0];
  uint8_t line = in[1];
  if (ch >= MAX_INPUTS) return APP_STATUS_RANGE;
  if (line >= inputLineCount(ch)) return APP_STATUS_RANGE;

  return readExpoBlob(expoAddress(inputFirstLine(ch) + line), out, outLen);
}

static uint8_t cmdInputInsert(const uint8_t* in, uint8_t inLen, uint8_t* out, uint8_t* outLen)
{
  if (!modelEditable()) return APP_STATUS_NOT_READY;
  if (inLen < 2) return APP_STATUS_BAD_PARAM;

  uint8_t ch = in[0];
  uint8_t line = in[1];
  if (ch >= MAX_INPUTS) return APP_STATUS_RANGE;
  if (line > inputLineCount(ch)) return APP_STATUS_RANGE;
  if (expoCount() >= MAX_EXPOS) return APP_STATUS_RANGE;

  uint8_t idx = inputFirstLine(ch) + line;

  // mirrors the UI: shift the trailing lines and initialise a default line
  expoInsertLine(idx, ch);

  uint8_t status = writeExpoBlob(expoAddress(idx), &in[2], (uint8_t)(inLen - 2));
  if (status != APP_STATUS_OK) {
    expoDeleteLine(idx);
    return status;
  }

  storageDirty(EE_MODEL);
  out[0] = idx;
  *outLen = 1;
  return APP_STATUS_OK;
}

static uint8_t cmdInputSet(const uint8_t* in, uint8_t inLen, uint8_t* out, uint8_t* outLen)
{
  if (!modelEditable()) return APP_STATUS_NOT_READY;
  if (inLen < 2) return APP_STATUS_BAD_PARAM;

  uint8_t ch = in[0];
  uint8_t line = in[1];
  if (ch >= MAX_INPUTS) return APP_STATUS_RANGE;
  if (line >= inputLineCount(ch)) return APP_STATUS_RANGE;

  ExpoData* expo = expoAddress(inputFirstLine(ch) + line);

  mixerTaskLock();
  uint8_t status = writeExpoBlob(expo, &in[2], (uint8_t)(inLen - 2));
  mixerTaskUnlock();

  if (status != APP_STATUS_OK) return status;
  storageDirty(EE_MODEL);
  return APP_STATUS_OK;
}

static uint8_t cmdInputDelete(const uint8_t* in, uint8_t inLen, uint8_t* out, uint8_t* outLen)
{
  if (!modelEditable()) return APP_STATUS_NOT_READY;
  if (inLen < 2) return APP_STATUS_BAD_PARAM;

  uint8_t ch = in[0];
  uint8_t line = in[1];
  if (ch >= MAX_INPUTS) return APP_STATUS_RANGE;
  if (line >= inputLineCount(ch)) return APP_STATUS_RANGE;

  expoDeleteLine(inputFirstLine(ch) + line);
  return APP_STATUS_OK;
}

static uint8_t cmdInputMove(const uint8_t* in, uint8_t inLen, uint8_t* out, uint8_t* outLen)
{
  if (!modelEditable()) return APP_STATUS_NOT_READY;
  if (inLen < 3) return APP_STATUS_BAD_PARAM;

  uint8_t ch = in[0];
  uint8_t line = in[1];
  if (ch >= MAX_INPUTS) return APP_STATUS_RANGE;
  if (line >= inputLineCount(ch)) return APP_STATUS_RANGE;

  uint8_t newIdx = expoMoveLine(inputFirstLine(ch) + line, in[2] == 0);
  out[0] = newIdx - inputFirstLine(ch);
  *outLen = 1;
  return APP_STATUS_OK;
}

static uint8_t cmdInputGetName(const uint8_t* in, uint8_t inLen, uint8_t* out, uint8_t* outLen)
{
  if (inLen < 1) return APP_STATUS_BAD_PARAM;
  uint8_t ch = in[0];
  if (ch >= MAX_INPUTS) return APP_STATUS_RANGE;

  out[0] = copyNameOut(&out[1], g_model.inputNames[ch],
                       (uint8_t)sizeof(g_model.inputNames[ch]));
  *outLen = 1 + out[0];
  return APP_STATUS_OK;
}

static uint8_t cmdInputSetName(const uint8_t* in, uint8_t inLen, uint8_t* out, uint8_t* outLen)
{
  if (inLen < 2) return APP_STATUS_BAD_PARAM;
  uint8_t ch = in[0];
  if (ch >= MAX_INPUTS) return APP_STATUS_RANGE;

  uint8_t nameLen = in[1];
  if ((uint16_t)2 + nameLen > inLen) return APP_STATUS_BAD_PARAM;

  setNameIn(g_model.inputNames[ch], (uint8_t)sizeof(g_model.inputNames[ch]),
            &in[2], nameLen);
  storageDirty(EE_MODEL);
  return APP_STATUS_OK;
}

// ---------------------------------------------------------------------------
// Curves
// ---------------------------------------------------------------------------
static uint8_t cmdCurveGet(const uint8_t* in, uint8_t inLen, uint8_t* out, uint8_t* outLen)
{
  if (inLen < 1) return APP_STATUS_BAD_PARAM;
  uint8_t idx = in[0];
  if (idx >= MAX_CURVES) return APP_STATUS_RANGE;

  const CurveHeader* curve = &g_model.curves[idx];
  uint8_t count = (uint8_t)(curve->points + 5);  // stored Y values
  if (count > MAX_CURVE_EDIT_POINTS) count = MAX_CURVE_EDIT_POINTS;

  uint8_t pos = 0;
  out[pos++] = (uint8_t)curve->type;
  out[pos++] = curve->smooth ? 1 : 0;
  out[pos++] = (uint8_t)(int8_t)curve->points;
  out[pos++] = count;
  out[pos++] = isCurveUsed(idx) ? 1 : 0;
  uint8_t nameLen = copyNameOut(&out[pos + 1], curve->name, (uint8_t)sizeof(curve->name));
  out[pos] = nameLen;
  pos += 1 + nameLen;

  const int8_t* points = curveAddress(idx);
  for (uint8_t i = 0; i < count; i++) out[pos++] = (uint8_t)points[i];

  *outLen = pos;
  return APP_STATUS_OK;
}

static uint8_t cmdCurveSet(const uint8_t* in, uint8_t inLen, uint8_t* out, uint8_t* outLen)
{
  if (!modelEditable()) return APP_STATUS_NOT_READY;
  if (inLen < 5) return APP_STATUS_BAD_PARAM;

  uint8_t idx = in[0];
  if (idx >= MAX_CURVES) return APP_STATUS_RANGE;

  uint8_t newType = in[1];
  if (newType > CURVE_TYPE_LAST) return APP_STATUS_RANGE;
  uint8_t smooth = in[2] ? 1 : 0;

  uint8_t count = in[3];
  if (count < 2 || count > MAX_CURVE_EDIT_POINTS) return APP_STATUS_RANGE;

  uint8_t nameLen = in[4];
  if ((uint16_t)5 + nameLen > inLen) return APP_STATUS_BAD_PARAM;

  bool hasValues = ((uint16_t)5 + nameLen + count) <= inLen;
  const uint8_t* values = &in[5 + nameLen];

  CurveHeader& curve = g_model.curves[idx];
  int8_t* points = curveAddress(idx);

  uint8_t oldCount = (uint8_t)(curve.points + 5);
  uint8_t newPoints = count - 5;
  bool resized = (newType != curve.type) || (count != oldCount);

  if (resized && oldCount > 0) {
    // resample the current shape onto the new point count, like the UI does
    int8_t newY[MAX_CURVE_EDIT_POINTS];
    newY[0] = points[0];
    newY[count - 1] = points[oldCount - 1];
    for (uint8_t i = 1; i + 1 < count; i++) {
      newY[i] = (int8_t)calcRESXto100(
          applyCustomCurve(-RESX + (int)(i * 2 * RESX) / (count - 1), idx));
    }

    int oldSize = getCurvePoints(idx);
    int newSize = (newType == CURVE_TYPE_CUSTOM) ? (2 * newPoints + 8)
                                                 : (newPoints + 5);
    int delta = newSize - oldSize;

    if (delta > 127 || delta < -128) return APP_STATUS_ERROR;
    if (delta && !moveCurve(idx, (int8_t)delta)) return APP_STATUS_RANGE;

    for (uint8_t i = 0; i < count; i++) {
      points[i] = hasValues ? (int8_t)values[i] : newY[i];
    }
    if (newType == CURVE_TYPE_CUSTOM) resetCustomCurveX(points, count);
  } else if (hasValues) {
    for (uint8_t i = 0; i < count; i++) points[i] = (int8_t)values[i];
  }

  curve.type = newType;
  curve.smooth = smooth;
  curve.points = newPoints;
  setNameIn(curve.name, (uint8_t)sizeof(curve.name), &in[5], nameLen);

  storageDirty(EE_MODEL);
  return APP_STATUS_OK;
}

static uint8_t cmdCurveSetPoint(const uint8_t* in, uint8_t inLen, uint8_t* out, uint8_t* outLen)
{
  if (!modelEditable()) return APP_STATUS_NOT_READY;
  if (inLen < 4) return APP_STATUS_BAD_PARAM;

  uint8_t idx = in[0];
  if (idx >= MAX_CURVES) return APP_STATUS_RANGE;

  const CurveHeader* curve = &g_model.curves[idx];
  uint8_t count = (uint8_t)(curve->points + 5);
  uint8_t point = in[1];
  if (point >= count) return APP_STATUS_RANGE;

  int16_t value = (int16_t)getU16(&in[2]);
  if (value < -100) value = -100;
  if (value > 100) value = 100;

  mixerTaskLock();
  curveAddress(idx)[point] = (int8_t)value;
  mixerTaskUnlock();

  storageDirty(EE_MODEL);
  return APP_STATUS_OK;
}

static uint8_t cmdCurveClear(const uint8_t* in, uint8_t inLen, uint8_t* out, uint8_t* outLen)
{
  if (!modelEditable()) return APP_STATUS_NOT_READY;
  if (inLen < 1) return APP_STATUS_BAD_PARAM;
  if (in[0] >= MAX_CURVES) return APP_STATUS_RANGE;

  curveClear(in[0]);
  storageDirty(EE_MODEL);
  return APP_STATUS_OK;
}

static uint8_t cmdCurveMirror(const uint8_t* in, uint8_t inLen, uint8_t* out, uint8_t* outLen)
{
  if (!modelEditable()) return APP_STATUS_NOT_READY;
  if (inLen < 1) return APP_STATUS_BAD_PARAM;
  if (in[0] >= MAX_CURVES) return APP_STATUS_RANGE;

  curveMirror(in[0]);
  storageDirty(EE_MODEL);
  return APP_STATUS_OK;
}

// ---------------------------------------------------------------------------
// Logical switches
// ---------------------------------------------------------------------------
static uint8_t cmdLsGet(const uint8_t* in, uint8_t inLen, uint8_t* out, uint8_t* outLen)
{
  if (inLen < 1) return APP_STATUS_BAD_PARAM;
  uint8_t idx = in[0];
  if (idx >= MAX_LOGICAL_SWITCHES) return APP_STATUS_RANGE;

  const LogicalSwitchData* ls = &g_model.logicalSw[idx];
  out[0] = ls->func;
  putU16(&out[1], (uint16_t)(int16_t)ls->v1);
  putU16(&out[3], (uint16_t)(int16_t)ls->v2);
  putU16(&out[5], (uint16_t)(int16_t)ls->v3);
  putU16(&out[7], (uint16_t)(int16_t)ls->andsw);
  out[9] = ls->delay;
  out[10] = ls->duration;
  out[11] = ls->lsPersist ? 1 : 0;
  out[12] = lswFamily(ls->func);
  *outLen = 13;
  return APP_STATUS_OK;
}

static uint8_t cmdLsSet(const uint8_t* in, uint8_t inLen, uint8_t* out, uint8_t* outLen)
{
  if (!modelEditable()) return APP_STATUS_NOT_READY;
  if (inLen < 13) return APP_STATUS_BAD_PARAM;

  uint8_t idx = in[0];
  if (idx >= MAX_LOGICAL_SWITCHES) return APP_STATUS_RANGE;

  uint8_t func = in[1];
  if (func >= LS_FUNC_COUNT) return APP_STATUS_RANGE;

  LogicalSwitchData* ls = &g_model.logicalSw[idx];
  mixerTaskLock();
  ls->func = func;
  ls->v1 = (int16_t)getU16(&in[2]);
  ls->v2 = (int16_t)getU16(&in[4]);
  ls->v3 = (int16_t)getU16(&in[6]);
  ls->andsw = (int16_t)getU16(&in[8]);
  ls->delay = in[10];
  ls->duration = in[11];
  ls->lsPersist = in[12] & 0x01;
  mixerTaskUnlock();

  storageDirty(EE_MODEL);
  return APP_STATUS_OK;
}

// ---------------------------------------------------------------------------
// Special functions
// ---------------------------------------------------------------------------
static uint8_t cmdCfGet(const uint8_t* in, uint8_t inLen, uint8_t* out, uint8_t* outLen)
{
  if (inLen < 1) return APP_STATUS_BAD_PARAM;
  uint8_t idx = in[0];
  if (idx >= MAX_SPECIAL_FUNCTIONS) return APP_STATUS_RANGE;

  const CustomFunctionData* cfn = &g_model.customFn[idx];
  uint8_t func = CFN_FUNC(cfn);
  bool useName = cfnUsesName(func);

  putU16(&out[0], (uint16_t)(int16_t)CFN_SWITCH(cfn));
  out[2] = func;
  out[3] = CFN_ACTIVE(cfn) ? 1 : 0;
  out[4] = (uint8_t)(int8_t)cfn->repeat;
  // 0 = slot not configured, 1 = name payload, 2 = numeric payload
  out[5] = (CFN_SWITCH(cfn) == 0) ? 0 : (useName ? 1 : 2);

  if (useName) {
    putU16(&out[6], 0);
    out[8] = 0;
    out[9] = 0;
    putU32(&out[10], 0);
  } else {
    putU16(&out[6], (uint16_t)(int16_t)cfn->all.val);
    out[8] = cfn->all.mode;
    out[9] = cfn->all.param;
    putU32(&out[10], (uint32_t)cfn->all.val2);
  }

  uint8_t nameLen = useName
      ? copyNameOut(&out[15], cfn->play.name, (uint8_t)sizeof(cfn->play.name))
      : 0;
  out[14] = nameLen;
  *outLen = CF_BLOB_FIXED + nameLen;
  return APP_STATUS_OK;
}

static uint8_t cmdCfSet(const uint8_t* in, uint8_t inLen, uint8_t* out, uint8_t* outLen)
{
  if (!modelEditable()) return APP_STATUS_NOT_READY;
  // index byte + custom function blob
  if (inLen < 1 + CF_BLOB_FIXED) return APP_STATUS_BAD_PARAM;

  uint8_t idx = in[0];
  if (idx >= MAX_SPECIAL_FUNCTIONS) return APP_STATUS_RANGE;

  const uint8_t* blob = &in[1];
  uint8_t func = blob[2];
  if (func > FUNC_MAX) return APP_STATUS_RANGE;

  uint8_t dataKind = blob[5];
  uint8_t nameLen = blob[14];

  if (dataKind == 1 && ((uint16_t)CF_BLOB_FIXED + nameLen > (inLen - 1))) {
    return APP_STATUS_BAD_PARAM;
  }

  CustomFunctionData* cfn = &g_model.customFn[idx];

  mixerTaskLock();
  cfn->swtch = (int16_t)getU16(&blob[0]);
  cfn->func = func;
  cfn->active = blob[3] ? 1 : 0;
  cfn->repeat = (int8_t)blob[4];

  if (dataKind == 1) {
    setNameIn(cfn->play.name, (uint8_t)sizeof(cfn->play.name),
              &blob[CF_BLOB_FIXED], nameLen);
  } else if (dataKind == 2) {
    cfn->all.val = (int16_t)getU16(&blob[6]);
    cfn->all.mode = blob[8];
    cfn->all.param = blob[9];
    cfn->all.val2 = (int32_t)getU32(&blob[10]);
  } else {
    memset(&cfn->all, 0, sizeof(cfn->all));
  }
  mixerTaskUnlock();

  storageDirty(EE_MODEL);
  return APP_STATUS_OK;
}

// ---------------------------------------------------------------------------
// Model list / selection
// ---------------------------------------------------------------------------
static uint8_t cmdModelList(const uint8_t* in, uint8_t inLen, uint8_t* out, uint8_t* outLen)
{
  uint8_t start = inLen >= 1 ? in[0] : 0;

  out[0] = 0;   // total
  out[1] = 0;   // returned
  uint8_t pos = 2;
  uint8_t count = 0;

#if defined(STORAGE_MODELSLIST)
  uint8_t total = modelslist.size() > 255 ? 255 : (uint8_t)modelslist.size();
  ModelCell* current = modelslist.getCurrentModel();

  for (uint8_t i = start; i < total && pos < (APP_MAX_PAYLOAD - 18); i++) {
    ModelCell* cell = modelslist[i];
    uint8_t nameLen = copyNameOut(&out[pos + 2], cell->modelName,
                                  (uint8_t)sizeof(cell->modelName) - 1);
    out[pos] = (cell == current) ? 0x01 : 0x00;
    out[pos + 1] = nameLen;
    pos += 2 + nameLen;
    count++;
  }
  out[0] = total;
#elif !defined(COLORLCD)
  uint8_t total = MAX_MODELS;
  for (uint8_t i = start; i < total && pos < (APP_MAX_PAYLOAD - 18); i++) {
    const ModelHeader* hdr = &modelHeaders[i];
    uint8_t nameLen = 0;
    while (nameLen < sizeof(hdr->name) && hdr->name[nameLen] != '\0') nameLen++;
    if (nameLen == 0) continue;
    out[pos] = (i == (uint8_t)g_eeGeneral.currModel) ? 0x01 : 0x00;
    out[pos + 1] = nameLen;
    memcpy(&out[pos + 2], hdr->name, nameLen);
    pos += 2 + nameLen;
    count++;
  }
  out[0] = total;
#else
  (void)start;
  return APP_STATUS_UNSUPPORTED;
#endif

  out[1] = count;
  *outLen = pos;
  return APP_STATUS_OK;
}

static uint8_t cmdModelSelect(const uint8_t* in, uint8_t inLen, uint8_t* out, uint8_t* outLen)
{
  if (!modelEditable()) return APP_STATUS_NOT_READY;
  if (modelSwitchCountdown) return APP_STATUS_ERROR;
  if (inLen < 1) return APP_STATUS_BAD_PARAM;

  uint8_t idx = in[0];

#if defined(STORAGE_MODELSLIST)
  if (idx >= modelslist.size()) return APP_STATUS_RANGE;
  ModelCell* cell = modelslist[idx];
  if (cell == modelslist.getCurrentModel()) return APP_STATUS_OK;

  strncpy(g_eeGeneral.currModelFilename, cell->modelFilename, LEN_MODEL_FILENAME);
  g_eeGeneral.currModelFilename[LEN_MODEL_FILENAME] = '\0';
#elif !defined(COLORLCD)
  if (idx >= MAX_MODELS) return APP_STATUS_RANGE;
  if (idx == (uint8_t)g_eeGeneral.currModel) return APP_STATUS_OK;
  g_eeGeneral.currModel = idx;
#else
  return APP_STATUS_UNSUPPORTED;
#endif

  // Persist everything, then reboot: loading a model in place would tear down
  // the UI (widgets/screens) which is not safe from a serial command.
  storageDirty(EE_MODEL | EE_GENERAL);
  modelSwitchCountdown = 300;  // 3 s, gives the storage writer time to run

  return APP_STATUS_OK;
}

// ---------------------------------------------------------------------------
// Model management
// ---------------------------------------------------------------------------
#if defined(STORAGE_MODELSLIST)
static ModelCell* modelCellAt(uint8_t idx)
{
  if (idx >= modelslist.size()) return nullptr;
  return modelslist[idx];
}
#endif

static uint8_t cmdModelCreate(const uint8_t* in, uint8_t inLen, uint8_t* out, uint8_t* outLen)
{
  out[0] = 0;
  *outLen = 1;

#if defined(STORAGE_MODELSLIST)
  if (inLen < 1) return APP_STATUS_BAD_PARAM;
  uint8_t nameLen = in[0];
  if ((uint16_t)1 + nameLen > inLen) return APP_STATUS_BAD_PARAM;
  if (nameLen > LEN_MODEL_NAME) nameLen = LEN_MODEL_NAME;

  char filename[LEN_MODEL_FILENAME + 1];
  strncpy(filename, MODEL_FILENAME_PATTERN, LEN_MODEL_FILENAME);
  filename[LEN_MODEL_FILENAME] = '\0';
  if (!findNextFileIndex(filename, LEN_MODEL_FILENAME, MODELS_PATH)) {
    return APP_STATUS_ERROR;
  }

  // Start from the empty model template shipped with the SD card contents.
  // Create it through a file copy so that the currently selected model is
  // left untouched (the firmware's createModel() would switch to the new
  // model and tear down the UI).
  if (sdCopyFile(MODEL_FILENAME_PATTERN, MODELS_PATH,
                 filename, MODELS_PATH)) {
    return APP_STATUS_ERROR;
  }

  ModelCell* cell = modelslist.addModel(filename, true);
  if (!cell) return APP_STATUS_ERROR;

  if (nameLen) {
    char name[LEN_MODEL_NAME + 1];
    memcpy(name, &in[1], nameLen);
    name[nameLen] = '\0';
    cell->setModelName(name);
    modelslabels.setDirty();
  }

  for (uint8_t i = 0; i < modelslist.size(); i++) {
    if (modelslist[i] == cell) { out[0] = i; break; }
  }
  return APP_STATUS_OK;
#else
  return APP_STATUS_UNSUPPORTED;
#endif
}

static uint8_t cmdModelDuplicate(const uint8_t* in, uint8_t inLen, uint8_t* out, uint8_t* outLen)
{
#if defined(STORAGE_MODELSLIST)
  if (inLen < 1) return APP_STATUS_BAD_PARAM;
  ModelCell* cell = modelCellAt(in[0]);
  if (!cell) return APP_STATUS_RANGE;

  // make sure the source on the SD card is up to date
  storageFlushCurrentModel();
  storageCheck(true);

  char filename[LEN_MODEL_FILENAME + 1];
  memcpy(filename, cell->modelFilename, sizeof(filename));
  if (!findNextFileIndex(filename, LEN_MODEL_FILENAME, MODELS_PATH)) {
    return APP_STATUS_ERROR;
  }
  if (sdCopyFile(cell->modelFilename, MODELS_PATH, filename, MODELS_PATH)) {
    return APP_STATUS_ERROR;
  }
#if defined(COLORLCD)
  uiScreensCopy(cell->modelFilename, filename);
#endif
  if (!modelslist.addModel(filename, true, cell)) return APP_STATUS_ERROR;
  return APP_STATUS_OK;
#else
  return APP_STATUS_UNSUPPORTED;
#endif
}

static uint8_t cmdModelDelete(const uint8_t* in, uint8_t inLen, uint8_t* out, uint8_t* outLen)
{
#if defined(STORAGE_MODELSLIST)
  if (inLen < 1) return APP_STATUS_BAD_PARAM;
  ModelCell* cell = modelCellAt(in[0]);
  if (!cell) return APP_STATUS_RANGE;
  // the active model cannot be deleted; select another model first
  if (cell == modelslist.getCurrentModel()) return APP_STATUS_ERROR;
  // removeModel() returns false on success
  if (modelslist.removeModel(cell)) return APP_STATUS_ERROR;
  return APP_STATUS_OK;
#else
  return APP_STATUS_UNSUPPORTED;
#endif
}

static uint8_t cmdModelRename(const uint8_t* in, uint8_t inLen, uint8_t* out, uint8_t* outLen)
{
#if defined(STORAGE_MODELSLIST)
  if (inLen < 2) return APP_STATUS_BAD_PARAM;
  ModelCell* cell = modelCellAt(in[0]);
  if (!cell) return APP_STATUS_RANGE;

  uint8_t nameLen = in[1];
  if ((uint16_t)2 + nameLen > inLen) return APP_STATUS_BAD_PARAM;
  if (nameLen == 0) return APP_STATUS_BAD_PARAM;
  // Names are clipped like they are in the radio UI: LEN_MODEL_NAME depends on
  // the screen size of the target (15 on colour radios, 12 or 10 on the
  // smaller ones), so the host does not need to know it.
  if (nameLen > LEN_MODEL_NAME) nameLen = LEN_MODEL_NAME;

  char name[LEN_MODEL_NAME + 1];
  memcpy(name, &in[2], nameLen);
  name[nameLen] = '\0';

  cell->setModelName(name);
  modelslabels.setDirty();

  if (cell == modelslist.getCurrentModel()) {
    setNameIn(g_model.header.name, (uint8_t)sizeof(g_model.header.name), &in[2], nameLen);
    storageDirty(EE_MODEL);
  }
  return APP_STATUS_OK;
#else
  return APP_STATUS_UNSUPPORTED;
#endif
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------
static uint8_t cmdSubscribe(const uint8_t* in, uint8_t inLen, uint8_t* out, uint8_t* outLen)
{
  if (inLen < 1) return APP_STATUS_BAD_PARAM;
  subscribedEvents = in[0];
  if (!subscribedEvents) appConfigEventMask = 0;
  out[0] = subscribedEvents;
  *outLen = 1;
  return APP_STATUS_OK;
}

// ---------------------------------------------------------------------------
// Generic settings access
// ---------------------------------------------------------------------------
#define PARAM_MAX_PATH      36
#define PARAM_LIST_ENTRIES  6
#define PARAM_GET_CHUNK     44
#define PARAM_EXPORT_CHUNK  44

static uint8_t paramStatus(int st)
{
  switch (st) {
  case ST_OK:            return APP_STATUS_OK;
  case ST_ERR_ROOT:      return APP_STATUS_UNSUPPORTED;
  case ST_ERR_NOT_FOUND: return APP_STATUS_BAD_PARAM;
  case ST_ERR_TYPE:      return APP_STATUS_UNSUPPORTED;
  case ST_ERR_RANGE:     return APP_STATUS_RANGE;
  case ST_ERR_LEN:       return APP_STATUS_BAD_PARAM;
  default:               return APP_STATUS_ERROR;
  }
}

// Copy the path out of the request. Returns the remaining payload or nullptr
// when the request is malformed.
static const uint8_t* paramReadPath(const uint8_t* in, uint8_t inLen, uint8_t minTail,
                                    char* buf, uint8_t* pathLen)
{
  if (inLen < 2) return nullptr;
  uint8_t len = in[1];
  if (len > PARAM_MAX_PATH) return nullptr;
  if ((uint16_t)2 + len + minTail > inLen) return nullptr;

  memcpy(buf, &in[2], len);
  buf[len] = '\0';
  *pathLen = len;
  return &in[2 + len];
}

static uint8_t cmdParamList(const uint8_t* in, uint8_t inLen, uint8_t* out, uint8_t* outLen)
{
  char path[PARAM_MAX_PATH + 1];
  uint8_t pathLen = 0;
  const uint8_t* tail = paramReadPath(in, inLen, 1, path, &pathLen);
  if (!tail) return APP_STATUS_BAD_PARAM;

  uint8_t root = in[0];
  uint8_t startIdx = (uint8_t)tail[0];

  StEntry entries[PARAM_LIST_ENTRIES];
  uint8_t total = 0;
  int st = stList(root, path, pathLen, startIdx, entries, PARAM_LIST_ENTRIES, &total);
  if (st != ST_OK) return paramStatus(st);

  uint8_t count = 0;
  if (total > startIdx) {
    count = (uint8_t)(total - startIdx);
    if (count > PARAM_LIST_ENTRIES) count = PARAM_LIST_ENTRIES;
  }

  out[0] = total;
  out[1] = count;
  uint8_t pos = 2;

  for (uint8_t i = 0; i < count; i++) {
    uint8_t tagLen = (uint8_t)strlen(entries[i].tag);
    if ((uint16_t)pos + 6 + tagLen > APP_MAX_PAYLOAD) {
      out[1] = i;
      break;
    }
    out[pos++] = entries[i].type;
    putU16(&out[pos], entries[i].size);
    pos += 2;
    putU16(&out[pos], entries[i].elmts);
    pos += 2;
    out[pos++] = tagLen;
    memcpy(&out[pos], entries[i].tag, tagLen);
    pos += tagLen;
  }

  *outLen = pos;
  return APP_STATUS_OK;
}

static uint8_t cmdParamGet(const uint8_t* in, uint8_t inLen, uint8_t* out, uint8_t* outLen)
{
  char path[PARAM_MAX_PATH + 1];
  uint8_t pathLen = 0;
  const uint8_t* tail = paramReadPath(in, inLen, 2, path, &pathLen);
  if (!tail) return APP_STATUS_BAD_PARAM;

  uint8_t root = in[0];
  uint16_t offset = getU16(tail);

  if (stPathEndsWithIndex(path, pathLen)) return APP_STATUS_BAD_PARAM;

  char text[PARAM_GET_CHUNK];
  uint16_t total = 0;
  bool more = false;
  int st = stGet(root, path, pathLen, offset, text, sizeof(text), &total, &more);
  if (st != ST_OK) return paramStatus(st);

  uint16_t written = 0;
  if (total > offset) {
    written = (uint16_t)(total - offset);
    if (written > sizeof(text)) written = sizeof(text);
  }

  putU16(&out[0], total);
  out[2] = more ? 1 : 0;
  if (written) memcpy(&out[3], text, written);

  *outLen = (uint8_t)(3 + written);
  return APP_STATUS_OK;
}

static uint8_t cmdParamSet(const uint8_t* in, uint8_t inLen, uint8_t* out, uint8_t* outLen)
{
  char path[PARAM_MAX_PATH + 1];
  uint8_t pathLen = 0;
  const uint8_t* tail = paramReadPath(in, inLen, 1, path, &pathLen);
  if (!tail) return APP_STATUS_BAD_PARAM;

  uint8_t root = in[0];
  if (root == ST_ROOT_MODEL && !modelEditable()) return APP_STATUS_NOT_READY;
  if (stPathEndsWithIndex(path, pathLen)) return APP_STATUS_BAD_PARAM;

  const char* value = (const char*)tail;
  uint16_t valueLen = (uint16_t)(inLen - 2 - pathLen);

  int st;
  if (root == ST_ROOT_MODEL) {
    mixerTaskLock();
    st = stSet(root, path, pathLen, value, valueLen);
    mixerTaskUnlock();
  } else {
    st = stSet(root, path, pathLen, value, valueLen);
  }
  if (st != ST_OK) return paramStatus(st);

  storageDirty(root == ST_ROOT_MODEL ? EE_MODEL : EE_GENERAL);
  return APP_STATUS_OK;
}

static uint8_t cmdParamExport(const uint8_t* in, uint8_t inLen, uint8_t* out, uint8_t* outLen)
{
  if (inLen < 3) return APP_STATUS_BAD_PARAM;

  uint8_t root = in[0];
  uint16_t offset = getU16(&in[1]);

  char text[PARAM_EXPORT_CHUNK];
  uint16_t total = 0;
  bool more = false;
  int st = stExport(root, offset, text, sizeof(text), &total, &more);
  if (st != ST_OK) return paramStatus(st);

  uint16_t written = 0;
  if (total > offset) {
    written = (uint16_t)(total - offset);
    if (written > sizeof(text)) written = sizeof(text);
  }

  putU16(&out[0], total);
  out[2] = more ? 1 : 0;
  if (written) memcpy(&out[3], text, written);

  *outLen = (uint8_t)(3 + written);
  return APP_STATUS_OK;
}

// ---------------------------------------------------------------------------
// Global variable runtime values
// ---------------------------------------------------------------------------
static uint8_t cmdGvarGet(const uint8_t* in, uint8_t inLen, uint8_t* out, uint8_t* outLen)
{
#if defined(GVARS)
  if (inLen < 2) return APP_STATUS_BAD_PARAM;
  uint8_t gv = in[0];
  uint8_t fm = in[1];
  if (gv >= MAX_GVARS || fm >= MAX_FLIGHT_MODES) return APP_STATUS_RANGE;

  putU16(out, (uint16_t)getGVarValue((int8_t)gv, (int8_t)fm));
  *outLen = 2;
  return APP_STATUS_OK;
#else
  return APP_STATUS_UNSUPPORTED;
#endif
}

static uint8_t cmdGvarSet(const uint8_t* in, uint8_t inLen, uint8_t* out, uint8_t* outLen)
{
#if defined(GVARS)
  if (!modelEditable()) return APP_STATUS_NOT_READY;
  if (inLen < 4) return APP_STATUS_BAD_PARAM;
  uint8_t gv = in[0];
  uint8_t fm = in[1];
  if (gv >= MAX_GVARS || fm >= MAX_FLIGHT_MODES) return APP_STATUS_RANGE;

  int16_t value = (int16_t)getU16(&in[2]);

  // written directly instead of via setGVarValue() so that no UI popup can be
  // triggered from the timer context
  mixerTaskLock();
  GVAR_VALUE(gv, getGVarFlightMode(fm, gv)) = value;
  mixerTaskUnlock();

  storageDirty(EE_MODEL);
  return APP_STATUS_OK;
#else
  return APP_STATUS_UNSUPPORTED;
#endif
}

static uint8_t cmdGetStatus(uint8_t* out, uint8_t* outLen)
{  out[0] = storageDirtyMsk;
  out[1] = mixerTaskRunning() ? 1 : 0;
  *outLen = 2;
  return APP_STATUS_OK;
}

// Commands that change the model / radio state and therefore must not be
// executed twice when the host retransmits a frame.
static bool isMutatingCmd(uint8_t cmd)
{
  switch (cmd) {
  case CMD_MIX_INSERT:
  case CMD_MIX_SET:
  case CMD_MIX_DELETE:
  case CMD_MIX_MOVE:
  case CMD_INPUT_INSERT:
  case CMD_INPUT_SET:
  case CMD_INPUT_DELETE:
  case CMD_INPUT_MOVE:
  case CMD_INPUT_SET_NAME:
  case CMD_OUTPUT_SET:
  case CMD_OUTPUT_SET_NAME:
  case CMD_CURVE_SET:
  case CMD_CURVE_SET_POINT:
  case CMD_CURVE_CLEAR:
  case CMD_CURVE_MIRROR:
  case CMD_LS_SET:
  case CMD_CF_SET:
  case CMD_MODEL_SET_NAME:
  case CMD_MODEL_SELECT:
  case CMD_MODEL_CREATE:
  case CMD_MODEL_DUPLICATE:
  case CMD_MODEL_DELETE:
  case CMD_MODEL_RENAME:
  case CMD_TIMER_SET:
  case CMD_GENERAL_SET:
  case CMD_RF_POWER_SET:
  case CMD_FM_SET:
  case CMD_FM_SET_TRIM:
  case CMD_SUBSCRIBE:
  case CMD_PARAM_SET:
  case CMD_GVAR_SET:
    return true;
  default:
    return false;
  }
}

static uint8_t handleCommand(uint8_t cmd, const uint8_t* in, uint8_t inLen,
                             uint8_t* out, uint8_t* outLen)
{
  *outLen = 0;

  switch (cmd) {
  case CMD_GET_INFO:        return cmdGetInfo(out, outLen);
  case CMD_MIX_COUNT:
    if (inLen < 1) return APP_STATUS_BAD_PARAM;
    if (in[0] >= MAX_OUTPUT_CHANNELS) return APP_STATUS_RANGE;
    out[0] = mixLineCount(in[0]);
    *outLen = 1;
    return APP_STATUS_OK;
  case CMD_MIX_GET:         return cmdMixGet(in, inLen, out, outLen);
  case CMD_MIX_INSERT:      return cmdMixInsert(in, inLen, out, outLen);
  case CMD_MIX_SET:         return cmdMixSet(in, inLen, out, outLen);
  case CMD_MIX_DELETE:      return cmdMixDelete(in, inLen, out, outLen);
  case CMD_MIX_MOVE:        return cmdMixMove(in, inLen, out, outLen);
  case CMD_INPUT_COUNT:     return cmdInputCount(in, inLen, out, outLen);
  case CMD_INPUT_GET:       return cmdInputGet(in, inLen, out, outLen);
  case CMD_INPUT_INSERT:    return cmdInputInsert(in, inLen, out, outLen);
  case CMD_INPUT_SET:       return cmdInputSet(in, inLen, out, outLen);
  case CMD_INPUT_DELETE:    return cmdInputDelete(in, inLen, out, outLen);
  case CMD_INPUT_MOVE:      return cmdInputMove(in, inLen, out, outLen);
  case CMD_INPUT_GET_NAME:  return cmdInputGetName(in, inLen, out, outLen);
  case CMD_INPUT_SET_NAME:  return cmdInputSetName(in, inLen, out, outLen);
  case CMD_OUTPUT_GET:      return cmdOutputGet(in, inLen, out, outLen);
  case CMD_OUTPUT_SET:      return cmdOutputSet(in, inLen, out, outLen);
  case CMD_OUTPUT_SET_NAME: return cmdOutputSetName(in, inLen, out, outLen);
  case CMD_CURVE_GET:       return cmdCurveGet(in, inLen, out, outLen);
  case CMD_CURVE_SET:       return cmdCurveSet(in, inLen, out, outLen);
  case CMD_CURVE_SET_POINT: return cmdCurveSetPoint(in, inLen, out, outLen);
  case CMD_CURVE_CLEAR:     return cmdCurveClear(in, inLen, out, outLen);
  case CMD_CURVE_MIRROR:    return cmdCurveMirror(in, inLen, out, outLen);
  case CMD_LS_GET:          return cmdLsGet(in, inLen, out, outLen);
  case CMD_LS_SET:          return cmdLsSet(in, inLen, out, outLen);
  case CMD_CF_GET:          return cmdCfGet(in, inLen, out, outLen);
  case CMD_CF_SET:          return cmdCfSet(in, inLen, out, outLen);
  case CMD_MODEL_GET_NAME:  return cmdModelGetName(out, outLen);
  case CMD_MODEL_SET_NAME:  return cmdModelSetName(in, inLen, out, outLen);
  case CMD_TIMER_GET:       return cmdTimerGet(in, inLen, out, outLen);
  case CMD_TIMER_SET:       return cmdTimerSet(in, inLen, out, outLen);
  case CMD_MODEL_LIST:      return cmdModelList(in, inLen, out, outLen);
  case CMD_MODEL_SELECT:    return cmdModelSelect(in, inLen, out, outLen);
  case CMD_MODEL_CREATE:    return cmdModelCreate(in, inLen, out, outLen);
  case CMD_MODEL_DUPLICATE: return cmdModelDuplicate(in, inLen, out, outLen);
  case CMD_MODEL_DELETE:    return cmdModelDelete(in, inLen, out, outLen);
  case CMD_MODEL_RENAME:    return cmdModelRename(in, inLen, out, outLen);
  case CMD_GENERAL_GET:     return cmdGeneralGet(in, inLen, out, outLen);
  case CMD_GENERAL_SET:     return cmdGeneralSet(in, inLen, out, outLen);
  case CMD_RF_POWER_GET:    return cmdRfPowerGet(in, inLen, out, outLen);
  case CMD_RF_POWER_SET:    return cmdRfPowerSet(in, inLen, out, outLen);
  case CMD_FM_GET:          return cmdFmGet(in, inLen, out, outLen);
  case CMD_FM_SET:          return cmdFmSet(in, inLen, out, outLen);
  case CMD_FM_SET_TRIM:     return cmdFmSetTrim(in, inLen, out, outLen);
  case CMD_GET_STATUS:      return cmdGetStatus(out, outLen);
  case CMD_SUBSCRIBE:       return cmdSubscribe(in, inLen, out, outLen);
  case CMD_PARAM_LIST:      return cmdParamList(in, inLen, out, outLen);
  case CMD_PARAM_GET:       return cmdParamGet(in, inLen, out, outLen);
  case CMD_PARAM_SET:       return cmdParamSet(in, inLen, out, outLen);
  case CMD_PARAM_EXPORT:    return cmdParamExport(in, inLen, out, outLen);
  case CMD_GVAR_GET:        return cmdGvarGet(in, inLen, out, outLen);
  case CMD_GVAR_SET:        return cmdGvarSet(in, inLen, out, outLen);
  default:                  return APP_STATUS_BAD_CMD;
  }
}

// Build and transmit one frame. `seq` must be 0 for unsolicited events.
// When `outFrame` is given the transmitted frame is also copied there.
static void sendFrame(uint8_t cmd, uint8_t seq, uint8_t status,
                      const uint8_t* data, uint8_t dataLen,
                      uint8_t* outFrame = nullptr)
{
  uint8_t frame[APP_MAX_FRAME];
  frame[0] = APP_SYNC0;
  frame[1] = APP_SYNC1;
  frame[2] = cmd;
  frame[3] = seq;

  // LEN counts the status byte plus the command data
  uint8_t payloadLen = dataLen + 1;
  frame[4] = payloadLen;
  frame[5] = status;
  if (dataLen) memcpy(&frame[6], data, dataLen);

  // CRC covers CMD, SEQ, LEN and the whole payload
  uint16_t crc = crc16(&frame[2], (uint32_t)(3 + payloadLen));
  frame[5 + payloadLen] = (uint8_t)(crc & 0xFF);
  frame[6 + payloadLen] = (uint8_t)(crc >> 8);

  uint8_t total = 7 + payloadLen;
  sendRaw(frame, total);

  if (outFrame) memcpy(outFrame, frame, total);
}

static void processFrame()
{
  uint8_t cmd = rxFrame[2];
  uint8_t seq = rxFrame[3];
  uint8_t len = rxFrame[4];

  // Retransmission of a mutating command: resend the cached answer.
  if (isMutatingCmd(cmd) && lastWriteValid &&
      cmd == lastWriteCmd && seq == lastWriteSeq) {
    sendRaw(lastWriteResp, lastWriteRespLen);
    return;
  }

  uint8_t data[APP_MAX_PAYLOAD];
  uint8_t dataLen = 0;
  uint8_t status = handleCommand(cmd, &rxFrame[5], len, data, &dataLen);

  if (isMutatingCmd(cmd)) {
    sendFrame(cmd, seq, status, data, dataLen, lastWriteResp);
    lastWriteRespLen = 8 + dataLen;
    lastWriteCmd = cmd;
    lastWriteSeq = seq;
    lastWriteValid = true;
  } else {
    sendFrame(cmd, seq, status, data, dataLen);
  }
}

static void parseByte(uint8_t b)
{
  switch (rxState) {
  case PARSE_SYNC0:
    if (b == APP_SYNC0) rxState = PARSE_SYNC1;
    break;

  case PARSE_SYNC1:
    if (b == APP_SYNC1) {
      rxState = PARSE_CMD;
    } else if (b != APP_SYNC0) {
      rxState = PARSE_SYNC0;
    }
    break;

  case PARSE_CMD:
    rxFrame[2] = b;
    rxState = PARSE_SEQ;
    break;

  case PARSE_SEQ:
    rxFrame[3] = b;
    rxState = PARSE_LEN;
    break;

  case PARSE_LEN:
    rxFrame[4] = b;
    rxPayloadIdx = 0;
    if (b > APP_MAX_PAYLOAD) {
      rxState = PARSE_SYNC0;
    } else {
      rxState = b ? PARSE_PAYLOAD : PARSE_CRC_LO;
    }
    break;

  case PARSE_PAYLOAD:
    rxFrame[5 + rxPayloadIdx++] = b;
    if (rxPayloadIdx >= rxFrame[4]) rxState = PARSE_CRC_LO;
    break;

  case PARSE_CRC_LO:
    rxCrcLo = b;
    rxState = PARSE_CRC_HI;
    break;

  case PARSE_CRC_HI: {
    uint16_t crc = (uint16_t)((uint16_t)b << 8) | rxCrcLo;
    uint16_t calc = crc16(&rxFrame[2], (uint32_t)(3 + rxFrame[4]));
    rxState = PARSE_SYNC0;
    if (crc == calc) processFrame();
    break;
  }

  default:
    rxState = PARSE_SYNC0;
    break;
  }
}

static void appConfigWakeup()
{
  if (!appDrv || !appDrv->getByte) return;

  uint8_t b;
  while (appDrv->getByte(appCtx, &b) > 0) {
    parseByte(b);
  }
}

static void appConfigTimerCb(timer_handle_t*)
{
  // Do not serve the protocol before the mixer has been started: some
  // commands rely on mixerTaskStop()/mixerTaskStart() being safe to call.
  if (!mixerTaskStarted()) return;

  // A model change is pending: stop serving commands, let the storage writer
  // flush the settings and then reboot into the newly selected model.
  if (modelSwitchCountdown) {
    if (--modelSwitchCountdown == 0) {
#if !defined(SIMU)
      NVIC_SystemReset();
#endif
    }
    return;
  }

  appConfigWakeup();

  // Coalesced "settings changed" notification
  if (subscribedEvents & SUBSCRIBE_SETTINGS_CHANGED) {
    if (appConfigEventMask) {
      if (eventCooldown) {
        eventCooldown--;
      } else {
        uint8_t ev[2] = { EVENT_SETTINGS_CHANGED, appConfigEventMask };
        appConfigEventMask = 0;
        eventCooldown = 20;  // at most one event every 200 ms
        sendFrame(CMD_EVENT, 0, APP_STATUS_OK, ev, sizeof(ev));
      }
    }
  } else {
    appConfigEventMask = 0;
  }
}

void appConfigSetSerialDriver(void* ctx, const etx_serial_driver_t* drv)
{
  appCtx = nullptr;
  appDrv = nullptr;

  if (!drv) {
    if (timer_is_created(&appTimer)) timer_stop(&appTimer);
    return;
  }

  appCtx = ctx;
  appDrv = drv;

  rxState = PARSE_SYNC0;
  lastWriteValid = false;
  subscribedEvents = 0;
  appConfigEventMask = 0;
  modelSwitchCountdown = 0;

  if (!timer_is_created(&appTimer)) {
    timer_create(&appTimer, appConfigTimerCb, "appcfg", APP_TIMER_PERIOD_MS, true);
  }
  timer_start(&appTimer);
}

#endif  // !BOOT
