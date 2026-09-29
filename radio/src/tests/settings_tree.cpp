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

//
// Tests for the reflection based settings access used by the App Config
// protocol (radio/src/settings_tree.cpp).
//
// These run on the native platform, so they exercise the very same YAML node
// tables and walker the firmware uses on the radio.
//

#include <gtest/gtest.h>

#include <string>
#include <string.h>

#include "settings_tree.h"
#include "myeeprom.h"

namespace {

std::string get(uint8_t root, const char* path)
{
  char buf[256];
  uint16_t total = 0;
  bool more = false;
  int st = stGet(root, path, (uint8_t)strlen(path), 0, buf, sizeof(buf) - 1,
                 &total, &more);
  if (st != ST_OK) return "<err " + std::to_string(st) + ">";
  if (more) return "<too long>";
  buf[total] = '\0';
  return std::string(buf);
}

bool set(uint8_t root, const char* path, const char* value)
{
  return stSet(root, path, (uint8_t)strlen(path), value,
               (uint16_t)strlen(value)) == ST_OK;
}

int setStatus(uint8_t root, const char* path, const char* value)
{
  return stSet(root, path, (uint8_t)strlen(path), value,
               (uint16_t)strlen(value));
}

}  // namespace

TEST(SettingsTree, GetString)
{
  strcpy(g_model.header.name, "TESTMDL");
  EXPECT_EQ(get(ST_ROOT_MODEL, "header/name"), "TESTMDL");
}

TEST(SettingsTree, GetSignedNumber)
{
  g_model.mixData[0].weight = makeSourceNumVal(75);
  EXPECT_EQ(get(ST_ROOT_MODEL, "mixData/0/weight"), "75");

  g_model.mixData[0].weight = makeSourceNumVal(-40);
  EXPECT_EQ(get(ST_ROOT_MODEL, "mixData/0/weight"), "-40");
}

TEST(SettingsTree, GetUnsignedNumber)
{
  g_model.mixData[0].delayUp = 123;
  EXPECT_EQ(get(ST_ROOT_MODEL, "mixData/0/delayUp"), "123");
}

TEST(SettingsTree, ArrayIndexSelectsTheRightElement)
{
  g_model.mixData[1].delayUp = 11;
  g_model.mixData[2].delayUp = 22;
  EXPECT_EQ(get(ST_ROOT_MODEL, "mixData/1/delayUp"), "11");
  EXPECT_EQ(get(ST_ROOT_MODEL, "mixData/2/delayUp"), "22");
}

TEST(SettingsTree, NestedStructAndArray)
{
  strcpy(g_model.timers[0].name, "T1");
  memset(g_model.timers[1].name, 0, sizeof(g_model.timers[1].name));
  strcpy(g_model.timers[1].name, "T2");

  EXPECT_EQ(get(ST_ROOT_MODEL, "timers/0/name"), "T1");
  EXPECT_EQ(get(ST_ROOT_MODEL, "timers/1/name"), "T2");
  EXPECT_EQ(get(ST_ROOT_MODEL, "flightModeData/1/fadeIn"),
            std::to_string(g_model.flightModeData[1].fadeIn));
}

TEST(SettingsTree, RadioRoot)
{
  g_eeGeneral.beepVolume = 1;
  EXPECT_EQ(get(ST_ROOT_RADIO, "beepVolume"),
            std::to_string(g_eeGeneral.beepVolume + 2));
}

TEST(SettingsTree, SetRoundTrip)
{
  ASSERT_TRUE(set(ST_ROOT_MODEL, "mixData/3/delayDown", "42"));
  EXPECT_EQ(g_model.mixData[3].delayDown, 42);
  EXPECT_EQ(get(ST_ROOT_MODEL, "mixData/3/delayDown"), "42");
}

TEST(SettingsTree, SetStringTruncationIsAReject)
{
  // a name longer than the field has to be rejected, not silently truncated
  std::string too_long(sizeof(g_model.timers[0].name) + 4, 'x');
  EXPECT_EQ(setStatus(ST_ROOT_MODEL, "timers/0/name", too_long.c_str()),
            ST_ERR_LEN);

  // a fitting name is accepted
  ASSERT_TRUE(set(ST_ROOT_MODEL, "timers/0/name", "AB"));
  EXPECT_STREQ(g_model.timers[0].name, "AB");
}

TEST(SettingsTree, UnsignedOutOfRangeIsRejected)
{
  g_model.mixData[4].delayUp = 7;
  EXPECT_EQ(setStatus(ST_ROOT_MODEL, "mixData/4/delayUp", "300"), ST_ERR_RANGE);
  // and the stored value is untouched
  EXPECT_EQ(g_model.mixData[4].delayUp, 7);
}

TEST(SettingsTree, SignedOutOfRangeIsRejected)
{
  // flightModeData[].fadeIn is an 8 bit unsigned field
  uint8_t before = g_model.flightModeData[2].fadeIn;
  EXPECT_EQ(setStatus(ST_ROOT_MODEL, "flightModeData/2/fadeIn", "3000"),
            ST_ERR_RANGE);
  EXPECT_EQ(g_model.flightModeData[2].fadeIn, before);
}

TEST(SettingsTree, BadPaths)
{
  EXPECT_EQ(stGet(ST_ROOT_MODEL, "doesNotExist", 12, 0, nullptr, 0, nullptr,
                  nullptr), ST_ERR_NOT_FOUND);
  EXPECT_EQ(stGet(ST_ROOT_MODEL, "timers/99/name", 14, 0, nullptr, 0, nullptr,
                  nullptr), ST_ERR_RANGE);
  EXPECT_EQ(stGet(42, "", 0, 0, nullptr, 0, nullptr, nullptr), ST_ERR_ROOT);
}

TEST(SettingsTree, ContainersAreNotWritable)
{
  EXPECT_EQ(setStatus(ST_ROOT_MODEL, "header", "x"), ST_ERR_TYPE);
  EXPECT_EQ(setStatus(ST_ROOT_MODEL, "mixData", "x"), ST_ERR_TYPE);
}

TEST(SettingsTree, ListRoot)
{
  StEntry entries[32];
  uint8_t total = 0;
  ASSERT_EQ(stList(ST_ROOT_MODEL, "", 0, 0, entries, 32, &total), ST_OK);
  ASSERT_GT(total, 0);

  uint8_t count = total > 32 ? 32 : total;

  bool foundHeader = false;
  bool foundMix = false;
  bool foundTimers = false;
  for (uint8_t i = 0; i < count; i++) {
    if (!strcmp(entries[i].tag, "header")) foundHeader = true;
    if (!strcmp(entries[i].tag, "mixData")) foundMix = true;
    if (!strcmp(entries[i].tag, "timers")) foundTimers = true;
  }
  EXPECT_TRUE(foundHeader);
  EXPECT_TRUE(foundMix);
  EXPECT_TRUE(foundTimers);
}

TEST(SettingsTree, ListArrayElement)
{
  StEntry entries[32];
  uint8_t total = 0;
  ASSERT_EQ(stList(ST_ROOT_MODEL, "mixData/0", 8, 0, entries, 32, &total), ST_OK);

  uint8_t count = total > 32 ? 32 : total;

  bool foundWeight = false;
  for (uint8_t i = 0; i < count; i++) {
    if (!strcmp(entries[i].tag, "weight")) foundWeight = true;
  }
  EXPECT_TRUE(foundWeight);
}

TEST(SettingsTree, ListPaging)
{
  StEntry all[64];
  uint8_t total = 0;
  ASSERT_EQ(stList(ST_ROOT_MODEL, "", 0, 0, all, 64, &total), ST_OK);
  ASSERT_GT(total, 2);

  StEntry page[2];
  uint8_t total2 = 0;
  ASSERT_EQ(stList(ST_ROOT_MODEL, "", 0, 0, page, 2, &total2), ST_OK);
  EXPECT_EQ(total2, total);
  EXPECT_STREQ(page[0].tag, all[0].tag);
  EXPECT_STREQ(page[1].tag, all[1].tag);

  ASSERT_EQ(stList(ST_ROOT_MODEL, "", 0, 1, page, 2, &total2), ST_OK);
  EXPECT_EQ(total2, total);
  EXPECT_STREQ(page[0].tag, all[1].tag);
  EXPECT_STREQ(page[1].tag, all[2].tag);
}

TEST(SettingsTree, PathEndsWithIndex)
{
  EXPECT_TRUE(stPathEndsWithIndex("timers/0", 8));
  EXPECT_TRUE(stPathEndsWithIndex("timers/0/", 9));
  EXPECT_FALSE(stPathEndsWithIndex("timers/0/name", 13));
  EXPECT_FALSE(stPathEndsWithIndex("header/name", 11));
}

TEST(SettingsTree, ExportContainsKnownFields)
{
  strcpy(g_model.header.name, "EXPORTME");
  g_model.mixData[0].delayUp = 33;

  std::string dump;
  uint16_t offset = 0;
  for (int i = 0; i < 1000; i++) {
    char buf[128];
    uint16_t total = 0;
    bool more = false;
    ASSERT_EQ(stExport(ST_ROOT_MODEL, offset, buf, sizeof(buf), &total, &more),
              ST_OK);
    uint16_t chunk = (total > offset) ? (uint16_t)(total - offset) : 0;
    if (chunk > sizeof(buf)) chunk = sizeof(buf);
    dump.append(buf, chunk);
    if (!more) break;
    offset = (uint16_t)(offset + chunk);
  }

  EXPECT_NE(dump.find("EXPORTME"), std::string::npos);
  EXPECT_NE(dump.find("delayUp"), std::string::npos);
  EXPECT_NE(dump.find("mixData"), std::string::npos);
}
