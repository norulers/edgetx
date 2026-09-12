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

#include "mixer_edit.h"

#include "channel_bar.h"
#include "curve_param.h"
#include "curveedit.h"
#include "edgetx.h"
#include "etx_lv_theme.h"
#include "getset_helpers.h"
#include "gvar_numberedit.h"
#include "mixer_edit_adv.h"
#include "mixes.h"
#include "pagegroup.h"
#include "source_numberedit.h"
#include "sourcechoice.h"
#include "switchchoice.h"
#include "textedit.h"
#include "timer_setup.h"

#define SET_DIRTY() storageDirty(EE_MODEL)

class MixerEditStatusBar : public Window
{
 public:
  MixerEditStatusBar(Window *parent, const rect_t &rect, int8_t channel) :
      Window(parent, rect), _channel(channel)
  {
    channelBar =
        new ComboChannelBar(this,
                            {MIX_STATUS_BAR_MARGIN, 0,
                             rect.w - (MIX_STATUS_BAR_MARGIN * 2), rect.h},
                            channel, true);
  }

  static LAYOUT_SIZE_SCALED(MIX_STATUS_BAR_MARGIN, 3, 0)

 protected:
  ComboChannelBar *channelBar;
  int8_t _channel;
};

MixEditWindow::MixEditWindow(int8_t channel, uint8_t index) :
    Page(ICON_MODEL_MIXER, PAD_MEDIUM), channel(channel), index(index)
{
  buildBody(body);
  buildHeader(header);
}

void MixEditWindow::buildHeader(Window *window)
{
  std::string title2(getSourceString(MIXSRC_FIRST_CH + channel));
  header->setTitle(STR_MIXES);
  header->setTitle2(title2);

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
  auto leftIco = new StaticIcon(leftBg, 0, 0, ICON_MODEL_MIXER, COLOR_THEME_PRIMARY2_INDEX);
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

  new MixerEditStatusBar(
      window,
      {window->getRect().w - MIX_STATUS_BAR_WIDTH - PageGroup::PAGE_GROUP_BACK_BTN_W, 0,
       MIX_STATUS_BAR_WIDTH, EdgeTxStyles::MENU_HEADER_HEIGHT},
      channel);
}

#if !NARROW_LAYOUT
static const lv_coord_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(3),
                                     LV_GRID_FR(1), LV_GRID_FR(4),
                                     LV_GRID_TEMPLATE_LAST};
static const lv_coord_t row_dsc[] = {LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};
#else
static const lv_coord_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(2),
                                     LV_GRID_TEMPLATE_LAST};
static const lv_coord_t row_dsc[] = {LV_GRID_CONTENT, LV_GRID_CONTENT,
                                     LV_GRID_TEMPLATE_LAST};
#endif

void MixEditWindow::buildBody(Window *form)
{
  FlexGridLayout grid(col_dsc, row_dsc, PAD_TINY);
  form->setFlexLayout();

  // Dark FPV theme
  lv_obj_set_style_bg_color(form->getLvObj(), lv_color_make(0x18, 0x18, 0x18), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(form->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_text_color(form->getLvObj(), lv_color_white(), LV_PART_MAIN);

  MixData *mix = mixAddress(index);

  // Mix name
  auto line = form->newLine(grid);
  lv_obj_set_style_bg_color(line->getLvObj(), lv_color_make(0x28, 0x28, 0x28), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(line->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
  line->padAll(PAD_SMALL);
  auto lbl = new StaticText(line, rect_t{}, STR_NAME);
  lv_obj_set_style_text_color(lbl->getLvObj(), lv_color_white(), LV_PART_MAIN);
  auto nameEdit = new ModelTextEdit(line, rect_t{}, mix->name, sizeof(mix->name));
  applyDarkBtnStyle(nameEdit->getLvObj());

  // Source
  line = form->newLine(grid);
  lv_obj_set_style_bg_color(line->getLvObj(), lv_color_make(0x28, 0x28, 0x28), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(line->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
  line->padAll(PAD_SMALL);
  lbl = new StaticText(line, rect_t{}, STR_SOURCE);
  lv_obj_set_style_text_color(lbl->getLvObj(), lv_color_white(), LV_PART_MAIN);
  auto srcChoice = new SourceChoice(line, rect_t{}, 0, MIXSRC_LAST,
                   GET_SET_DEFAULT(mix->srcRaw), true);
  applyDarkBtnStyle(srcChoice->getLvObj());

  // Weight
  line = form->newLine(grid);
  lv_obj_set_style_bg_color(line->getLvObj(), lv_color_make(0x28, 0x28, 0x28), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(line->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
  line->padAll(PAD_SMALL);
  lbl = new StaticText(line, rect_t{}, STR_WEIGHT);
  lv_obj_set_style_text_color(lbl->getLvObj(), lv_color_white(), LV_PART_MAIN);
  auto svar = new SourceNumberEdit(line, MIX_WEIGHT_MIN, MIX_WEIGHT_MAX,
                                   GET_SET_DEFAULT(mix->weight), MIXSRC_FIRST);
  svar->setSuffix("%");
  applyDarkBtnStyle(svar->getLvObj());

  // Offset
  lbl = new StaticText(line, rect_t{}, STR_OFFSET);
  lv_obj_set_style_text_color(lbl->getLvObj(), lv_color_white(), LV_PART_MAIN);
  auto gvar = new SourceNumberEdit(line, MIX_OFFSET_MIN, MIX_OFFSET_MAX,
                                   GET_SET_DEFAULT(mix->offset), MIXSRC_FIRST);
  gvar->setSuffix("%");
  applyDarkBtnStyle(gvar->getLvObj());

  // Switch
  line = form->newLine(grid);
  lv_obj_set_style_bg_color(line->getLvObj(), lv_color_make(0x28, 0x28, 0x28), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(line->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
  line->padAll(PAD_SMALL);
  lbl = new StaticText(line, rect_t{}, STR_SWITCH);
  lv_obj_set_style_text_color(lbl->getLvObj(), lv_color_white(), LV_PART_MAIN);
  auto swtch = new SwitchChoice(line, rect_t{}, SWSRC_FIRST_IN_MIXES, SWSRC_LAST_IN_MIXES,
                   GET_SET_DEFAULT(mix->swtch));
  applyDarkBtnStyle(swtch->getLvObj());

  // Curve
  lbl = new StaticText(line, rect_t{}, STR_CURVE);
  lv_obj_set_style_text_color(lbl->getLvObj(), lv_color_white(), LV_PART_MAIN);
  auto curveParam = new CurveParam(line, rect_t{}, &mix->curve, SET_DEFAULT(mix->curve.value), MIXSRC_FIRST, mix->srcRaw);
  applyDarkBtnStyle(curveParam->getLvObj());

  line = form->newLine(grid);
  line->padAll(PAD_LARGE);
  auto btn =
      new TextButton(line, rect_t{}, LV_SYMBOL_SETTINGS, [=]() -> uint8_t {
        new MixEditAdvanced(channel, index);
        return 0;
      });
  lv_obj_set_width(btn->getLvObj(), lv_pct(100));
  applyDarkBtnStyle(btn->getLvObj());
}
