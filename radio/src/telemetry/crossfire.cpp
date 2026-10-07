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

#include "crossfire.h"
#include "edgetx.h"
#include "math.h"
#include "os/time.h"

#include "trainer.h"
#include "sensor_names.h"

// clang-format off
#define CS(id,subId,name,unit,precision) {id,subId,unit,precision,name}

#define CROSSFIRE_CH_BITS           11
#define CROSSFIRE_CH_MASK           ((1 << CROSSFIRE_CH_BITS) - 1)
#define CROSSFIRE_CH_CENTER         0x3E0

struct CrossfireSensor {
  const uint8_t id;
  const uint8_t subId;
  const TelemetryUnit unit;
  const uint8_t precision;
  const char* name;
};

const CrossfireSensor crossfireSensors[] = {
  CS(LINK_ID,        0, STR_SENSOR_RX_RSSI1,      UNIT_DBM,               0),
  CS(LINK_ID,        1, STR_SENSOR_RX_RSSI2,      UNIT_DBM,               0),
  CS(LINK_ID,        2, STR_SENSOR_RX_QUALITY,    UNIT_PERCENT,           0),
  CS(LINK_ID,        3, STR_SENSOR_RX_SNR,        UNIT_DB,                0),
  CS(LINK_ID,        4, STR_SENSOR_ANTENNA,       UNIT_RAW,               0),
  CS(LINK_ID,        5, STR_SENSOR_RF_MODE,       UNIT_RAW,               0),
  CS(LINK_ID,        6, STR_SENSOR_TX_POWER,      UNIT_MILLIWATTS,        0),
  CS(LINK_ID,        7, STR_SENSOR_TX_RSSI,       UNIT_DBM,               0),
  CS(LINK_ID,        8, STR_SENSOR_TX_QUALITY,    UNIT_PERCENT,           0),
  CS(LINK_ID,        9, STR_SENSOR_TX_SNR,        UNIT_DB,                0),
  CS(LINK_RX_ID,     0, STR_SENSOR_RX_RSSI_PERC,  UNIT_PERCENT,           0),
  CS(LINK_RX_ID,     1, STR_SENSOR_RX_RF_POWER,   UNIT_DBM,               0),
  CS(LINK_TX_ID,     0, STR_SENSOR_TX_RSSI_PERC,  UNIT_PERCENT,           0),
  CS(LINK_TX_ID,     1, STR_SENSOR_TX_POWER,      UNIT_DBM,               0),
  CS(LINK_TX_ID,     2, STR_SENSOR_TX_FPS,        UNIT_HERTZ,             0),
  CS(BATTERY_ID,     0, STR_SENSOR_BATT,          UNIT_VOLTS,             1),
  CS(BATTERY_ID,     1, STR_SENSOR_CURR,          UNIT_AMPS,              1),
  CS(BATTERY_ID,     2, STR_SENSOR_CAPACITY,      UNIT_MAH,               0),
  CS(BATTERY_ID,     3, STR_SENSOR_BATT_PERCENT,  UNIT_PERCENT,           0),
  CS(GPS_ID,         0, STR_SENSOR_GPS,           UNIT_GPS_LATITUDE,      0),
  CS(GPS_ID,         0, STR_SENSOR_GPS,           UNIT_GPS_LONGITUDE,     0),
  CS(GPS_ID,         2, STR_SENSOR_GSPD,          UNIT_KMH,               1),
  CS(GPS_ID,         3, STR_SENSOR_HDG,           UNIT_DEGREE,            2),
  CS(GPS_ID,         4, STR_SENSOR_GPSALT,        UNIT_METERS,            0),
  CS(GPS_ID,         5, STR_SENSOR_SATELLITES,    UNIT_RAW,               0),
  CS(GPS_TIME_ID,    0, STR_SENSOR_GPSDATETIME,   UNIT_DATETIME,          0),
  CS(ATTITUDE_ID,    0, STR_SENSOR_PITCH,         UNIT_RADIANS,           3),
  CS(ATTITUDE_ID,    1, STR_SENSOR_ROLL,          UNIT_RADIANS,           3),
  CS(ATTITUDE_ID,    2, STR_SENSOR_YAW,           UNIT_RADIANS,           3),
  CS(FLIGHT_MODE_ID, 0, STR_SENSOR_FLIGHT_MODE,   UNIT_TEXT,              0),
  CS(CF_VARIO_ID,    0, STR_SENSOR_VSPD,          UNIT_METERS_PER_SECOND, 2),
  CS(BARO_ALT_ID,    0, STR_SENSOR_ALT,           UNIT_METERS,            2),
  CS(AIRSPEED_ID,    0, STR_SENSOR_ASPD,          UNIT_KMH,               1),
  CS(CF_RPM_ID,      0, STR_SENSOR_RPM,           UNIT_RPMS,              0),
  CS(CF_RPM_ID,      1, STR_SENSOR_RPM2,          UNIT_RPMS,              0),
  CS(TEMP_ID,        0, STR_SENSOR_TEMP,          UNIT_DEGREE,            1),
  CS(CELLS_ID,       0, STR_SENSOR_CELLS,         UNIT_CELLS,             2),
  CS(VOLT_ARRAY_ID,  0, STR_SENSOR_VOLT,          UNIT_VOLTS,             2),
  CS(0,              0, STR_SENSOR_UNKNOWN,       UNIT_RAW,               0),
};
// clang-format on

CrossfireModuleStatus crossfireModuleStatus[2] = {0};

const CrossfireSensor & getCrossfireSensor(uint8_t id, uint8_t subId)
{
  if (id == LINK_ID)
    return crossfireSensors[RX_RSSI1_INDEX + subId];
  else if (id == LINK_RX_ID)
    return crossfireSensors[RX_RSSI_PERC_INDEX + subId];
  else if (id == LINK_TX_ID)
    return crossfireSensors[TX_RSSI_PERC_INDEX + subId];
  else if (id == BATTERY_ID)
    return crossfireSensors[BATT_VOLTAGE_INDEX + subId];
  else if (id == GPS_ID)
    return crossfireSensors[GPS_LATITUDE_INDEX + subId];
  else if (id == GPS_TIME_ID)
    return crossfireSensors[GPS_TIME_INDEX];
  else if (id == CF_VARIO_ID)
    return crossfireSensors[VERTICAL_SPEED_INDEX];
  else if (id == ATTITUDE_ID)
    return crossfireSensors[ATTITUDE_PITCH_INDEX + subId];
  else if (id == FLIGHT_MODE_ID)
    return crossfireSensors[FLIGHT_MODE_INDEX];
  else if (id == BARO_ALT_ID)
    return crossfireSensors[BARO_ALTITUDE_INDEX];
  else if (id == AIRSPEED_ID)
    return crossfireSensors[AIRSPEED_INDEX];
  else if (id == CF_RPM_ID)
    return crossfireSensors[subId == 1 ? CF_RPM2_INDEX : CF_RPM_INDEX];
  else if (id == TEMP_ID)
    return crossfireSensors[TEMP_INDEX];
  else if (id == CELLS_ID)
    return crossfireSensors[CELLS_INDEX];
  else if (id == VOLT_ARRAY_ID)
    return crossfireSensors[VOLT_ARRAY_INDEX];
  else
    return crossfireSensors[UNKNOWN_INDEX];
}

void processCrossfireTelemetryValue(uint8_t index, int32_t value)
{
  if (!TELEMETRY_STREAMING())
    return;

  const CrossfireSensor & sensor = crossfireSensors[index];
  setTelemetryValue(PROTOCOL_TELEMETRY_CROSSFIRE, sensor.id, 0, sensor.subId,
                    value, sensor.unit, sensor.precision);
}

int32_t getCrossfireTelemetryValue(uint8_t index, uint8_t* rxBuffer, uint8_t size, bool isSigned)
{
  uint8_t *byte = &rxBuffer[index];
  int32_t value = ((*byte & 0x80) && isSigned) ? -1 : 0;

  for (uint8_t i=0; i<size; i++) {
    value = (value << 8) | *byte++;
  }

  return value;
}

//-----------------------------------------------------------------------------
// ArduPilot passthrough telemetry (the data source of the yaapu Lua script)
//
// With CRSF custom telemetry (ArduPilot RC_OPTIONS bit 8) the native frames are
// throttled and the data arrives in the custom frames instead: 0x5006
// roll/pitch, 0x5005 yaw (0.2 deg) and vertical speed, 0x5002 satellites (4
// bits, 15 = "15 or more"), 0x5004 home bearing and altitude above home, and
// 0x500A the two RPM sensors. They feed the regular sensors, the home frame
// the HUD home marker and its altitude readout.
//-----------------------------------------------------------------------------
#define CRSF_AP_CUSTOM_TELEM_SINGLE_PACKET 0xF0
#define CRSF_AP_CUSTOM_TELEM_MULTI_PACKET  0xF2
#define CRSF_AP_PASSTHROUGH_ROLL_PITCH     0x5006
#define CRSF_AP_PASSTHROUGH_VEL_YAW        0x5005
#define CRSF_AP_PASSTHROUGH_HOME           0x5004
#define CRSF_AP_PASSTHROUGH_GPS_STATUS     0x5002
#define CRSF_AP_PASSTHROUGH_RPM            0x500A

// 0x5006 every 350ms, 0x5005 every 250-400ms, 0x5004 every 500ms, 0x5002 every
// 0.5-1s, 0x500A 300ms
#define AP_PASSTHROUGH_ATTITUDE_TIMEOUT_MS 1200
#define AP_PASSTHROUGH_HOME_TIMEOUT_MS     3000
#define AP_PASSTHROUGH_GPS_TIMEOUT_MS      3000
#define AP_PASSTHROUGH_VSPD_TIMEOUT_MS     3000

// Per module state of the passthrough packets: while fresh, each one keeps the
// slower native frame it replaces suppressed, and the home packet feeds the
// HUD home marker. They all age out, so the native frames take over again
static struct {
  uint32_t attitudeMs;
  bool     attitudeSeen;
  uint32_t homeMs;
  bool     homeSeen;
  uint16_t homeBearingDeg;
  uint32_t gpsMs;
  bool     gpsSeen;
  float    homeAltM;
  float    homeDistM;
  uint32_t vspdMs;
  bool     vspdSeen;
} apPassthrough[2];

// the native ATTITUDE frame carries milliradians, as UNIT_RADIANS with a
// precision of 3 expects
static int32_t attitudeSensorValue(float degrees)
{
  return (int32_t)lroundf(degrees * M_PI / 180.0f * 1000.0f);
}

static bool isArduPilotPassthroughAttitudeFresh(uint8_t module)
{
  return apPassthrough[module].attitudeSeen &&
         (time_get_ms() - apPassthrough[module].attitudeMs) <
             AP_PASSTHROUGH_ATTITUDE_TIMEOUT_MS;
}

static void setArduPilotPassthroughAttitudeSeen(uint8_t module)
{
  apPassthrough[module].attitudeMs = time_get_ms();
  apPassthrough[module].attitudeSeen = true;
}

// the passthrough satellite count wins while it is being received
static bool isArduPilotPassthroughGpsFresh(uint8_t module)
{
  return apPassthrough[module].gpsSeen &&
         (time_get_ms() - apPassthrough[module].gpsMs) <
             AP_PASSTHROUGH_GPS_TIMEOUT_MS;
}

// the passthrough vertical speed is sent faster than the native vario frames,
// which it replaces while it is being received
static bool isArduPilotPassthroughVSpeedFresh(uint8_t module)
{
  return apPassthrough[module].vspdSeen &&
         (time_get_ms() - apPassthrough[module].vspdMs) <
             AP_PASSTHROUGH_VSPD_TIMEOUT_MS;
}

static void processArduPilotPassthroughPacket(uint8_t module, uint16_t appId,
                                              uint32_t data)
{
  switch (appId) {
    case CRSF_AP_PASSTHROUGH_GPS_STATUS:
      // 4 bits of satellites (15 = "15 or more"), the rest is fix/HDOP/alt
      processCrossfireTelemetryValue(GPS_SATELLITES_INDEX,
                                     (int32_t)(data & 0x0F));
      apPassthrough[module].gpsMs = time_get_ms();
      apPassthrough[module].gpsSeen = true;
      break;

    case CRSF_AP_PASSTHROUGH_ROLL_PITCH:
      // 0.2 deg steps: roll [0,1800] -> [-180,180], pitch [0,900] -> [-90,90]
      processCrossfireTelemetryValue(
          ATTITUDE_ROLL_INDEX,
          attitudeSensorValue(((int32_t)(data & 0x7FF) - 900) * 0.2f));
      processCrossfireTelemetryValue(
          ATTITUDE_PITCH_INDEX,
          attitudeSensorValue(((int32_t)((data >> 11) & 0x3FF) - 450) * 0.2f));
      setArduPilotPassthroughAttitudeSeen(module);
      break;

    case CRSF_AP_PASSTHROUGH_VEL_YAW: {
      // vertical speed in dm/s: 7 bit mantissa, exponent bit (x10) and sign
      int32_t vspd = ((int32_t)data >> 1) & 0x7F;
      if (data & 0x01) vspd *= 10;
      if (data & 0x100) vspd = -vspd;
      // the sensor carries cm/s, as the native vario frame does
      processCrossfireTelemetryValue(VERTICAL_SPEED_INDEX, vspd * 10);
      apPassthrough[module].vspdMs = time_get_ms();
      apPassthrough[module].vspdSeen = true;

      // 0.2 deg steps: yaw [0,1800] -> [0,360], normalized to the +-180 range
      // used by the native ATTITUDE frame
      float yaw = ((int32_t)((data >> 17) & 0x7FF)) * 0.2f;
      if (yaw > 180.0f) yaw -= 360.0f;
      processCrossfireTelemetryValue(ATTITUDE_YAW_INDEX,
                                     attitudeSensorValue(yaw));
      setArduPilotPassthroughAttitudeSeen(module);
      break;
    }

    case CRSF_AP_PASSTHROUGH_HOME: {
      // home bearing in 3 deg steps at bits 25-31
      uint16_t bearing = ((data >> 25) & 0x7F) * 3;
      if (bearing >= 360) bearing -= 360;
      apPassthrough[module].homeBearingDeg = bearing;

      // distance to home in m: 10 bit mantissa at bits 2-11, 2 bit exponent
      // (x10^n) at bits 0-1
      int32_t homeDistM = (data >> 2) & 0x3FF;
      for (uint8_t i = data & 0x03; i > 0; i--) homeDistM *= 10;
      apPassthrough[module].homeDistM = homeDistM;

      // altitude above home in dm: 10 bit mantissa at bits 14-23, 2 bit
      // exponent at bits 12-13 and a sign bit at bit 24
      int32_t homeAltDm = (data >> 14) & 0x3FF;
      for (uint8_t i = (data >> 12) & 0x03; i > 0; i--) homeAltDm *= 10;
      if (data & 0x1000000) homeAltDm = -homeAltDm;
      apPassthrough[module].homeAltM = homeAltDm * 0.1f;

      apPassthrough[module].homeMs = time_get_ms();
      apPassthrough[module].homeSeen = true;
      break;
    }

    case CRSF_AP_PASSTHROUGH_RPM:
      // the vehicle sends 0.1 rpm as two signed 16 bit values, the sensors
      // carry whole rpm (as the yaapu script does: value * 10)
      processCrossfireTelemetryValue(
          CF_RPM_INDEX, (int32_t)(int16_t)(data & 0xFFFF) * 10);
      processCrossfireTelemetryValue(
          CF_RPM2_INDEX, (int32_t)(int16_t)((data >> 16) & 0xFFFF) * 10);
      break;

    default:
      break;
  }
}

// module with the freshest passthrough home frame, -1 if there is none
static int freshestHomeModule()
{
  int best = -1;
  uint32_t newest = 0;
  const uint32_t now = time_get_ms();

  for (uint8_t module = 0; module < 2; module++) {
    if (!apPassthrough[module].homeSeen ||
        (now - apPassthrough[module].homeMs) >= AP_PASSTHROUGH_HOME_TIMEOUT_MS) {
      continue;
    }
    if (best < 0 || (int32_t)(apPassthrough[module].homeMs - newest) > 0) {
      newest = apPassthrough[module].homeMs;
      best = module;
    }
  }

  return best;
}

int16_t getArduPilotHomeBearing()
{
  const int module = freshestHomeModule();
  return module < 0 ? -1 : apPassthrough[module].homeBearingDeg;
}

// Altitude above home in meters (or -100000 when no home frame has been
// received recently)
float getArduPilotHomeAltitude()
{
  const int module = freshestHomeModule();
  return module < 0 ? -100000.0f : apPassthrough[module].homeAltM;
}

// Distance to home in meters, from the same frame (-100000 while no home frame
// has been received recently)
float getArduPilotHomeDistance()
{
  const int module = freshestHomeModule();
  return module < 0 ? -100000.0f : apPassthrough[module].homeDistM;
}

static void processArduPilotCustomTelemFrame(uint8_t module, uint8_t payloadSize,
                                             const uint8_t* payload)
{
  if (payloadSize < 1) return;

  if (payload[0] == CRSF_AP_CUSTOM_TELEM_SINGLE_PACKET) {
    if (payloadSize < 7) return;
    uint32_t data = payload[6];
    data = (data << 8) | payload[5];
    data = (data << 8) | payload[4];
    data = (data << 8) | payload[3];
    processArduPilotPassthroughPacket(module, payload[1] | (payload[2] << 8),
                                      data);
  } else if (payload[0] == CRSF_AP_CUSTOM_TELEM_MULTI_PACKET) {
    if (payloadSize < 2) return;
    const uint8_t count = min<uint8_t>(payload[1], (payloadSize - 2) / 6);
    for (uint8_t i = 0; i < count; i++) {
      const uint8_t* packet = payload + 2 + i * 6;
      uint32_t data = packet[5];
      data = (data << 8) | packet[4];
      data = (data << 8) | packet[3];
      data = (data << 8) | packet[2];
      processArduPilotPassthroughPacket(module, packet[0] | (packet[1] << 8),
                                        data);
    }
  }
}

void processCrossfireTelemetryFrame(uint8_t module, uint8_t* rxBuffer,
                                    uint8_t rxBufferCount)
{
  if (telemetryState == TELEMETRY_INIT &&
      moduleState[module].counter != CRSF_FRAME_MODELID_SENT) {
    moduleState[module].counter = CRSF_FRAME_MODELID;
  }

  uint8_t crsfPayloadLen = rxBuffer[1];
  uint8_t id = rxBuffer[2];
  int32_t value;
  switch(id) {
    case CF_VARIO_ID:
      // the passthrough packet wins while it is fresh, see
      // processArduPilotPassthroughPacket()
      if (!isArduPilotPassthroughVSpeedFresh(module)) {
        processCrossfireTelemetryValue(VERTICAL_SPEED_INDEX,
          getCrossfireTelemetryValue(3, rxBuffer, 2, true));
      }
      break;

    case GPS_ID:
      processCrossfireTelemetryValue(GPS_LATITUDE_INDEX,
        getCrossfireTelemetryValue(3, rxBuffer, 4, true)/10);
      processCrossfireTelemetryValue(GPS_LONGITUDE_INDEX,
        getCrossfireTelemetryValue(7, rxBuffer, 4, true)/10);
      processCrossfireTelemetryValue(GPS_GROUND_SPEED_INDEX,
        getCrossfireTelemetryValue(11, rxBuffer, 2, false));
      processCrossfireTelemetryValue(GPS_HEADING_INDEX,
        getCrossfireTelemetryValue(13, rxBuffer, 2, false));
      processCrossfireTelemetryValue(GPS_ALTITUDE_INDEX,
        getCrossfireTelemetryValue(15, rxBuffer, 2, false) - 1000);
      // the passthrough packet wins while it is fresh, see
      // processArduPilotPassthroughPacket()
      if (!isArduPilotPassthroughGpsFresh(module)) {
        processCrossfireTelemetryValue(GPS_SATELLITES_INDEX,
          getCrossfireTelemetryValue(17, rxBuffer, 1, false));
      }
      break;

    case GPS_TIME_ID:
    {
      // Payload: year (2B BE), month, day, hour, min, sec, millisecond (2B BE)
      const CrossfireSensor & sensor = crossfireSensors[GPS_TIME_INDEX];
      value = getCrossfireTelemetryValue(3, rxBuffer, 2, false);
      uint8_t year = (uint8_t)((uint16_t)value - 2000);
      uint8_t month  = rxBuffer[5];
      uint8_t day    = rxBuffer[6];
      uint8_t hour   = rxBuffer[7];
      uint8_t minute = rxBuffer[8];
      uint8_t sec    = rxBuffer[9];
      // Date record: low byte non-zero (acts as date/time discriminator)
      uint32_t dateVal = ((uint32_t)year << 24) | ((uint32_t)month << 16)
                       | ((uint32_t)day << 8) | 0xFF;
      setTelemetryValue(PROTOCOL_TELEMETRY_CROSSFIRE, sensor.id, 0, sensor.subId,
                        (int32_t)dateVal, UNIT_DATETIME, 0);
      // Time record: low byte zero
      uint32_t timeVal = ((uint32_t)hour << 24) | ((uint32_t)minute << 16)
                       | ((uint32_t)sec << 8);
      setTelemetryValue(PROTOCOL_TELEMETRY_CROSSFIRE, sensor.id, 0, sensor.subId,
                        (int32_t)timeVal, UNIT_DATETIME, 0);
      break;
    }

    case BARO_ALT_ID:
      value = getCrossfireTelemetryValue(3, rxBuffer, 2, false);
      if (value & 0x8000) {
        // Altitude in meters
        value &= ~(0x8000);
        value *= 100; // cm
      } else {
        // Altitude in decimeters + 10000dm
        value -= 10000;
        value *= 10;
      }
      processCrossfireTelemetryValue(BARO_ALTITUDE_INDEX, value);

      // Length of TBS BARO_ALT has 4 payload bytes with just 2 bytes of altitude
      // but support including TBS VARIO if the declared payload length is 5 bytes
      if (!isArduPilotPassthroughVSpeedFresh(module)) {
        if (crsfPayloadLen == 5) {
          constexpr int Kl = 100;       // linearity constant;
          constexpr float Kr = .026;    // range constant;

          value = getCrossfireTelemetryValue(5, rxBuffer, 1, true);
          int8_t sign = value < 0 ? -1 : 1;
          value =((expf(value * sign * Kr) - 1) * Kl) * sign;
          processCrossfireTelemetryValue(VERTICAL_SPEED_INDEX, value);
        }

        // Length of TBS BARO_ALT has 4 payload bytes with just 2 bytes of altitude
        // but support including ELRS VARIO if the declared payload length is 6 bytes or more
        if (crsfPayloadLen > 5)
          processCrossfireTelemetryValue(VERTICAL_SPEED_INDEX,
            getCrossfireTelemetryValue(5, rxBuffer, 2, true));
      }
      break;

    case AIRSPEED_ID:
        // Airspeed in 0.1 * km/h (hectometers/h)
        // Converstion to KMH is done through PREC1
        processCrossfireTelemetryValue(AIRSPEED_INDEX,
          getCrossfireTelemetryValue(3, rxBuffer, 2, false));
      break;

    case CF_RPM_ID:
    {
      uint8_t sensorID = getCrossfireTelemetryValue(3, rxBuffer, 1, false);
      for(uint8_t i = 0; i * 3 < (crsfPayloadLen - 4);  i++) {
        value = getCrossfireTelemetryValue(4 + i * 3, rxBuffer, 3, true);
        const CrossfireSensor & sensor = crossfireSensors[CF_RPM_INDEX];
        setTelemetryValue(PROTOCOL_TELEMETRY_CROSSFIRE, sensor.id + (sensorID << 8), 0, i,
                          value, sensor.unit, sensor.precision);
      }
      break;
    }

    case TEMP_ID:
    {
      uint8_t sensorID = getCrossfireTelemetryValue(3, rxBuffer, 1, false);
      for(uint8_t i = 0; i * 2 < (crsfPayloadLen - 4);  i++) {
        value = getCrossfireTelemetryValue(4 + i * 2, rxBuffer, 2, true);
        const CrossfireSensor & sensor = crossfireSensors[TEMP_INDEX];
        setTelemetryValue(PROTOCOL_TELEMETRY_CROSSFIRE, sensor.id + (sensorID << 8), 0, i,
                          value, sensor.unit, sensor.precision);
      }
      break;
    }

    case CELLS_ID:
    {
      uint8_t sensorID = getCrossfireTelemetryValue(3, rxBuffer, 1, false);

      if (sensorID < 128) {
        // Treating frame as Cells sensor
        // We can handle only up to 8 cells
        for(uint8_t i = 0; i * 2 < min(16, crsfPayloadLen - 4);  i++) {
          value = getCrossfireTelemetryValue(4 + i * 2, rxBuffer, 2, false);
          const CrossfireSensor & sensor = crossfireSensors[CELLS_INDEX];
          setTelemetryValue(PROTOCOL_TELEMETRY_CROSSFIRE, sensor.id + (sensorID << 8), 0, 0,
                          i << 16 | value / 10, sensor.unit, sensor.precision);
        }
      } else {
        // Treating frame as Voltage sensor array
        for(uint8_t i = 0; i * 2 < (crsfPayloadLen - 4);  i++) {
          value = getCrossfireTelemetryValue(4 + i * 2, rxBuffer, 2, false);
          const CrossfireSensor & sensor = crossfireSensors[VOLT_ARRAY_INDEX];
          setTelemetryValue(PROTOCOL_TELEMETRY_CROSSFIRE, sensor.id + (sensorID << 8), 0, i,
                                    value / 10, sensor.unit, sensor.precision);
        }
      }
      break;
    }

    case LINK_ID:
      for (unsigned int i=0; i<=TX_SNR_INDEX; i++) {
        value = getCrossfireTelemetryValue(3+i, rxBuffer, 1, true);
        if (i == TX_POWER_INDEX) {
          static const int32_t power_values[] = {0,    10,   25,  100, 500,
                                                  1000, 2000, 250, 50};
          value =
              ((unsigned)value < DIM(power_values) ? power_values[value] : 0);
        } else if (i == RX_ANTENNA_INDEX &&
                   crossfireModuleStatus[module].isELRS) {
          value += 1;  // 0 = Antenna 1, 1 = Antenna 2
        }
        processCrossfireTelemetryValue(i, value);
        if (i == RX_QUALITY_INDEX) {
          if (value) {
            telemetryData.rssi.set(value);
            telemetryStreaming = TELEMETRY_TIMEOUT10ms;
            telemetryData.telemetryValid |= 1 << module;
          }
          else {
            if (telemetryData.telemetryValid & (1 << module)) {
              telemetryData.rssi.reset();
              telemetryStreaming = 0;
            }
            telemetryData.telemetryValid &= ~(1 << module);
          }
        }
      }
      break;

    case CHANNELS_ID:
      if (g_model.trainerData.mode == TRAINER_MODE_CRSF) {
        uint8_t inputbitsavailable = 0;
        uint32_t inputbits = 0;
        uint8_t  byteIdx = 3;
        int16_t *pulses = trainerInput;

        for (int i = 0; i < min(CROSSFIRE_CHANNELS_COUNT, MAX_TRAINER_CHANNELS); i++) {
          while (inputbitsavailable < CROSSFIRE_CH_BITS) {
            inputbits |= (uint32_t)(rxBuffer[byteIdx++]) << inputbitsavailable;
            inputbitsavailable += 8;
          }
          *pulses++ = ((int32_t)(inputbits & CROSSFIRE_CH_MASK) - CROSSFIRE_CH_CENTER) * 5 / 8;
          inputbitsavailable -= CROSSFIRE_CH_BITS;
          inputbits >>= CROSSFIRE_CH_BITS;
        }

        trainerResetTimer();
      }
      break;

    case LINK_RX_ID:
      processCrossfireTelemetryValue(RX_RSSI_PERC_INDEX,
        getCrossfireTelemetryValue(4, rxBuffer, 1, false));
      processCrossfireTelemetryValue(TX_RF_POWER_INDEX,
        getCrossfireTelemetryValue(7, rxBuffer, 1, false));
      break;

    case LINK_TX_ID:
      processCrossfireTelemetryValue(TX_RSSI_PERC_INDEX,
        getCrossfireTelemetryValue(4, rxBuffer, 1, false));
      processCrossfireTelemetryValue(RX_RF_POWER_INDEX,
        getCrossfireTelemetryValue(7, rxBuffer, 1, false));
      processCrossfireTelemetryValue(TX_FPS_INDEX,
        getCrossfireTelemetryValue(8, rxBuffer, 1, false) * 10);
      break;

    case BATTERY_ID:
      processCrossfireTelemetryValue(BATT_VOLTAGE_INDEX,
        getCrossfireTelemetryValue(3, rxBuffer, 2, true));
      processCrossfireTelemetryValue(BATT_CURRENT_INDEX,
        getCrossfireTelemetryValue(5, rxBuffer, 2, true));
      processCrossfireTelemetryValue(BATT_CAPACITY_INDEX,
        getCrossfireTelemetryValue(7, rxBuffer, 3, false));
      processCrossfireTelemetryValue(BATT_REMAINING_INDEX,
        getCrossfireTelemetryValue(10, rxBuffer, 1, false));
      break;

    case ATTITUDE_ID:
      // while the passthrough attitude packets are being received they are the
      // faster source, see processArduPilotPassthroughPacket()
      if (isArduPilotPassthroughAttitudeFresh(module)) break;
      processCrossfireTelemetryValue(ATTITUDE_PITCH_INDEX,
        getCrossfireTelemetryValue(3, rxBuffer, 2, true)/10);
      processCrossfireTelemetryValue(ATTITUDE_ROLL_INDEX,
        getCrossfireTelemetryValue(5, rxBuffer, 2, true)/10);
      processCrossfireTelemetryValue(ATTITUDE_YAW_INDEX,
        getCrossfireTelemetryValue(7, rxBuffer, 2, true)/10);
      break;

    case FLIGHT_MODE_ID:
    {
      const CrossfireSensor & sensor = crossfireSensors[FLIGHT_MODE_INDEX];
      auto textLength = min<int>(16, rxBuffer[1]);
      rxBuffer[textLength] = '\0';
      setTelemetryText(PROTOCOL_TELEMETRY_CROSSFIRE, sensor.id, 0, sensor.subId,
                       (const char *)rxBuffer + 3);
      break;
    }

    case RADIO_ID:
      if (rxBuffer[3] == 0xEA     // radio address
          && rxBuffer[5] == 0x10  // timing correction frame
      ) {
        // values are in 10th of micro-seconds
        uint32_t update_interval = getCrossfireTelemetryValue(6, rxBuffer, 4, false) / 10;
        int32_t offset = getCrossfireTelemetryValue(10, rxBuffer, 4, true) / 10;
        //TRACE("[XF] Rate: %d, Lag: %d", update_interval, offset);
        getModuleSyncStatus(module).update(update_interval, offset);
      }
      break;

    case AP_CUSTOM_TELEM_ID:
    case AP_CUSTOM_TELEM_LEGACY_ID:
      processArduPilotCustomTelemFrame(
          module, crsfPayloadLen > 2 ? crsfPayloadLen - 2 : 0, rxBuffer + 3);
#if defined(LUA)
      // Lua scripts (yaapu and friends) get the raw frame
      pushTelemetryDataToQueues(rxBuffer + 1, rxBufferCount - 2);
#endif
      break;

#if defined(LUA)
    default:
      if (id == DEVICE_INFO_ID && rxBuffer[4]== MODULE_ADDRESS) {
        uint8_t nameSize = rxBuffer[1] - 18;
        strncpy((char *)&crossfireModuleStatus[module].name, (const char *)&rxBuffer[5], CRSF_NAME_MAXSIZE);
        crossfireModuleStatus[module].name[CRSF_NAME_MAXSIZE -1] = 0; // For some reason, GH din't like strlcpy
        if (strncmp((const char *) &rxBuffer[5 + nameSize], "ELRS", 4) == 0)
          crossfireModuleStatus[module].isELRS = true;
        crossfireModuleStatus[module].major = rxBuffer[14 + nameSize];
        crossfireModuleStatus[module].minor = rxBuffer[15 + nameSize];
        crossfireModuleStatus[module].revision = rxBuffer[16 + nameSize];

        ModuleData *md = &g_model.moduleData[module];

        if(!CRSF_ELRS_MIN_VER(module, 4, 0) &&
           (md->crsf.crsfArmingMode != ARMING_MODE_CH5 || md->crsf.crsfArmingMode != SWSRC_NONE)) {
          md->crsf.crsfArmingMode = ARMING_MODE_CH5;
          md->crsf.crsfArmingTrigger = SWSRC_NONE;

          storageDirty(EE_MODEL);
        }

        crossfireModuleStatus[module].queryCompleted = true;
      }

      // destination address and CRC are skipped
      pushTelemetryDataToQueues(rxBuffer + 1, rxBufferCount - 2);
      break;
#endif
  }
}

void crossfireSetDefault(int index, uint16_t id, uint8_t subId)
{
  TelemetrySensor & telemetrySensor = g_model.telemetrySensors[index];

  telemetrySensor.id = id;
  telemetrySensor.instance = subId;

  const CrossfireSensor & sensor = getCrossfireSensor(id, subId);
  TelemetryUnit unit = sensor.unit;
  if (unit == UNIT_GPS_LATITUDE || unit == UNIT_GPS_LONGITUDE)
    unit = UNIT_GPS;
  uint8_t prec = min<uint8_t>(2, sensor.precision);
  telemetrySensor.init(sensor.name, unit, prec);
  if (id == LINK_ID) {
    telemetrySensor.logs = true;
  }

  storageDirty(EE_MODEL);
}
