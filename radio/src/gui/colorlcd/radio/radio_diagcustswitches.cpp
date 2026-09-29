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

#if defined(FUNCTION_SWITCHES)

#include "radio_diagcustswitches.h"

#include "hal/rgbleds.h"
#include "board.h"
#include "edgetx.h"
#include "pagegroup.h"
#include "timer_setup.h"

#if defined(FUNCTION_SWITCHES_RGB_LEDS)
#include "color_list.h"
#include "hal/rgbleds.h"

uint16_t getLedColor(int i)
{
  // Convert RBG888 to RGB565
  uint32_t rgb32 = getFSLedRGBColor(i);
  uint8_t r = GET_RED32(rgb32);
  uint8_t g = GET_GREEN32(rgb32);
  uint8_t b = GET_BLUE32(rgb32);
  return RGB(r, g, b);
}
#endif

class RadioCustSwitchesDiagsWindow : public Window
{
  static LAYOUT_VAL_SCALED(FS_1ST_COLUMN, 95)
  static LAYOUT_VAL_SCALED(FS_2ND_COLUMN, 160)
  static LAYOUT_VAL_SCALED(FS_3RD_COLUMN, 260)
  static LAYOUT_VAL_SCALED(FS_LBL_WIDTH, 80)
#if defined(FUNCTION_SWITCHES_RGB_LEDS)
  static LAYOUT_VAL_SCALED(FS_COLOR_WIDTH, 30)
  static LAYOUT_VAL_SCALED(FS_COLOR_HEIGHT, 15)
  ColorSwatch* colorBox[NUM_FUNCTIONS_SWITCHES];
#endif

 public:
  RadioCustSwitchesDiagsWindow(Window *parent, const rect_t &rect) :
      Window(parent, rect)
  {
    stylePageGroupControl(lvobj);
    lv_obj_set_style_bg_color(lvobj, lv_color_make(0x28, 0x28, 0x28), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(lvobj, LV_OPA_COVER, LV_PART_MAIN);

    auto physLbl = new StaticText(this, {FS_1ST_COLUMN, PAD_SMALL, FS_LBL_WIDTH, LV_SIZE_CONTENT},
             "Phys", COLOR_THEME_QM_FG_INDEX);
    lv_obj_set_style_text_color(physLbl->getLvObj(), lv_color_white(), LV_PART_MAIN);
    auto logLbl = new StaticText(this, {FS_2ND_COLUMN, PAD_SMALL, FS_LBL_WIDTH, LV_SIZE_CONTENT},
             "Log", COLOR_THEME_QM_FG_INDEX);
    lv_obj_set_style_text_color(logLbl->getLvObj(), lv_color_white(), LV_PART_MAIN);
    auto ledLbl = new StaticText(this, {FS_3RD_COLUMN, PAD_SMALL, FS_LBL_WIDTH, LV_SIZE_CONTENT},
             "Led", COLOR_THEME_QM_FG_INDEX);
    lv_obj_set_style_text_color(ledLbl->getLvObj(), lv_color_white(), LV_PART_MAIN);
    for (uint8_t i = 0, r = 0; i < switchGetMaxSwitches(); i += 1) {
      if (switchIsCustomSwitch(i)) {
        coord_t y = (r + 2) * EdgeTxStyles::STD_FONT_HEIGHT;
          std::string s(CHAR_SWITCH);
          s += switchGetDefaultName(i);
          auto swLbl = new StaticText(this, {PAD_LARGE, y, FS_LBL_WIDTH, EdgeTxStyles::STD_FONT_HEIGHT + 2}, s, COLOR_THEME_QM_FG_INDEX);
          lv_label_set_long_mode(swLbl->getLvObj(), LV_LABEL_LONG_CLIP);
          lv_obj_set_style_text_color(swLbl->getLvObj(), lv_color_white(), LV_PART_MAIN);
        auto physVal = new DynamicText(
            this, {FS_1ST_COLUMN + PAD_LARGE, y, FS_LBL_WIDTH, LV_SIZE_CONTENT},
            [=]() {
              return getFSPhysicalState(i) ? CHAR_DOWN : CHAR_UP;
            }, COLOR_THEME_QM_FG_INDEX);
        lv_obj_set_style_text_color(physVal->getLvObj(), lv_color_white(), LV_PART_MAIN);
        auto logVal = new DynamicText(
            this, {FS_2ND_COLUMN + 10, y, FS_LBL_WIDTH, LV_SIZE_CONTENT},
            [=]() { return g_model.cfsState(i) ? CHAR_DOWN : CHAR_UP; },
            COLOR_THEME_QM_FG_INDEX);
        lv_obj_set_style_text_color(logVal->getLvObj(), lv_color_white(), LV_PART_MAIN);

#if defined(FUNCTION_SWITCHES_RGB_LEDS)
        colorBox[r] = new ColorSwatch(this, {FS_3RD_COLUMN, y, FS_COLOR_WIDTH,
                                             FS_COLOR_HEIGHT}, getLedColor(r));
#else
        auto ledVal = new DynamicText(this,
                        {FS_3RD_COLUMN, y, FS_LBL_WIDTH, LV_SIZE_CONTENT},
                        [=]() { return STR_OFFON[getFSLedState(i)]; },
                        COLOR_THEME_QM_FG_INDEX);
        lv_obj_set_style_text_color(ledVal->getLvObj(), lv_color_white(), LV_PART_MAIN);
#endif
        r += 1;
      }
    }
  }

#if defined(FUNCTION_SWITCHES_RGB_LEDS)
  void checkEvents() override {
    Window::checkEvents();
    for (uint8_t i = 0; i < NUM_FUNCTIONS_SWITCHES; i += 1) {
      if (colorBox[i])
        colorBox[i]->setColor(getLedColor(i));
    }
  }
#endif
};

void RadioCustSwitchesDiagsPage::buildHeader(Window *window)
{
  header->setTitle(STR_HARDWARE);
  header->setTitle2(STR_FUNCTION_SWITCHES);
}

void RadioCustSwitchesDiagsPage::buildBody(Window *window)
{
  body->padAll(PAD_ZERO);
  new RadioCustSwitchesDiagsWindow(window,
                                   {0, 0, window->width(), window->height()});
}

RadioCustSwitchesDiagsPage::RadioCustSwitchesDiagsPage() :
    Page(ICON_MODEL_SETUP)
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

#endif
