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

#include "radio_diagkeys.h"

#include "hal/rotary_encoder.h"
#include "edgetx.h"
#include "pagegroup.h"
#include "timer_setup.h"

#if defined(RADIO_PL18U)
  static const uint8_t _trimMap[MAX_TRIMS * 2] = {6, 7, 4, 5, 2, 3, 0, 1,
                                                  10, 11, 8, 9, 12, 13, 14, 15};
#elif defined(RADIO_NB4P)
  static const uint8_t _trimMap[MAX_TRIMS * 2] = {0, 1, 2, 3, 4, 5, 6, 7,
                                                  8, 9, 10, 11, 12, 13, 14, 15};
#elif defined(PCBPL18)
  static const uint8_t _trimMap[MAX_TRIMS * 2] = {8, 9, 10, 11, 12, 13, 14, 15,
                                                  2, 3, 4,  5,  0,  1,  6,  7};
#else
  static const uint8_t _trimMap[MAX_TRIMS * 2] = {6, 7, 4, 5, 2,  3,
                                                  0, 1, 8, 9, 10, 11};
#endif

static EnumKeys get_ith_key(uint8_t i)
{
  auto supported_keys = keysGetSupported();
  for (uint8_t k = 0; k < MAX_KEYS; k++) {
    if (supported_keys & (1 << k)) {
      if (i-- == 0) return (EnumKeys)k;
    }
  }

  // should not get here,
  // we assume: i < keysGetMaxKeys()
  return (EnumKeys)0;
}

class RadioKeyDiagsWindow : public Window
{
 public:
  RadioKeyDiagsWindow(Window *parent, const rect_t &rect) : Window(parent, rect)
  {
    padAll(PAD_ZERO);

    coord_t colWidth = (width() - PAD_LARGE * 3) / 3;
    coord_t colHeight = height() - PAD_LARGE - PAD_SMALL;

    Window* form;
    coord_t x = PAD_MEDIUM;

    if (keysGetMaxKeys() > 0) {
      form = new Window(parent, rect_t{x, PAD_MEDIUM, colWidth, colHeight});
      stylePageGroupControl(form->getLvObj());
      lv_obj_set_style_bg_color(form->getLvObj(), lv_color_make(0x28, 0x28, 0x28), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(form->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_set_style_text_color(form->getLvObj(), lv_color_white(), LV_PART_MAIN);
      addKeys(form);
      x += colWidth + PAD_MEDIUM;
    } else {
      colWidth = (width() - PAD_MEDIUM * 3) / 2;
    }

    form = new Window(parent, rect_t{x, PAD_MEDIUM, colWidth, colHeight});
    stylePageGroupControl(form->getLvObj());
    lv_obj_set_style_bg_color(form->getLvObj(), lv_color_make(0x28, 0x28, 0x28), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(form->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(form->getLvObj(), lv_color_white(), LV_PART_MAIN);
    addSwitches(form);
    x += colWidth + PAD_MEDIUM;

    form = new Window(parent, rect_t{x, PAD_MEDIUM, colWidth, colHeight});
    stylePageGroupControl(form->getLvObj());
    lv_obj_set_style_bg_color(form->getLvObj(), lv_color_make(0x28, 0x28, 0x28), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(form->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(form->getLvObj(), lv_color_white(), LV_PART_MAIN);
    addTrims(form);
  }

  ~RadioKeyDiagsWindow()
  {
    delete keyValues;
    delete switchValues;
    delete trimValues;
  }

  void addKeys(Window *form)
  {
    keyValues = new lv_obj_t *[keysGetMaxKeys()];
    lv_obj_t *obj = form->getLvObj();
    uint8_t i;

    // KEYS
    for (i = 0; i < keysGetMaxKeys(); i++) {
      auto k = get_ith_key(i);

      auto lbl = etx_label_create(obj);
      lv_label_set_text(lbl, keysGetLabel(k));
      lv_obj_set_pos(lbl, 0, i * EdgeTxStyles::STD_FONT_HEIGHT);

      lbl = etx_label_create(obj);
      lv_label_set_text(lbl, "");
      lv_obj_set_pos(lbl, KVAL_X, i * EdgeTxStyles::STD_FONT_HEIGHT);
      keyValues[i] = lbl;
    }

#if defined(ROTARY_ENCODER_NAVIGATION) && !defined(USE_HATS_AS_KEYS)
    auto lbl = etx_label_create(obj);
    lv_label_set_text(lbl, STR_ROTARY_ENCODER);
    lv_obj_set_pos(lbl, 0, (i + 1) * EdgeTxStyles::STD_FONT_HEIGHT);

    reValue = etx_label_create(obj);
    lv_label_set_text(reValue, "");
    lv_obj_set_pos(reValue, KVAL_X, (i + 1) * EdgeTxStyles::STD_FONT_HEIGHT);
#endif
  }

  void addSwitches(Window *form)
  {
    switchValues = new lv_obj_t *[switchGetMaxAllSwitches()];
    lv_obj_t *obj = form->getLvObj();
    uint8_t i;
    uint8_t row = 0;

    int maxRows = form->height() / EdgeTxStyles::STD_FONT_HEIGHT;
    coord_t subColW = form->width() / 2;

    // SWITCHES - wrap into second sub-column if rows exceed visible height
    for (i = 0; i < switchGetMaxAllSwitches(); i++) {
      if (SWITCH_EXISTS(i) && !switchIsCustomSwitch(i)) {
        coord_t col_x = (row >= maxRows) ? subColW : 0;
        coord_t col_row = (row >= maxRows) ? row - maxRows : row;
        auto lbl = etx_label_create(obj);
        lv_label_set_text(lbl, "");
        lv_obj_set_pos(lbl, col_x, col_row * EdgeTxStyles::STD_FONT_HEIGHT);
        switchValues[i] = lbl;
        row += 1;
      }
    }
  }

  void addTrims(Window *form)
  {
    trimValues = new lv_obj_t *[keysGetMaxTrims() * 2];
    lv_obj_t *obj = form->getLvObj();
    char s[10];

    auto lbl = etx_label_create(obj);
    lv_label_set_text(lbl, STR_TRIMS);
    lv_obj_set_pos(lbl, 0, 0);
    lbl = etx_label_create(obj);
    lv_label_set_text(lbl, "-");
    lv_obj_set_pos(lbl, TRIM_MINUS_X, 0);
    lbl = etx_label_create(obj);
    lv_label_set_text(lbl, "+");
    lv_obj_set_pos(lbl, TRIM_PLUS_X, 0);

    // TRIMS
    for (uint8_t i = 0; i < keysGetMaxTrims(); i++) {
      lbl = etx_label_create(obj);
      formatNumberAsString(s, 10, i + 1, 0, 10, "T");
      lv_label_set_text(lbl, s);
      lv_obj_set_pos(lbl, PAD_SMALL, i * EdgeTxStyles::STD_FONT_HEIGHT + EdgeTxStyles::STD_FONT_HEIGHT);

      lbl = etx_label_create(obj);
      lv_label_set_text(lbl, "");
      lv_obj_set_pos(lbl, TRIM_MINUS_X - PAD_TINY, i * EdgeTxStyles::STD_FONT_HEIGHT + EdgeTxStyles::STD_FONT_HEIGHT);
      trimValues[i * 2] = lbl;

      lbl = etx_label_create(obj);
      lv_label_set_text(lbl, "");
      lv_obj_set_pos(lbl, TRIM_PLUS_X, i * EdgeTxStyles::STD_FONT_HEIGHT + EdgeTxStyles::STD_FONT_HEIGHT);
      trimValues[i * 2 + 1] = lbl;
    }
  }

  void setKeyState()
  {
    char s[10] = "0";

    for (uint8_t i = 0; i < keysGetMaxKeys(); i++) {
      auto k = get_ith_key(i);
      s[0] = keysGetState(k) + '0';
      lv_label_set_text(keyValues[i], s);
    }

#if defined(ROTARY_ENCODER_NAVIGATION) && !defined(USE_HATS_AS_KEYS)
    formatNumberAsString(s, 10, rotaryEncoderGetValue());
    lv_label_set_text(reValue, s);
#endif
  }

  void setSwitchState()
  {
    uint8_t i;

    for (i = 0; i < switchGetMaxAllSwitches(); i++) {
      if (SWITCH_EXISTS(i) && !switchIsCustomSwitch(i)) {
        getvalue_t val = getValue(MIXSRC_FIRST_SWITCH + i);
        getvalue_t sw =
            ((val < 0) ? 3 * i + 1 : ((val == 0) ? 3 * i + 2 : 3 * i + 3));
        lv_label_set_text(switchValues[i], getSwitchPositionName(sw));
      }
    }
  }

  void setTrimState()
  {
    char s[10] = "0";

    for (uint8_t i = 0; i < keysGetMaxTrims() * 2; i++) {
      s[0] = keysGetTrimState(_trimMap[i]) + '0';
      lv_label_set_text(trimValues[i], s);
    }
  }

  void checkEvents() override
  {
    setKeyState();
    setSwitchState();
    setTrimState();
  }

 protected:
  lv_obj_t **keyValues = nullptr;
#if defined(ROTARY_ENCODER_NAVIGATION) && !defined(USE_HATS_AS_KEYS)
  lv_obj_t *reValue = nullptr;
#endif
  lv_obj_t **switchValues = nullptr;
  lv_obj_t **trimValues = nullptr;

  static LAYOUT_VAL_SCALED(KVAL_X, 70)
  static LAYOUT_VAL_SCALED(TRIM_MINUS_X, 62)
  static LAYOUT_VAL_SCALED(TRIM_PLUS_X, 75)
};

void RadioKeyDiagsPage::buildHeader(Window *window)
{
  header->setTitle(STR_HARDWARE);
  header->setTitle2(STR_MENU_RADIO_SWITCHES);
}

void RadioKeyDiagsPage::buildBody(Window *window)
{
  body->padAll(PAD_ZERO);
  new RadioKeyDiagsWindow(window, {0, 0, window->width(), window->height()});
}

RadioKeyDiagsPage::RadioKeyDiagsPage() : Page(ICON_MODEL_SETUP)
{
  buildHeader(header);
  buildBody(body);

  // Dark FPV header: hide original canvas icons, place new ones with orange color
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(header->getLvObj()); i++) {
    auto child = lv_obj_get_child(header->getLvObj(), i);
    if (lv_obj_check_type(child, &lv_canvas_class))
      lv_obj_add_flag(child, LV_OBJ_FLAG_HIDDEN);
  }
  auto leftBg = new StaticIcon(header, 0, 0, ICON_TOPLEFT_BG, COLOR_THEME_PRIMARY2_INDEX);
  lv_obj_set_style_img_recolor_opa(leftBg->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_img_recolor(leftBg->getLvObj(), lv_color_make(0x22, 0x22, 0x22), LV_PART_MAIN);
  leftBg->setTop((EdgeTxStyles::MENU_HEADER_HEIGHT - leftBg->height()) / 2);
  auto leftIco = new StaticIcon(leftBg, 0, 0, ICON_MODEL_SETUP, COLOR_THEME_PRIMARY2_INDEX);
  lv_obj_set_style_img_recolor_opa(leftIco->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_img_recolor(leftIco->getLvObj(), lv_color_make(0xFF, 0x8C, 0x00), LV_PART_MAIN);
  leftIco->center(leftBg->width() + PAD_MEDIUM, leftBg->height());
  auto rightBg = new StaticIcon(header, LCD_W, 0, ICON_TOPRIGHT_BG, COLOR_THEME_PRIMARY2_INDEX);
  lv_obj_set_style_img_recolor_opa(rightBg->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_img_recolor(rightBg->getLvObj(), lv_color_make(0x22, 0x22, 0x22), LV_PART_MAIN);
  rightBg->setPos(LCD_W - rightBg->width(), (EdgeTxStyles::MENU_HEADER_HEIGHT - rightBg->height()) / 2);
  auto rightIco = new StaticIcon(rightBg, 0, 0, ICON_BTN_CLOSE, COLOR_THEME_PRIMARY2_INDEX);
  lv_obj_set_style_img_recolor_opa(rightIco->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_img_recolor(rightIco->getLvObj(), lv_color_make(0xFF, 0x8C, 0x00), LV_PART_MAIN);
  rightIco->center(rightBg->width() + PAD_MEDIUM, rightBg->height());

  // Dark FPV theme
  lv_obj_set_style_bg_color(body->getLvObj(), lv_color_make(0x18, 0x18, 0x18), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(body->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_text_color(body->getLvObj(), lv_color_white(), LV_PART_MAIN);
}
