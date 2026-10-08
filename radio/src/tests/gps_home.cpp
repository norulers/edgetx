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

#include "gtests.h"

// Coordinates are degrees * 1000000, the scale of a GPS telemetry sensor
static constexpr int32_t lat50 = 50000000;
static constexpr int32_t lon14 = 14000000;

// The telemetry code keeps the first position a GPS sensor reported as its
// "pilot" position, which is the home point of a vehicle that sends no home
// frame of its own (iNav)
static void setGpsPosition(int32_t lat, int32_t lon)
{
  auto& sensor = g_model.telemetrySensors[0];
  telemetryItems[0].setValue(sensor, lat, UNIT_GPS_LATITUDE);
  telemetryItems[0].setValue(sensor, lon, UNIT_GPS_LONGITUDE);
}

TEST(GpsHome, UnknownWithoutGpsSensor)
{
  MODEL_RESET();
  TELEMETRY_RESET();

  EXPECT_FLOAT_EQ(getGpsHomeDistance(), -100000.0f);
  EXPECT_EQ(getGpsHomeBearing(), -1);
}

TEST(GpsHome, UnknownWithoutPosition)
{
  MODEL_RESET();
  TELEMETRY_RESET();
  g_model.telemetrySensors[0].init("GPS", UNIT_GPS, 0);

  EXPECT_FLOAT_EQ(getGpsHomeDistance(), -100000.0f);
  EXPECT_EQ(getGpsHomeBearing(), -1);
}

// A sensor that is not a position must not be mistaken for the model's GPS
TEST(GpsHome, IgnoresOtherSensors)
{
  MODEL_RESET();
  TELEMETRY_RESET();
  g_model.telemetrySensors[0].init("RxBt", UNIT_VOLTS, 1);
  telemetryItems[0].setValue(g_model.telemetrySensors[0], 50, UNIT_VOLTS, 1);

  EXPECT_FLOAT_EQ(getGpsHomeDistance(), -100000.0f);
  EXPECT_EQ(getGpsHomeBearing(), -1);
}

TEST(GpsHome, HomeIsTheFirstPosition)
{
  MODEL_RESET();
  TELEMETRY_RESET();
  g_model.telemetrySensors[0].init("GPS", UNIT_GPS, 0);

  // the first position is the home point
  setGpsPosition(lat50, lon14);

  // home is 0.001 deg north of the model: 111.19 m straight ahead
  setGpsPosition(lat50 - 1000, lon14);
  EXPECT_NEAR(getGpsHomeDistance(), 111.2f, 0.5f);
  EXPECT_EQ(getGpsHomeBearing(), 0);

  // home is 0.001 deg east of the model at 50 deg N: 71.5 m to the right
  setGpsPosition(lat50, lon14 - 1000);
  EXPECT_NEAR(getGpsHomeDistance(), 71.6f, 0.5f);
  EXPECT_EQ(getGpsHomeBearing(), 90);

  // and back at home
  setGpsPosition(lat50, lon14);
  EXPECT_NEAR(getGpsHomeDistance(), 0.0f, 0.5f);
}
