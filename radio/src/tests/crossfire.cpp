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

#include "gtest/gtest.h"
#include "gtests.h"
#include "telemetry/telemetry.h"
#include "telemetry/crossfire.h"
#include "telemetry/sensor_names.h"
#include "crc.h"

#if defined(CROSSFIRE)

uint8_t createCrossfireChannelsFrame(uint8_t moduleIdx, uint8_t * frame, int16_t * pulses);

// Spec-defined part of the frame (0x16 RC Channels Packed): sync byte, type,
// 16 x 11-bit channel packing. Expected bytes computed independently, not
// derived from createCrossfireChannelsFrame() itself.
TEST(Crossfire, createCrossfireChannelsFrame)
{
  MODEL_RESET();

  int16_t pulsesStart[MAX_TRAINER_CHANNELS];
  uint8_t crossfire[CROSSFIRE_FRAME_MAXLEN];

  memset(crossfire, 0, sizeof(crossfire));
  for (int i=0; i<MAX_TRAINER_CHANNELS; i++) {
    pulsesStart[i] = -1024 + (2048 / MAX_TRAINER_CHANNELS) * i;
  }

  createCrossfireChannelsFrame(EXTERNAL_MODULE, crossfire, pulsesStart);

  ASSERT_EQ(crossfire[0], MODULE_ADDRESS);
  ASSERT_EQ(crossfire[2], CHANNELS_ID);

  const uint8_t expectedChannelData[22] = {
    0xAD, 0xA0, 0x88, 0x5E, 0xC0, 0x73, 0xA4, 0x56, 0x51, 0x4C, 0x6F,
    0xE0, 0x33, 0x22, 0x2B, 0x27, 0x9A, 0x57, 0xF0, 0x1A, 0x99, 0xD5
  };
  ASSERT_EQ(memcmp(&crossfire[3], expectedChannelData, sizeof(expectedChannelData)), 0);
}

// Status byte after the 0x16 payload is an ExpressLRS extension, not TBS CRSF
// spec (semantics per ExpressLRS's TXModuleEndpoint.cpp / crsf_protocol.h).
// Frame is always 25 bytes (1 ID + 22 channel data + 1 status + 1 CRC);
// bit 0 = commanded armed status (Switch mode only), bit 1 = arming mode is CH5.
TEST(Crossfire, ExpressLRSArmingExtension_CH5Mode)
{
  MODEL_RESET();

  int16_t pulsesStart[MAX_TRAINER_CHANNELS];
  uint8_t crossfire[CROSSFIRE_FRAME_MAXLEN];

  memset(crossfire, 0, sizeof(crossfire));
  for (int i=0; i<MAX_TRAINER_CHANNELS; i++) {
    pulsesStart[i] = -1024 + (2048 / MAX_TRAINER_CHANNELS) * i;
  }

  g_model.moduleData[EXTERNAL_MODULE].crsf.crsfArmingMode = ARMING_MODE_CH5;

  uint8_t len = createCrossfireChannelsFrame(EXTERNAL_MODULE, crossfire, pulsesStart);

  ASSERT_EQ(len, 27);
  ASSERT_EQ(crossfire[0], MODULE_ADDRESS);
  ASSERT_EQ(crossfire[1], 25);
  ASSERT_EQ(crossfire[2], CHANNELS_ID);
  ASSERT_EQ(crossfire[25], 0x02); // bit 1: arming mode CH5

  uint8_t crc = crc8(&crossfire[2], 24);
  ASSERT_EQ(crossfire[26], crc);
}

TEST(Crossfire, ExpressLRSArmingExtension_SwitchMode)
{
  MODEL_RESET();

  int16_t pulsesStart[MAX_TRAINER_CHANNELS];
  uint8_t crossfire[CROSSFIRE_FRAME_MAXLEN];

  memset(crossfire, 0, sizeof(crossfire));
  for (int i=0; i<MAX_TRAINER_CHANNELS; i++) {
    pulsesStart[i] = -1024 + (2048 / MAX_TRAINER_CHANNELS) * i;
  }

  g_model.moduleData[EXTERNAL_MODULE].crsf.crsfArmingMode = ARMING_MODE_SWITCH;
  g_model.moduleData[EXTERNAL_MODULE].crsf.crsfArmingTrigger = SWSRC_NONE;

  uint8_t len = createCrossfireChannelsFrame(EXTERNAL_MODULE, crossfire, pulsesStart);

  ASSERT_EQ(len, 27);
  ASSERT_EQ(crossfire[0], MODULE_ADDRESS);
  ASSERT_EQ(crossfire[1], 25);
  ASSERT_EQ(crossfire[2], CHANNELS_ID);
  ASSERT_EQ(crossfire[25], 0); // SWSRC_NONE -> not armed, bit 1 clear (Switch mode)

  uint8_t crc = crc8(&crossfire[2], 24);
  ASSERT_EQ(crossfire[26], crc);
}

TEST(Crossfire, crc8)
{
  uint8_t frame[] = { 0x00, 0x0C, 0x14, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x01, 0x03, 0x00, 0x00, 0x00, 0xF4 };
  uint8_t crc = crc8(&frame[2], frame[1]-1);
  ASSERT_EQ(frame[frame[1]+1], crc);
}

#if defined(HARDWARE_EXTERNAL_MODULE)
#include "pulses/crossfire.h"

struct crsf_frame_test {
  void* ctx = nullptr;

  uint8_t buffer[TELEMETRY_RX_PACKET_SIZE];
  uint8_t len = 0;

  crsf_frame_test()
  {
    ctx = CrossfireDriver.init(EXTERNAL_MODULE);
    if (!luaInputTelemetryFifo) {
      luaInputTelemetryFifo = new TelemetryQueue();
      assert(luaInputTelemetryFifo != nullptr);
    } else {
      luaInputTelemetryFifo->clear();
    }
  }

  template<unsigned Len>
  void process(uint8_t (&frame)[Len]) {
    CrossfireDriver.processFrame(ctx, frame, Len, buffer, &len);    
  }

  ~crsf_frame_test()
  {
    if (ctx != nullptr) {
      CrossfireDriver.deinit(ctx);
    }
  }
};

static uint8_t incomplete_frame[] = {
    // first frame
    0xEA, 0x14, 0xFF, 0x11, 0xFD, 0x05, 0x00, 0x00, 0x13, 0x01, 0x01,
    0x2A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x5D, 0x98, 0xB4,
    // second frame
    0xEA, 0x21, 0xFF, 0x1E, 0xFD, 0x12, 0x00, 0x00, 0x14, 0x01, 0x01,
    0x4A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x8F, 0xC2, 0x35, 0x3F, 0xFD, 0xC7, 0xD1, 0x3C, 0x4C, 0x01, 0x92,
    0x3F, 0x31,
    // incomplete third frame
    0xEA, 0x1F, 0xFF, 0x1C, 0xFD, 0x10, 0x00,
};

static uint8_t cont_frame[] = {
    0x00, 0x15, 0x01, 0x01, 0x24, 0x00, 0x00, 0x35, 0x7D, 0xF2, 0x40,
    0xE8, 0x03, 0xE8, 0x03, 0xDC, 0x05, 0xDC, 0x05, 0xE1, 0x05, 0xE1,
    0x05, 0x2A, 0xFE, 0x5F,
    //next trailing packet start
    0xEA, 0x0A, 0x0B
};

TEST(Crossfire, frameParser_incompleteFrames)
{
  crsf_frame_test ft;
  if (!ft.ctx) return;
  
  ft.process(incomplete_frame);
  EXPECT_EQ(ft.len, 7);
  EXPECT_EQ(ft.buffer[0], 0xEA);
  EXPECT_EQ(ft.buffer[1], 0x1F);

  ft.process(cont_frame);
  EXPECT_EQ(ft.len, 3);
  EXPECT_EQ(ft.buffer[0], 0xEA);
  EXPECT_EQ(ft.buffer[1], 0x0A);

  uint8_t* lua_buffer = luaInputTelemetryFifo->buffer();
  EXPECT_EQ(luaInputTelemetryFifo->size(), (size_t)(0x14 + 0x21 + 0x1F));

  unsigned offset = 0;
  EXPECT_EQ(lua_buffer[offset], 0x14);
  offset += 0x14;
  
  EXPECT_EQ(lua_buffer[offset], 0x21);
  offset += 0x21;

  EXPECT_EQ(lua_buffer[offset], 0x1F);
  EXPECT_EQ(lua_buffer[offset + 0x1F - 1], 0xFE);
}

static uint8_t length_error[] = {
    0x2A, 0xFE, 0x5F, 0x00,
};

static uint8_t length_error2[] = {
    // first frame
    0xEA, 0x09, 0xFF, 0x11, 0xFD, 0x05, 0x00, 0x00, 0x13, 0x01, 0x8C,
    // 2nd incomplete frame
    0xEA, 0xFE, 0x5F, 0x00,
};

static uint8_t length_error3[] = {
    // first frame
    0xEA, 0x09, 0xFF, 0x11, 0xFD, 0x05, 0x00, 0x00, 0x13, 0x01, 0x8C,
    // invalid: invalid frame start
    0x2A, 0x09, 0x5F, 0x00,
    // 2nd valid frame
    0xEA, 0x09, 0xFF, 0x11, 0xFD, 0x05, 0x00, 0x00, 0x13, 0x01, 0x8C,
};


TEST(Crossfire, frameParser_length)
{
  crsf_frame_test ft;
  if (!ft.ctx) return;

  uint8_t* lua_buffer = luaInputTelemetryFifo->buffer();

  // Check that a frame that is too big is rejected even if incomplete
  ft.process(length_error);
  EXPECT_EQ(ft.len, 0);

  // Check that a frame that is too big is rejected if positioned
  // after a complete frame
  ft.process(length_error2);
  EXPECT_EQ(ft.len, 0);

  // the first complete frame should have been processed
  EXPECT_EQ(luaInputTelemetryFifo->size(), (size_t)0x09);

  ft.process(length_error3);
  EXPECT_EQ(ft.len, 0);

  // only the first frame has been processed, as the rest
  // of the input buffer is thrown away due to length error
  EXPECT_EQ(luaInputTelemetryFifo->size(), (size_t)(0x09 + 0x09 + 0x09));

  // check all 3 frames
  unsigned offset = 0;
  for (int i = 0; i < 3; i++) {
    EXPECT_EQ(lua_buffer[offset], 0x09);
    EXPECT_EQ(lua_buffer[offset + 0x09 - 1], 0x01);
    offset += 0x09;
  }
}

static uint8_t invalid_frames[] = {
    // first frame
    0xEA, 0x14, 0xFF, 0x11, 0xFD, 0x05, 0x00, 0x00, 0x13, 0x01, 0x01,
    0x2A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x5D, 0x98, 0xB4,
    // random bytes
    0x00, 0x35, 0xA4,
    // second frame
    0xEA, 0x21, 0xFF, 0x1E, 0xFD, 0x12, 0x00, 0x00, 0x14, 0x01, 0x01,
    0x4A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x8F, 0xC2, 0x35, 0x3F, 0xFD, 0xC7, 0xD1, 0x3C, 0x4C, 0x01, 0x92,
    0x3F, 0x31,
    // invalid CRC frame
    0xEA, 0x02, 0x00, 0x01,
    // third frame
    0xEA, 0x1F, 0xFF, 0x1C, 0xFD, 0x10, 0x00,0x00, 0x15, 0x01, 0x01, 0x24, 0x00, 0x00, 0x35, 0x7D, 0xF2, 0x40,
    0xE8, 0x03, 0xE8, 0x03, 0xDC, 0x05, 0xDC, 0x05, 0xE1, 0x05, 0xE1,
    0x05, 0x2A, 0xFE, 0x5F,
};

TEST(Crossfire, frameParser_badFrames)
{
  //check if frameParser correctly skips bad frames (too long, bad CRC) and does't lose following packets 
  crsf_frame_test ft;
  if (!ft.ctx) return;

  ft.process(invalid_frames);
  EXPECT_EQ(ft.len,0);

  uint8_t* lua_buffer = luaInputTelemetryFifo->buffer();
  EXPECT_EQ(luaInputTelemetryFifo->size(), (size_t)(0x14 + 0x21 + 0x1F));

  unsigned offset = 0;
  EXPECT_EQ(lua_buffer[offset], 0x14);
  offset += 0x14;
  
  EXPECT_EQ(lua_buffer[offset], 0x21);
  offset += 0x21;

  EXPECT_EQ(lua_buffer[offset], 0x1F);
  EXPECT_EQ(lua_buffer[offset + 0x1F - 1], 0xFE);
}

static uint8_t jumboFrame1[]={
  0xEA, 0x3E, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFB
};
static uint8_t jumboFrame2[]={ //jumbo frame 2 
  0x8D, 0xEA, 0x3D, 0xFE, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x57
};

TEST(Crossfire, frameParser_multipleJumboFrames)
{
  crsf_frame_test ft;
  if (!ft.ctx) return;

  ft.process(jumboFrame1);
  EXPECT_EQ(ft.len, 63);
  EXPECT_EQ(ft.buffer[0], 0xEA);
  EXPECT_EQ(ft.buffer[1], 0x3E);

  ft.process(jumboFrame2);
  EXPECT_EQ(ft.len,0);

  uint8_t* lua_buffer = luaInputTelemetryFifo->buffer();
  EXPECT_EQ(luaInputTelemetryFifo->size(), (size_t)(62 + 61));

  unsigned offset = 0;
  EXPECT_EQ(lua_buffer[offset], 0x3E);
  EXPECT_EQ(lua_buffer[offset + 0x3E - 1], 0xFB);

  EXPECT_EQ(lua_buffer[offset], 0x3E);
  offset += 0x3E;
  
  EXPECT_EQ(lua_buffer[offset], 0x3D);
  EXPECT_EQ(lua_buffer[offset + 0x3D - 1], 0xF0);
}

//-----------------------------------------------------------------------------
// ArduPilot custom telemetry frames (0x80, legacy 0x7F) carry the yaapu
// passthrough sensors. Roll and pitch sit in the 0x5006 packet, yaw in 0x5005,
// all in 0.2 deg steps; they are decoded into the same Ptch/Roll/Yaw sensors as
// the native ATTITUDE frame, so they keep updating when the vehicle throttles
// that frame to 1Hz (ArduPilot RC_OPTIONS bit 8).
//-----------------------------------------------------------------------------
static int findSensorIndex(const char* label)
{
  for (int i = 0; i < MAX_TELEMETRY_SENSORS; i++) {
    if (g_model.telemetrySensors[i].isAvailable() &&
        strncmp(g_model.telemetrySensors[i].label, label, TELEM_LABEL_LEN) == 0) {
      return i;
    }
  }
  return -1;
}

// roll -11.6 deg  -> round((-11.6 * 100 + 18000) * 0.05) = 842
// pitch 34.4 deg  -> round((34.4 * 100 + 9000) * 0.05) = 622
// yaw 270.0 deg   -> round(270.0 * 100 * 0.05) = 1350
// data = 842 | (622 << 11) = 0x0013734A, 1350 << 17 = 0x0A8C0000
// appid and data are little endian, the CRC goes into the last byte
static uint8_t ap_passthrough_attitude[] = {
    0xEA, 0x09, 0x80, 0xF0, 0x06, 0x50, 0x4A, 0x73, 0x13, 0x00, 0x00,
};

static uint8_t ap_passthrough_yaw[] = {
    0xEA, 0x09, 0x80, 0xF0, 0x05, 0x50, 0x00, 0x00, 0x8C, 0x0A, 0x00,
};

static uint8_t ap_passthrough_multi[] = {
    0xEA, 0x10, 0x80, 0xF2, 0x02, 0x06, 0x50, 0x4A, 0x73, 0x13,
    0x00, 0x05, 0x50, 0x00, 0x00, 0x8C, 0x0A, 0x00,
};

// HOME packet (0x5004): bearing to home in 3 deg steps at bits 25-31, the low
// bits carry the distance and the altitude: 41 * 3 = 123 deg, the 0x23700C of
// the other fields must not leak into it. The second frame encodes 121 * 3 =
// 363 deg, which has to wrap to 3
static uint8_t ap_passthrough_home[] = {
    0xEA, 0x09, 0x80, 0xF0, 0x04, 0x50, 0x0C, 0x70, 0x23, 0x52, 0x00,
};

static uint8_t ap_passthrough_home_wrapped[] = {
    0xEA, 0x09, 0x80, 0xF0, 0x04, 0x50, 0x0C, 0x70, 0x23, 0xF2, 0x00,
};

// HOME packet with the altitude above home: 500 dm mantissa with an exponent
// of 1 -> 500.0 m (bearing still 41 * 3), and the same with the sign bit set:
// 125 dm -> -12.5 m
static uint8_t ap_passthrough_home_alt[] = {
    0xEA, 0x09, 0x80, 0xF0, 0x04, 0x50, 0x00, 0x10, 0x7D, 0x52, 0x00,
};

static uint8_t ap_passthrough_home_alt_neg[] = {
    0xEA, 0x09, 0x80, 0xF0, 0x04, 0x50, 0x00, 0x40, 0x1F, 0x53, 0x00,
};

// HOME packets with the distance to home: 123 * 10 m with the exponent set and
// 456 m without it
static uint8_t ap_passthrough_home_dist[] = {
    0xEA, 0x09, 0x80, 0xF0, 0x04, 0x50, 0xED, 0x01, 0x00, 0x52, 0x00,
};

static uint8_t ap_passthrough_home_dist_short[] = {
    0xEA, 0x09, 0x80, 0xF0, 0x04, 0x50, 0x20, 0x07, 0x00, 0x52, 0x00,
};

// native ATTITUDE frame: pitch 10 deg, roll 23 deg, yaw 30 deg in 1e-4 rad
static uint8_t native_attitude[] = {
    0xEA, 0x08, 0x1E, 0x06, 0xD1, 0x0F, 0xAE, 0x14, 0x74, 0x00,
};

// GPS status packet (0x5002): 12 satellites (0xC), the HDOP bits above the
// count must not leak into it
static uint8_t ap_passthrough_gps[] = {
    0xEA, 0x09, 0x80, 0xF0, 0x02, 0x50, 0xFC, 0x37, 0x00, 0x00, 0x00,
};

// native GPS frame: 20 satellites in the last payload byte
static uint8_t native_gps[] = {
    0xEA, 0x11, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0x00,
};

// RPM packet (0x500A): the two RPM sensors of the vehicle in 0.1 rpm as signed
// 16 bit values, sensor 1 in bits 0-15 (1234 -> 12340 rpm), sensor 2 in bits
// 16-31 (0xFED4 -> -300 -> -3000 rpm)
static uint8_t ap_passthrough_rpm[] = {
    0xEA, 0x09, 0x80, 0xF0, 0x0A, 0x50, 0xD2, 0x04, 0xD4, 0xFE, 0x00,
};

// VEL_YAW packet (0x5005): vertical speed in dm/s as a 7 bit mantissa (bits
// 1-7), an exponent bit (bit 0, x10) and a sign bit (bit 8). -127 dm/s =
// -12.7 m/s, yaw is the 270 deg of the frame above
static uint8_t ap_passthrough_vspeed[] = {
    0xEA, 0x09, 0x80, 0xF0, 0x05, 0x50, 0xFE, 0x01, 0x8C, 0x0A, 0x00,
};

// same packet with the exponent bit set: 5 * 10 = 50 dm/s = 5.0 m/s, yaw 0
static uint8_t ap_passthrough_vspeed_exp[] = {
    0xEA, 0x09, 0x80, 0xF0, 0x05, 0x50, 0x0B, 0x00, 0x00, 0x00, 0x00,
};

// native vario frame (0x07): 100 cm/s = 1.00 m/s
static uint8_t native_vario[] = {
    0xEA, 0x04, 0x07, 0x00, 0x64, 0x00,
};

static void finishFrame(uint8_t* frame)
{
  frame[frame[1] + 1] = crc8(&frame[2], frame[1] - 1);
}

static void resetPassthroughTest()
{
  MODEL_RESET();
  TELEMETRY_RESET();
  // as if a LINK_ID frame with a valid link quality had arrived
  telemetryStreaming = TELEMETRY_TIMEOUT10ms;
  allowNewSensors = true;
}

// the sensor keeps UNIT_RADIANS, and the value is scaled by its precision
static float sensorRadians(int idx)
{
  float scale = 1.0f;
  for (int p = 0; p < g_model.telemetrySensors[idx].prec; p++) scale *= 0.1f;
  return telemetryItems[idx].value * scale;
}

// Without passthrough frames the native ATTITUDE frame remains the attitude
// source. Keep this test ahead of the passthrough ones: the passthrough
// attitude state is module global, so the module has to be untouched here.
TEST(Crossfire, ArduPilotPassthroughWithoutPassthroughFrames)
{
  resetPassthroughTest();

  crsf_frame_test ft;
  if (!ft.ctx) return;

  finishFrame(native_attitude);
  ft.process(native_attitude);

  const int rollIdx = findSensorIndex(STR_SENSOR_ROLL);
  const int pitchIdx = findSensorIndex(STR_SENSOR_PITCH);
  const int yawIdx = findSensorIndex(STR_SENSOR_YAW);
  ASSERT_GE(rollIdx, 0);
  ASSERT_GE(pitchIdx, 0);
  ASSERT_GE(yawIdx, 0);
  EXPECT_NEAR(sensorRadians(pitchIdx), 10.0f * M_PI / 180.0f, 0.01f);
  EXPECT_NEAR(sensorRadians(rollIdx), 23.0f * M_PI / 180.0f, 0.01f);
  EXPECT_NEAR(sensorRadians(yawIdx), 30.0f * M_PI / 180.0f, 0.01f);
}

// The VEL_YAW packet carries the vertical speed the yaapu vario shows; it is
// sent faster than the native vario frames, which it replaces while fresh.
// Keep this test ahead of the other passthrough ones: the vertical speed state
// is module global and they send 0x5005 frames as well.
TEST(Crossfire, ArduPilotPassthroughVSpeed)
{
  resetPassthroughTest();

  crsf_frame_test ft;
  if (!ft.ctx) return;

  finishFrame(native_vario);
  ft.process(native_vario);

  const int vspdIdx = findSensorIndex(STR_SENSOR_VSPD);
  ASSERT_GE(vspdIdx, 0);
  // the sensor carries cm/s, as UNIT_METERS_PER_SECOND with a precision of 2
  EXPECT_EQ(telemetryItems[vspdIdx].value, 100);

  finishFrame(ap_passthrough_vspeed);
  ft.process(ap_passthrough_vspeed);
  EXPECT_EQ(telemetryItems[vspdIdx].value, -1270);

  // the native frame is ignored while the passthrough one is fresh
  ft.process(native_vario);
  EXPECT_EQ(telemetryItems[vspdIdx].value, -1270);

  finishFrame(ap_passthrough_vspeed_exp);
  ft.process(ap_passthrough_vspeed_exp);
  EXPECT_EQ(telemetryItems[vspdIdx].value, 500);
}

TEST(Crossfire, ArduPilotPassthroughAttitude)
{
  resetPassthroughTest();

  crsf_frame_test ft;
  if (!ft.ctx) return;

  finishFrame(ap_passthrough_attitude);
  finishFrame(ap_passthrough_yaw);
  ft.process(ap_passthrough_attitude);
  ft.process(ap_passthrough_yaw);

  const int rollIdx = findSensorIndex(STR_SENSOR_ROLL);
  const int pitchIdx = findSensorIndex(STR_SENSOR_PITCH);
  const int yawIdx = findSensorIndex(STR_SENSOR_YAW);
  ASSERT_GE(rollIdx, 0);
  ASSERT_GE(pitchIdx, 0);
  ASSERT_GE(yawIdx, 0);
  EXPECT_NEAR(sensorRadians(rollIdx), -11.6f * M_PI / 180.0f, 0.01f);
  EXPECT_NEAR(sensorRadians(pitchIdx), 34.4f * M_PI / 180.0f, 0.01f);
  // yaw is normalized to the +-180 range of the native frame: 270 -> -90
  EXPECT_NEAR(sensorRadians(yawIdx), -90.0f * M_PI / 180.0f, 0.01f);

  // the raw frame still reaches the Lua queue (yaapu and friends)
  EXPECT_EQ(luaInputTelemetryFifo->size(), (size_t)(0x09 + 0x09));

  // the slower native ATTITUDE frame is ignored while the passthrough attitude
  // is fresh
  finishFrame(native_attitude);
  ft.process(native_attitude);
  EXPECT_NEAR(sensorRadians(rollIdx), -11.6f * M_PI / 180.0f, 0.01f);
  EXPECT_NEAR(sensorRadians(pitchIdx), 34.4f * M_PI / 180.0f, 0.01f);
  EXPECT_NEAR(sensorRadians(yawIdx), -90.0f * M_PI / 180.0f, 0.01f);
}

TEST(Crossfire, ArduPilotPassthroughMultiPacket)
{
  resetPassthroughTest();

  crsf_frame_test ft;
  if (!ft.ctx) return;

  finishFrame(ap_passthrough_multi);
  ft.process(ap_passthrough_multi);

  const int rollIdx = findSensorIndex(STR_SENSOR_ROLL);
  const int pitchIdx = findSensorIndex(STR_SENSOR_PITCH);
  const int yawIdx = findSensorIndex(STR_SENSOR_YAW);
  ASSERT_GE(rollIdx, 0);
  ASSERT_GE(pitchIdx, 0);
  ASSERT_GE(yawIdx, 0);
  EXPECT_NEAR(sensorRadians(rollIdx), -11.6f * M_PI / 180.0f, 0.01f);
  EXPECT_NEAR(sensorRadians(pitchIdx), 34.4f * M_PI / 180.0f, 0.01f);
  EXPECT_NEAR(sensorRadians(yawIdx), -90.0f * M_PI / 180.0f, 0.01f);
}

TEST(Crossfire, ArduPilotPassthroughHomeBearing)
{
  resetPassthroughTest();

  crsf_frame_test ft;
  if (!ft.ctx) return;

  EXPECT_EQ(getArduPilotHomeBearing(), -1);

  finishFrame(ap_passthrough_home);
  ft.process(ap_passthrough_home);
  EXPECT_EQ(getArduPilotHomeBearing(), 123);

  finishFrame(ap_passthrough_home_wrapped);
  ft.process(ap_passthrough_home_wrapped);
  EXPECT_EQ(getArduPilotHomeBearing(), 3);
}

// The same frame carries the altitude above home the right readout shows, in
// dm as a 10 bit mantissa, a 2 bit exponent and a sign bit. The home state is
// module global and already fresh from the bearing test, hence no check for the
// "no home frame" value here (the bearing test covers that path).
TEST(Crossfire, ArduPilotPassthroughHomeAltitude)
{
  resetPassthroughTest();

  crsf_frame_test ft;
  if (!ft.ctx) return;

  finishFrame(ap_passthrough_home_alt);
  ft.process(ap_passthrough_home_alt);
  EXPECT_NEAR(getArduPilotHomeAltitude(), 500.0f, 0.01f);
  EXPECT_EQ(getArduPilotHomeBearing(), 123);

  // the sign bit turns it into an altitude below home
  finishFrame(ap_passthrough_home_alt_neg);
  ft.process(ap_passthrough_home_alt_neg);
  EXPECT_NEAR(getArduPilotHomeAltitude(), -12.5f, 0.01f);
  EXPECT_EQ(getArduPilotHomeBearing(), 123);
}

// and the distance to home in the same frame, in m as a 10 bit mantissa with a
// 2 bit exponent below it
TEST(Crossfire, ArduPilotPassthroughHomeDistance)
{
  resetPassthroughTest();

  crsf_frame_test ft;
  if (!ft.ctx) return;

  finishFrame(ap_passthrough_home_dist);
  ft.process(ap_passthrough_home_dist);
  EXPECT_NEAR(getArduPilotHomeDistance(), 1230.0f, 0.01f);

  finishFrame(ap_passthrough_home_dist_short);
  ft.process(ap_passthrough_home_dist_short);
  EXPECT_NEAR(getArduPilotHomeDistance(), 456.0f, 0.01f);
}

// The passthrough GPS status packet carries the satellite count the yaapu
// script shows, the native frame the real one. The passthrough state is module
// global, so check the native frame first.
TEST(Crossfire, ArduPilotPassthroughGpsStatus)
{
  resetPassthroughTest();

  crsf_frame_test ft;
  if (!ft.ctx) return;

  finishFrame(native_gps);
  ft.process(native_gps);

  const int satsIdx = findSensorIndex(STR_SENSOR_SATELLITES);
  ASSERT_GE(satsIdx, 0);
  EXPECT_EQ(telemetryItems[satsIdx].value, 20);

  // once the vehicle sends the passthrough packet, it takes over
  finishFrame(ap_passthrough_gps);
  ft.process(ap_passthrough_gps);
  EXPECT_EQ(telemetryItems[satsIdx].value, 12);

  ft.process(native_gps);
  EXPECT_EQ(telemetryItems[satsIdx].value, 12);

  // 15 is the top of the 4 bit field, the HUD draws it as "15+"
  ap_passthrough_gps[6] = 0x0F;
  finishFrame(ap_passthrough_gps);
  ft.process(ap_passthrough_gps);
  EXPECT_EQ(telemetryItems[satsIdx].value, 15);
}

// The RPM packet feeds the two RPM sensors of the vehicle, both arrive in the
// same frame so each sensor has to keep its own value
TEST(Crossfire, ArduPilotPassthroughRpm)
{
  resetPassthroughTest();

  crsf_frame_test ft;
  if (!ft.ctx) return;

  finishFrame(ap_passthrough_rpm);
  ft.process(ap_passthrough_rpm);

  const int rpm1Idx = findSensorIndex(STR_SENSOR_RPM);
  const int rpm2Idx = findSensorIndex(STR_SENSOR_RPM2);
  ASSERT_GE(rpm1Idx, 0);
  ASSERT_GE(rpm2Idx, 0);
  EXPECT_EQ(telemetryItems[rpm1Idx].value, 12340);
  EXPECT_EQ(telemetryItems[rpm2Idx].value, -3000);

  // sensor 1 = 100 * 10 = 1000 rpm, sensor 2 = 10 * 10 = 100 rpm
  ap_passthrough_rpm[6] = 0x64;
  ap_passthrough_rpm[7] = 0x00;
  ap_passthrough_rpm[8] = 0x0A;
  ap_passthrough_rpm[9] = 0x00;
  finishFrame(ap_passthrough_rpm);
  ft.process(ap_passthrough_rpm);
  EXPECT_EQ(telemetryItems[rpm1Idx].value, 1000);
  EXPECT_EQ(telemetryItems[rpm2Idx].value, 100);
}
#endif // HARDWARE_EXTERNAL_MODULE
#endif

