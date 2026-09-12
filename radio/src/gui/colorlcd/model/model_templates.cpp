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

#include "model_templates.h"

#include "standalone_lua.h"
#include "etx_lv_theme.h"
#include "lib_file.h"

#define ETX_STATE_NO_INFO_COLOR LV_STATE_USER_1

static const lv_coord_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1),
                                     LV_GRID_TEMPLATE_LAST};
static const lv_coord_t row_dsc[] = {LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};

TemplatePage::TemplatePage() : Page(ICON_MODEL_SELECT, PAD_ZERO)
{
  // Hide original blue HeaderIcon/HeaderBackIcon (lv_canvas) and recreate dark
  {
    lv_obj_t* hdr = header->getLvObj();
    uint32_t cnt = lv_obj_get_child_cnt(hdr);
    for (uint32_t i = 0; i < cnt; i++) {
      auto child = lv_obj_get_child(hdr, i);
      if (lv_obj_check_type(child, &lv_canvas_class)) {
        lv_obj_add_flag(child, LV_OBJ_FLAG_HIDDEN);
        // Also hide inner icon
        uint32_t ccnt = lv_obj_get_child_cnt(child);
        for (uint32_t j = 0; j < ccnt; j++) {
          lv_obj_add_flag(lv_obj_get_child(child, j), LV_OBJ_FLAG_HIDDEN);
        }
      }
    }
    // Left: dark bg + orange icon
    auto leftBg = new StaticIcon(header, 0, 0, ICON_TOPLEFT_BG, COLOR_THEME_PRIMARY2_INDEX);
    lv_obj_set_style_img_recolor_opa(leftBg->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_img_recolor(leftBg->getLvObj(), lv_color_make(0x22, 0x22, 0x22), LV_PART_MAIN);
    leftBg->setTop((EdgeTxStyles::MENU_HEADER_HEIGHT - leftBg->height()) / 2);
    auto leftIco = new StaticIcon(leftBg, 0, 0, ICON_MODEL_SELECT, COLOR_THEME_PRIMARY2_INDEX);
    lv_obj_set_style_img_recolor_opa(leftIco->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_img_recolor(leftIco->getLvObj(), lv_color_make(0xFF, 0x8C, 0x00), LV_PART_MAIN);
    leftIco->center(leftBg->width() + PAD_MEDIUM, leftBg->height());
    // Right: dark bg + orange close icon
    auto rightBg = new StaticIcon(header, LCD_W - 0, 0, ICON_TOPRIGHT_BG, COLOR_THEME_PRIMARY2_INDEX);
    lv_obj_set_style_img_recolor_opa(rightBg->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_img_recolor(rightBg->getLvObj(), lv_color_make(0x22, 0x22, 0x22), LV_PART_MAIN);
    rightBg->setPos(LCD_W - rightBg->width(),
                    (EdgeTxStyles::MENU_HEADER_HEIGHT - rightBg->height()) / 2);
    auto rightIco = new StaticIcon(rightBg, 0, 0, ICON_BTN_CLOSE, COLOR_THEME_PRIMARY2_INDEX);
    lv_obj_set_style_img_recolor_opa(rightIco->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_img_recolor(rightIco->getLvObj(), lv_color_make(0xFF, 0x8C, 0x00), LV_PART_MAIN);
    rightIco->center(rightBg->width() + PAD_MEDIUM, rightBg->height());
  }

  body->setFlexLayout();

  FlexGridLayout grid(col_dsc, row_dsc, PAD_SMALL);

  auto line = body->newLine(grid);

  listWindow = new Window(line, rect_t{});
  etx_scrollbar(listWindow->getLvObj());
  listWindow->padAll(PAD_TINY);
  listWindow->padRight(PAD_SMALL);
  listWindow->setFlexLayout(LV_FLEX_FLOW_COLUMN, PAD_SMALL, LV_PCT(100), body->height() - PAD_SMALL * 2);
  lv_obj_set_flex_align(listWindow->getLvObj(), LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_SPACE_BETWEEN);
  lv_obj_set_grid_cell(listWindow->getLvObj(), LV_GRID_ALIGN_STRETCH, 0, 1,
                       LV_GRID_ALIGN_START, 0, 1);
  // Dark theme
  lv_obj_set_style_bg_color(listWindow->getLvObj(), lv_color_make(0x18, 0x18, 0x18), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(listWindow->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(line->getLvObj(), lv_color_make(0x18, 0x18, 0x18), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(line->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);

  infoLabel = etx_label_create(line->getLvObj());
  lv_label_set_text(infoLabel, "");
  lv_obj_set_height(infoLabel, body->height() - PAD_SMALL * 2);
  etx_obj_add_style(infoLabel, styles->text_align_left, LV_PART_MAIN);
  lv_obj_set_style_text_color(infoLabel, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_text_color(infoLabel, lv_color_make(0x66, 0x66, 0x66), ETX_STATE_NO_INFO_COLOR);
  lv_obj_set_grid_cell(infoLabel, LV_GRID_ALIGN_STRETCH, 1, 1,
                       LV_GRID_ALIGN_CENTER, 0, 1);
}

void TemplatePage::updateInfo()
{
  if (buffer[0]) {
    FIL fp;
    FRESULT res = f_open(&fp, buffer, FA_READ);
    unsigned int bytesRead = 0;
    if (res == FR_OK) {
      f_read(&fp, infoText, LEN_INFO_TEXT, &bytesRead);
      f_close(&fp);
    }
    infoText[bytesRead] = '\0';
  }

  if (infoText[0] == 0) {
    lv_label_set_text(infoLabel, STR_NO_INFORMATION);
    lv_obj_add_state(infoLabel, ETX_STATE_NO_INFO_COLOR);
  } else {
    lv_label_set_text(infoLabel, infoText);
    lv_obj_clear_state(infoLabel, ETX_STATE_NO_INFO_COLOR);
  }
}

void TemplatePage::setInfo(const char* text)
{
  strncpy(infoText, text, LEN_INFO_TEXT);
  infoText[LEN_INFO_TEXT] = '\0';
  lv_label_set_text(infoLabel, infoText);
  lv_obj_clear_state(infoLabel, ETX_STATE_NO_INFO_COLOR);
}

// Try language-specific file (e.g., about_cn.txt), fall back to base path
static void resolveLangPath(char* out, size_t outSize, const char* basePath)
{
  // Default: copy base path
  strncpy(out, basePath, outSize);
  out[outSize - 1] = '\0';

  // Get language code
  char lang[3] = {0};
#if defined(ALL_LANGS)
  // Multi-language: use runtime uiLanguage
  if (g_eeGeneral.uiLanguage[0]) {
    lang[0] = g_eeGeneral.uiLanguage[0];
    lang[1] = g_eeGeneral.uiLanguage[1];
  }
#else
  // Single-language: use compile-time TRANSLATIONS, lowercased
  lang[0] = tolower(TRANSLATIONS[0]);
  lang[1] = tolower(TRANSLATIONS[1]);
#endif
  if (!lang[0]) return;

  // Find last '.' for extension
  const char* dot = nullptr;
  for (const char* p = basePath; *p; p++) {
    if (*p == '.') dot = p;
  }
  if (!dot) return;

  // Build language path: insert _xx before extension
  int baseLen = dot - basePath;
  char langPath[FF_MAX_LFN];
  snprintf(langPath, sizeof(langPath), "%.*s_%s%s", baseLen, basePath, lang, dot);
  FILINFO fno;
  if (f_stat(langPath, &fno) == FR_OK && !(fno.fattrib & AM_DIR)) {
    strncpy(out, langPath, outSize);
    out[outSize - 1] = '\0';
  }
}

static void styleDarkButton(ButtonBase* btn)
{
  lv_obj_t* obj = btn->getLvObj();
  lv_obj_set_style_bg_color(obj, lv_color_make(0x28, 0x28, 0x28), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_text_color(obj, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
  lv_obj_set_style_outline_width(obj, 0, LV_PART_MAIN | LV_STATE_FOCUS_KEY);
  lv_obj_set_style_bg_color(obj, lv_color_make(0xFF, 0x8C, 0x00), LV_PART_MAIN | LV_STATE_FOCUSED);
  lv_obj_set_style_text_color(obj, lv_color_black(), LV_PART_MAIN | LV_STATE_FOCUSED);
  lv_obj_set_style_bg_color(obj, lv_color_make(0x00, 0xA0, 0x00), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_text_color(obj, lv_color_black(), LV_PART_MAIN | LV_STATE_PRESSED);
}

class SelectTemplate : public TemplatePage
{
 public:
  SelectTemplate(SelectTemplateFolder* tp, std::string folder,
                 const char* subtitle = STR_NEW_MODEL) :
      templateFolderPage(tp)
  {
    header->setTitle(STR_MAIN_MENU_MANAGE_MODELS);
    header->setTitle2(subtitle);

    char path[LEN_PATH + 1];
    snprintf(path, LEN_PATH, "%s/%s", TEMPLATES_PATH, folder.c_str());

    std::list<std::string> files;
    FILINFO fno;
    DIR dir;
    FRESULT res = f_opendir(&dir, path);

    ButtonBase* firstButton = nullptr;

    if (res == FR_OK) {
      // read all entries
      for (;;) {
        res = f_readdir(&dir, &fno);
        if (res != FR_OK || fno.fname[0] == 0)
          break;  // Break on error or end of dir
        if (strlen((const char*)fno.fname) > SD_SCREEN_FILE_LENGTH) continue;
        if (fno.fattrib & (AM_DIR | AM_HID | AM_SYS))
          continue; /* Ignore folders, hidden and system files */
        if (fno.fname[0] == '.') continue; /* Ignore UNIX hidden files */

        const char* ext = getFileExtension(fno.fname);
        if (ext && !strcasecmp(ext, YAML_EXT)) {
          int len = ext - fno.fname;
          if (len < FF_MAX_LFN) {
            char name[FF_MAX_LFN] = {0};
            strncpy(name, fno.fname, len);
            files.push_back(name);
          }
        }
      }

      files.sort(compare_nocase);

      for (auto name : files) {
        auto tb = new TextButton(
            listWindow, rect_t{0, 0, lv_pct(100), EdgeTxStyles::STD_FONT_HEIGHT * 2}, name,
            [=]() -> uint8_t {
              deleteLater();
              templateFolderPage->doUpdate(folder, name);
              return 0;
            });
        styleDarkButton(tb);
        tb->setFocusHandler([=](bool active) {
          if (active) {
            char basePath[LEN_BUFFER + 1];
            snprintf(basePath, sizeof(basePath), "%s/%s%s", path, name.c_str(),
                     TEXT_EXT);
            resolveLangPath(buffer, LEN_BUFFER, basePath);
            updateInfo();
          }
        });

        if (!firstButton) firstButton = tb;
      }
    }

    f_closedir(&dir);

    if (files.size() == 0) {
      auto noTpl = new StaticText(listWindow, rect_t{0, 0, lv_pct(100), lv_pct(50)},
                     STR_NO_TEMPLATES);
      lv_obj_set_style_text_color(noTpl->getLvObj(), lv_color_white(), LV_PART_MAIN);
    } else {
      lv_group_focus_obj(firstButton->getLvObj());
    }
  }

 protected:
  SelectTemplateFolder* templateFolderPage;
};

SelectTemplateFolder::SelectTemplateFolder(
    std::function<void(std::string folder, std::string)> update)
{
  this->update = update;

  header->setTitle(STR_MAIN_MENU_MANAGE_MODELS);
  header->setTitle2(STR_NEW_MODEL);

  auto tfb = new TextButton(listWindow,
                            rect_t{0, 0, lv_pct(100), EdgeTxStyles::STD_FONT_HEIGHT * 2},
                            STR_BLANK_MODEL, [=]() -> uint8_t {
                              doUpdate("", "");
                              return 0;
                            });
  styleDarkButton(tfb);
  tfb->setFocusHandler([=](bool active) {
    if (active) {
      buffer[0] = 0;
      strcpy(infoText, STR_BLANK_MODEL_INFO);
      updateInfo();
    }
  });

  // ─── Model Wizard button (C++ wizard replaces Lua for known types) ──
  auto wizBtn = new TextButton(
      listWindow, rect_t{0, 0, lv_pct(100), EdgeTxStyles::STD_FONT_HEIGHT * 2},
      STR_WIZARD_GUIDE, [=]() -> uint8_t {
        // C++ wizard selection: use TemplatePage for same layout as SelectTemplate
        auto page = new TemplatePage();
        page->setHeaderTitle(STR_MAIN_MENU_MANAGE_MODELS);
        page->setHeaderTitle2(STR_WIZARD_GUIDE);

        struct { const char* label; const char* info; const char* name; } wizItems[] = {
          {STR_WIZARD_TMPL_PLANE, STR_WIZARD_TMPL_PLANE_INFO, "Plane"},
          {STR_WIZARD_TMPL_GLIDER, STR_WIZARD_TMPL_GLIDER_INFO, "Glider"},
          {STR_WIZARD_TMPL_DELTA, STR_WIZARD_TMPL_DELTA_INFO, "Wing"},
          {STR_WIZARD_TMPL_HELI, STR_WIZARD_TMPL_HELI_INFO, "Helicopter"},
          {STR_WIZARD_TMPL_MULTIROTOR, STR_WIZARD_TMPL_MULTIROTOR_INFO, "Multirotor"},
        };
        TextButton* firstBtn = nullptr;
        for (auto& it : wizItems) {
          const char* label = it.label;
          const char* info = it.info;
          const char* name = it.name;
          auto btn = new TextButton(
              page->listWindow, rect_t{0, 0, lv_pct(100), EdgeTxStyles::STD_FONT_HEIGHT * 2},
              label, [=]() -> uint8_t {
                page->deleteLater();
                doUpdate("__cppwiz__", name);
                return 0;
              });
          styleDarkButton(btn);
          btn->setFocusHandler([=](bool active) {
            if (active) {
              page->setInfo(info);
            }
          });
          if (!firstBtn) firstBtn = btn;
        }
        if (firstBtn) lv_group_focus_obj(firstBtn->getLvObj());
        return 0;
      });
  styleDarkButton(wizBtn);
  wizBtn->setFocusHandler([=](bool active) {
    if (active) {
      buffer[0] = 0;
      strcpy(infoText, STR_WIZARD_GUIDE_INFO);
      updateInfo();
    }
  });

  // ─── Third-party Script button (SD card script folders) ──
  auto tpsBtn = new TextButton(
      listWindow, rect_t{0, 0, lv_pct(100), EdgeTxStyles::STD_FONT_HEIGHT * 2},
      STR_THIRD_PARTY_SCRIPT, [=]() -> uint8_t {
        auto page = new TemplatePage();
        page->setHeaderTitle(STR_MAIN_MENU_MANAGE_MODELS);
        page->setHeaderTitle2(STR_THIRD_PARTY_SCRIPT);

        std::list<std::string> dirs;
        FILINFO fno2;
        DIR dir2;
        FRESULT res2 = f_opendir(&dir2, TEMPLATES_PATH);

        TextButton* firstBtn = nullptr;

        if (res2 == FR_OK) {
          for (;;) {
            res2 = f_readdir(&dir2, &fno2);
            if (res2 != FR_OK || fno2.fname[0] == 0)
              break;
            if (strlen((const char*)fno2.fname) > SD_SCREEN_FILE_LENGTH) continue;
            if (fno2.fattrib & (AM_HID | AM_SYS))
              continue;
            if (fno2.fname[0] == '.') continue;
            if (fno2.fattrib & AM_DIR) dirs.push_back((char*)fno2.fname);
          }

          dirs.sort(compare_nocase);

          for (auto name : dirs) {
#if not defined(LUA)
            if (!strcasecmp(name.c_str(), "WIZARD") == 0) {
#endif
              auto btn = new TextButton(
                  page->listWindow,
                  rect_t{0, 0, lv_pct(100), EdgeTxStyles::STD_FONT_HEIGHT * 2},
                  name, [=]() -> uint8_t {
                    page->deleteLater();
                    new SelectTemplate(this, name);
                    return 0;
                  });
              styleDarkButton(btn);
              btn->setFocusHandler([=](bool active) {
                if (active) {
                  char basePath[LEN_BUFFER + 1];
                  snprintf(basePath, sizeof(basePath), "%s/%s/about%s",
                           TEMPLATES_PATH, name.c_str(), TEXT_EXT);
                  resolveLangPath(page->buffer, LEN_BUFFER, basePath);
                  page->updateInfo();
                }
              });
              if (!firstBtn) firstBtn = btn;
#if not defined(LUA)
            }
#endif
          }
        }

        f_closedir(&dir2);

        if (dirs.size() == 0) {
          auto noTpl = new StaticText(
              page->listWindow,
              rect_t{0, 0, lv_pct(100), lv_pct(50)},
              STR_NO_TEMPLATES);
          lv_obj_set_style_text_color(noTpl->getLvObj(), lv_color_white(),
                                      LV_PART_MAIN);
        } else {
          lv_group_focus_obj(firstBtn->getLvObj());
        }
        return 0;
      });
  styleDarkButton(tpsBtn);
  tpsBtn->setFocusHandler([=](bool active) {
    if (active) {
      buffer[0] = 0;
      strcpy(infoText, STR_THIRD_PARTY_SCRIPT_INFO);
      updateInfo();
    }
  });

  lv_group_focus_obj(tfb->getLvObj());
}
