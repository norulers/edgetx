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

#include "settings_tree.h"

#if !defined(BOOT)

#include "myeeprom.h"
#include "storage/yaml/yaml_tree_walker.h"
#include "storage/yaml/yaml_datastructs.h"
#include "storage/yaml/yaml_bits.h"

#include <string.h>

static const YamlNode* stRootNodes(uint8_t root)
{
  switch (root) {
  case ST_ROOT_RADIO: return get_radiodata_nodes();
  case ST_ROOT_MODEL: return get_modeldata_nodes();
  default:            return nullptr;
  }
}

static uint8_t* stRootData(uint8_t root)
{
  switch (root) {
  case ST_ROOT_RADIO: return (uint8_t*)&g_eeGeneral;
  case ST_ROOT_MODEL: return (uint8_t*)&g_model;
  default:            return nullptr;
  }
}

static bool stIsDigit(char c) { return c >= '0' && c <= '9'; }

// Find a field by tag among the attributes of the current node.
//
// YamlTreeWalker::findNode() cannot be used here: inside an array element it
// treats the first attribute as an index whenever that attribute has the
// YDT_IDX type, and would then interpret a field name such as "name" as an
// index value. Scanning the attributes directly avoids that ambiguity.
static bool stFindAttr(YamlTreeWalker& w, const char* tag, uint8_t tagLen)
{
  // The walker is always positioned on the first attribute of the current
  // node when a path component is entered (push()/rewind() reset it, and the
  // array index handling leaves it on the first real field).
  const YamlNode* attr = w.getAttr();
  while (attr && attr->type != YDT_NONE) {
    if (attr->tag && attr->tag_len() == tagLen &&
        !strncmp(tag, attr->tag, tagLen)) {
      return true;
    }
    w.toNextAttr();
    attr = w.getAttr();
  }
  return false;
}

bool stPathEndsWithIndex(const char* path, uint8_t pathLen)
{
  if (!path || pathLen == 0) return false;
  uint8_t i = pathLen;
  while (i > 0 && path[i - 1] == '/') i--;
  if (i == 0) return false;

  uint8_t end = i;
  while (i > 0 && path[i - 1] != '/') i--;
  if (i >= end) return false;

  for (uint8_t j = i; j < end; j++) {
    if (!stIsDigit(path[j])) return false;
  }
  return true;
}

// Walk `path` starting from the root. When `descendLast` is false the walker
// is left with the last field as the current attribute (ready for a value read
// or write); when true the walker is left *inside* that field (ready to
// enumerate its children).
static int stResolve(YamlTreeWalker& w, const char* path, uint8_t pathLen,
                     bool descendLast)
{
  const char* p = path;
  const char* end = path + pathLen;

  while (p < end) {
    while (p < end && *p == '/') p++;
    if (p >= end) break;

    const char* tok = p;
    while (p < end && *p != '/') p++;
    uint8_t tokLen = (uint8_t)(p - tok);
    if (tokLen == 0) continue;

    if (!stFindAttr(w, tok, tokLen)) return ST_ERR_NOT_FOUND;

    // An array element is addressed by putting the index right after the
    // field name, e.g. "timers/0". Peek at the next token.
    const char* mark = p;
    while (p < end && *p == '/') p++;
    uint16_t idx = 0;
    bool isIndex = (p < end);

    while (p < end && *p != '/') {
      if (!stIsDigit(*p)) { isIndex = false; break; }
      idx = (uint16_t)(idx * 10 + (uint16_t)(*p - '0'));
      p++;
    }
    if (isIndex && p < end && *p != '/') isIndex = false;

    if (isIndex) {
      const YamlNode* attr = w.getAttr();
      if (!attr || attr->type != YDT_ARRAY || attr->elmts <= 1) {
        return ST_ERR_TYPE;
      }
      if (idx >= attr->elmts) return ST_ERR_RANGE;

      if (!w.toChild()) return ST_ERR_NOT_FOUND;
      for (uint16_t i = 0; i < idx; i++) {
        if (!w.toNextElmt()) return ST_ERR_RANGE;
      }
      // Some arrays carry an index attribute as their first member. Elements
      // are addressed by position here, so step over it: otherwise findNode()
      // would mistake the next field name for an index value.
      if (w.getAttr() && w.getAttr()->type == YDT_IDX) w.toNextAttr();
      // now sitting on the first field of element `idx`
      if (p >= end) return ST_OK;
      continue;
    }

    p = mark;

    if (p >= end) {
      if (descendLast && !w.toChild()) return ST_ERR_NOT_FOUND;
      return ST_OK;
    }

    if (!w.toChild()) return ST_ERR_NOT_FOUND;
  }

  return ST_OK;
}

// --- value writer -----------------------------------------------------------

struct StWriteCtx {
  char*    buf;
  uint16_t size;
  uint16_t written;
  uint16_t offset;
  uint16_t total;
};

static bool stCollect(void* opaque, const char* str, size_t len)
{
  auto ctx = (StWriteCtx*)opaque;
  for (size_t i = 0; i < len; i++) {
    if (ctx->total >= ctx->offset && ctx->written < ctx->size) {
      ctx->buf[ctx->written++] = str[i];
    }
    ctx->total++;
  }
  return true;
}

int stGet(uint8_t root, const char* path, uint8_t pathLen, uint16_t offset,
          char* out, uint16_t outSize, uint16_t* totalLen, bool* more)
{
  const YamlNode* nodes = stRootNodes(root);
  uint8_t* data = stRootData(root);
  if (!nodes || !data) return ST_ERR_ROOT;

  YamlTreeWalker w;
  w.reset(nodes, data);

  int err = stResolve(w, path, pathLen, false);
  if (err != ST_OK) return err;

  if (!w.getAttr() || w.getAttr()->type == YDT_NONE) return ST_ERR_NOT_FOUND;

  // The writer emits a complete YAML attribute ("tag: value\r\n"); the host
  // works with the bare value, so the decoration is stripped here.
  char raw[128];
  StWriteCtx rctx = { raw, sizeof(raw) - 1, 0, 0, 0 };
  if (!w.outputCurrentAttr(stCollect, &rctx)) return ST_ERR_TYPE;
  raw[rctx.written] = '\0';

  const char* val = raw;
  const char* sep = strstr(raw, ": ");
  if (sep) val = sep + 2;

  size_t len = strlen(val);
  while (len && (val[len - 1] == '\n' || val[len - 1] == '\r' ||
                 val[len - 1] == ' ')) {
    len--;
  }
  if (len >= 2 && val[0] == '"' && val[len - 1] == '"') {
    val++;
    len -= 2;
  }

  uint16_t written = 0;
  if (len > offset && out && outSize) {
    size_t chunk = len - offset;
    if (chunk > outSize) chunk = outSize;
    memcpy(out, val + offset, chunk);
    written = (uint16_t)chunk;
  }

  if (totalLen) *totalLen = (uint16_t)len;
  if (more) *more = (uint16_t)(offset + written) < (uint16_t)len;
  return ST_OK;
}

// --- value writer / writer for whole containers -----------------------------

int stSet(uint8_t root, const char* path, uint8_t pathLen,
          const char* value, uint16_t valueLen)
{
  const YamlNode* nodes = stRootNodes(root);
  uint8_t* data = stRootData(root);
  if (!nodes || !data) return ST_ERR_ROOT;
  if (!value || valueLen == 0) return ST_ERR_LEN;

  YamlTreeWalker w;
  w.reset(nodes, data);

  int err = stResolve(w, path, pathLen, false);
  if (err != ST_OK) return err;

  const YamlNode* attr = w.getAttr();
  if (!attr || attr->type == YDT_NONE) return ST_ERR_NOT_FOUND;

  // validate before touching the data, so a bad request cannot corrupt the
  // settings (the parse itself behaves exactly like loading a YAML file)
  switch (attr->type) {
  case YDT_SIGNED: {
    if (attr->size == 0 || attr->size > 32) return ST_ERR_TYPE;
    int32_t v = yaml_str2int(value, (uint8_t)valueLen);
    int32_t lim = (int32_t)1 << (attr->size - 1);
    if (v < -lim || v >= lim) return ST_ERR_RANGE;
    break;
  }
  case YDT_UNSIGNED: {
    if (attr->size == 0 || attr->size > 32) return ST_ERR_TYPE;
    uint32_t v = yaml_str2uint(value, (uint8_t)valueLen);
    if (attr->size < 32 && v >= ((uint32_t)1 << attr->size)) return ST_ERR_RANGE;
    break;
  }
  case YDT_STRING:
    if ((uint32_t)valueLen > (attr->size >> 3)) return ST_ERR_LEN;
    break;
  case YDT_ENUM:
  case YDT_CUSTOM:
    break;
  default:
    // structs, arrays and unions have to be written field by field
    return ST_ERR_TYPE;
  }

  w.setAttrValue(value, valueLen);
  return ST_OK;
}

// --- whole document export --------------------------------------------------

int stExport(uint8_t root, uint16_t offset, char* out, uint16_t outSize,
             uint16_t* totalLen, bool* more)
{
  const YamlNode* nodes = stRootNodes(root);
  uint8_t* data = stRootData(root);
  if (!nodes || !data || !out || outSize == 0) return ST_ERR_ROOT;

  YamlTreeWalker w;
  w.reset(nodes, data);

  StWriteCtx ctx = { out, outSize, 0, offset, 0 };
  if (!w.generate(stCollect, &ctx)) return ST_ERR_TYPE;

  if (totalLen) *totalLen = ctx.total;
  if (more) *more = (uint16_t)(offset + ctx.written) < ctx.total;
  return ST_OK;
}

// --- listing ---------------------------------------------------------------

int stList(uint8_t root, const char* path, uint8_t pathLen, uint8_t startIdx,
           StEntry* entries, uint8_t maxEntries, uint8_t* totalEntries)
{
  const YamlNode* nodes = stRootNodes(root);
  uint8_t* data = stRootData(root);
  if (!nodes || !data || !entries || maxEntries == 0) return ST_ERR_ROOT;

  YamlTreeWalker w;
  w.reset(nodes, data);

  int err = stResolve(w, path, pathLen, true);
  if (err != ST_OK) return err;

  uint8_t idx = 0;
  uint8_t count = 0;

  const YamlNode* attr = w.getAttr();
  while (attr && attr->type != YDT_NONE) {

    if (idx >= startIdx && count < maxEntries) {
      StEntry& e = entries[count];
      memset(&e, 0, sizeof(e));

      if (attr->tag) {
        uint8_t len = attr->tag_len();
        if (len > sizeof(e.tag) - 1) len = sizeof(e.tag) - 1;
        memcpy(e.tag, attr->tag, len);
        e.tag[len] = '\0';
      }

      e.type = attr->type;
      e.size = attr->size;
      e.elmts = attr->elmts;
      count++;
    }

    idx++;
    w.toNextAttr();
    attr = w.getAttr();
  }

  if (totalEntries) *totalEntries = idx;
  return ST_OK;
}

#endif  // !BOOT
