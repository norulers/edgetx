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

#pragma once

#include <stdint.h>

//
// Generic, reflection based access to the radio and model settings.
//
// The firmware already describes every stored field in the YAML node tables
// (name, type, bit width, array bounds, enum value names) and can read / write
// them with the very same code that loads and saves the SD card files. This
// module reuses that machinery to address settings by path, which is what
// allows an external application to configure everything the on-radio screens
// can configure without one bespoke command per field.
//
// A path is a '/'-separated list of field names, where an array element is
// addressed by putting its index directly after the field name:
//
//   "timers/0/name"            -> g_model.timers[0].name
//   "mixData/3/weight"         -> g_model.mixData[3].weight
//   "moduleData/1/multi/rfProtocol"
//
// Values use the exact textual representation of the storage YAML files, so
// enums are written by name (e.g. `SA`, `TELEMETRY_MIRROR`, `ADD`) and are
// therefore self-describing for the host.
//

#define ST_ROOT_RADIO           0   // g_eeGeneral (RadioData)
#define ST_ROOT_MODEL           1   // g_model (ModelData)

enum StStatus {
  ST_OK = 0,
  ST_ERR_ROOT,
  ST_ERR_NOT_FOUND,
  ST_ERR_TYPE,
  ST_ERR_RANGE,
  ST_ERR_LEN,
};

// One child of a container, as reported by stList()
struct StEntry {
  char     tag[20];
  uint8_t  type;   // YamlDataType
  uint16_t size;   // bits, size of one element for arrays
  uint16_t elmts;  // 1 for structs/unions, > 1 for real arrays
};

// List the children of the container at `path` (or of the root when the path
// is empty). At most `maxEntries` are returned, `totalEntries` always reports
// the full count so the host can page.
int stList(uint8_t root, const char* path, uint8_t pathLen, uint8_t startIdx,
           StEntry* entries, uint8_t maxEntries, uint8_t* totalEntries);

// Read the value at `path` as text. The writer collects the whole value while
// copying at most `outSize` bytes starting at `offset`; `totalLen` returns the
// full length and `more` is set when the value did not fit.
int stGet(uint8_t root, const char* path, uint8_t pathLen, uint16_t offset,
          char* out, uint16_t outSize, uint16_t* totalLen, bool* more);

// Write the value at `path` from its textual representation. The value is
// validated against the field type and bit width before it is applied.
int stSet(uint8_t root, const char* path, uint8_t pathLen,
          const char* value, uint16_t valueLen);

// Emit the whole settings document (the same text the storage layer would
// write to a YAML file) starting at byte `offset`. Useful for a one shot dump
// of the entire configuration; the host pages through it with `offset`.
int stExport(uint8_t root, uint16_t offset, char* out, uint16_t outSize,
             uint16_t* totalLen, bool* more);

// True when the path ends with an array index; such a path can be listed but
// does not denote a single value.
bool stPathEndsWithIndex(const char* path, uint8_t pathLen);
