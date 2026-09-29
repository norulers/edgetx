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

#include "ui_screens_yaml.h"

#if defined(COLORLCD) && defined(UI_SCREENS_SEPARATE_STORAGE)

#include "edgetx.h"
#include "layout.h"
#include "sdcard_common.h"
#include "sdcard_yaml.h"
#include "yaml/yaml_datastructs.h"
#include "yaml/yaml_tree_walker.h"

#include <utility>

#define UI_SCREENS_PATH ROOT_PATH "SCREENS"

// Mirrors the trailing scalar members of the ScreensData yaml root node. The
// screen and top bar nodes have a bit size of 0 and their read/write callbacks
// address g_model directly, so only these two are held in the walker buffer.
struct UiScreensBuffer {
  uint8_t topbarWidgetWidth[MAX_TOPBAR_ZONES];
  uint8_t view;
};

// Main view configuration as last written by the other firmware. It is swapped
// back in while the model file is written so we never clobber it.
static CustomScreenData* stockScreens[MAX_CUSTOM_SCREENS] = {};
static TopBarPersistentData stockTopbar;
static UiScreensBuffer stockBuffer;
static bool stashed = false;

static void swapWithStock()
{
  g_model.swapScreenData(stockScreens, &stockTopbar);
  for (int i = 0; i < MAX_TOPBAR_ZONES; i += 1)
    std::swap(g_model.topbarWidgetWidth[i], stockBuffer.topbarWidgetWidth[i]);
  std::swap(g_model.view, stockBuffer.view);
}

static void clearStock()
{
  for (int i = 0; i < MAX_CUSTOM_SCREENS; i += 1) {
    delete stockScreens[i];
    stockScreens[i] = nullptr;
  }
  stockTopbar.clear();
  memset(&stockBuffer, 0, sizeof(stockBuffer));
}

// Drops screens whose layout only exists in this build. They are leftovers
// from before the configurations were split and would leave stock EdgeTX with
// an empty main view.
static void sanitizeStock()
{
  int dst = 0;

  for (int i = 0; i < MAX_CUSTOM_SCREENS; i += 1) {
    auto* data = stockScreens[i];
    stockScreens[i] = nullptr;
    if (!data) continue;

    if (LayoutFactory::isPrivateLayout(data->LayoutId.c_str())) {
      delete data;
      continue;
    }

    stockScreens[dst++] = data;
  }

  if (stockBuffer.view >= dst) stockBuffer.view = 0;
}

// Stock EdgeTX renders an empty main view when the model file holds no layout
// it knows about, so make sure the shared file always carries a usable one.
static void ensureStockDefault()
{
  if (stockScreens[0] || !defaultLayout) return;

  auto* data = new CustomScreenData();
  data->layoutData.clear();
  data->LayoutId = defaultLayout->getId();

  for (int i = 0; i <= LAYOUT_OPTION_LAST_DEFAULT; i += 1) {
    data->layoutData.options[i].type = LOV_Bool;
    data->layoutData.options[i].value.boolValue = (i != LAYOUT_OPTION_MIRRORED);
  }

  stockScreens[0] = data;

  for (int i = 0; i < MAX_TOPBAR_ZONES; i += 1)
    stockBuffer.topbarWidgetWidth[i] = 1;
}

static bool getCompanionPath(char* path, size_t size, const char* filename)
{
  if (!filename || !filename[0]) return false;
  // The name is used verbatim as a path element
  if (strchr(filename, '/') || strchr(filename, '\\') || strstr(filename, ".."))
    return false;
  if (strlen(filename) + sizeof(UI_SCREENS_PATH PATH_SEPARATOR) > size)
    return false;

  strcpy(path, UI_SCREENS_PATH PATH_SEPARATOR);
  strcat(path, filename);
  return true;
}

void uiScreensClearStock() { clearStock(); }

void uiScreensLoad(const char* filename)
{
  stashed = false;
  clearStock();

  // The model yaml has just been parsed, so what is live now is the stock
  // configuration; park it and start from a clean slate.
  swapWithStock();
  sanitizeStock();
  ensureStockDefault();

  char path[LEN_MODEL_FILENAME + sizeof(UI_SCREENS_PATH PATH_SEPARATOR)];
  if (!getCompanionPath(path, sizeof(path), filename)) return;

  UiScreensBuffer buffer;
  memset(&buffer, 0, sizeof(buffer));

  YamlTreeWalker tree;
  tree.reset(get_screensdata_nodes(), (uint8_t*)&buffer);

  if (readYamlFile(path, YamlTreeWalker::get_parser_calls(), &tree, nullptr)) {
    // No companion file yet (or unreadable): drop whatever was parsed so the
    // caller falls back to creating a default layout.
    g_model.resetScreenData();
    memset(g_model.topbarWidgetWidth, 0, sizeof(g_model.topbarWidgetWidth));
    g_model.view = 0;
    return;
  }

  memcpy(g_model.topbarWidgetWidth, buffer.topbarWidgetWidth,
         sizeof(buffer.topbarWidgetWidth));
  g_model.view = buffer.view;
}

void uiScreensBeginModelWrite(const char* filename)
{
  if (stashed) return;

  char path[LEN_MODEL_FILENAME + sizeof(UI_SCREENS_PATH PATH_SEPARATOR)];
  if (getCompanionPath(path, sizeof(path), filename)) {
    UiScreensBuffer buffer;
    memcpy(buffer.topbarWidgetWidth, g_model.topbarWidgetWidth,
           sizeof(buffer.topbarWidgetWidth));
    buffer.view = g_model.view;

    f_mkdir(UI_SCREENS_PATH);
    writeFileYaml(path, get_screensdata_nodes(), (uint8_t*)&buffer, 0);
  }

  ensureStockDefault();
  swapWithStock();
  stashed = true;
}

void uiScreensEndModelWrite()
{
  if (!stashed) return;
  swapWithStock();
  stashed = false;
}

void uiScreensDelete(const char* filename)
{
  char path[LEN_MODEL_FILENAME + sizeof(UI_SCREENS_PATH PATH_SEPARATOR)];
  if (getCompanionPath(path, sizeof(path), filename)) f_unlink(path);
}

void uiScreensCopy(const char* srcFilename, const char* destFilename)
{
  char src[LEN_MODEL_FILENAME + sizeof(UI_SCREENS_PATH PATH_SEPARATOR)];
  char dest[LEN_MODEL_FILENAME + sizeof(UI_SCREENS_PATH PATH_SEPARATOR)];

  if (!getCompanionPath(src, sizeof(src), srcFilename)) return;
  if (!getCompanionPath(dest, sizeof(dest), destFilename)) return;
  if (f_stat(src, nullptr) != FR_OK) return;

  f_mkdir(UI_SCREENS_PATH);
  sdCopyFile(src, dest);
}

#endif
