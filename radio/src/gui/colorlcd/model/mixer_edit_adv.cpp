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

#include "mixer_edit_adv.h"

#include "choice.h"
#include "edgetx.h"
#include "etx_lv_theme.h"
#include "fm_matrix.h"
#include "getset_helpers.h"
#include "mixes.h"
#include "numberedit.h"
#include "timer_setup.h"
#include "toggleswitch.h"

#define SET_DIRTY() storageDirty(EE_MODEL)

MixEditAdvanced::MixEditAdvanced(int8_t channel, uint8_t index) :
    Page(ICON_MODEL_MIXER, PAD_MEDIUM), channel(channel), index(index)
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

  // Dark FPV theme
  lv_obj_set_style_bg_color(body->getLvObj(), lv_color_make(0x18, 0x18, 0x18), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(body->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_text_color(body->getLvObj(), lv_color_white(), LV_PART_MAIN);

  buildBody(body);
}

#if !NARROW_LAYOUT
static const lv_coord_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1),
                                     LV_GRID_FR(1), LV_GRID_FR(1),
                                     LV_GRID_TEMPLATE_LAST};
static const lv_coord_t row_dsc[] = {LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};
#else
static const lv_coord_t col_dsc[] = {LV_GRID_FR(12), LV_GRID_FR(13),
                                     LV_GRID_TEMPLATE_LAST};
static const lv_coord_t row_dsc[] = {LV_GRID_CONTENT, LV_GRID_CONTENT,
                                     LV_GRID_TEMPLATE_LAST};
#endif

void MixEditAdvanced::buildBody(Window* form)
{
  FlexGridLayout grid(col_dsc, row_dsc, PAD_TINY);
  form->setFlexLayout();

  MixData* mix = mixAddress(index);

  // Advanced...
  FormLine* line;
  StaticText* lbl;

  // Multiplex
  if (index > 0 && mixAddress(index - 1)->destCh == channel) {
    line = form->newLine(grid);
    lv_obj_set_style_bg_color(line->getLvObj(), lv_color_make(0x28, 0x28, 0x28), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(line->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
    line->padAll(PAD_SMALL);
    lbl = new StaticText(line, rect_t{}, STR_MULTPX);
    lv_obj_set_style_text_color(lbl->getLvObj(), lv_color_white(), LV_PART_MAIN);
    auto mltpx = new Choice(line, rect_t{}, STR_VMLTPX, 0, 2, GET_SET_DEFAULT(mix->mltpx));
    applyDarkBtnStyle(mltpx->getLvObj());
  }

  // Flight modes
  if (modelFMEnabled()) {
    line = form->newLine(grid);
    lv_obj_set_style_bg_color(line->getLvObj(), lv_color_make(0x28, 0x28, 0x28), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(line->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
    line->padAll(PAD_SMALL);
    lbl = new StaticText(line, rect_t{}, STR_FLMODE);
    lv_obj_set_style_text_color(lbl->getLvObj(), lv_color_white(), LV_PART_MAIN);
    new FMMatrix<MixData>(line, rect_t{}, mix);
  }

  // Trim
  line = form->newLine(grid);
  lv_obj_set_style_bg_color(line->getLvObj(), lv_color_make(0x28, 0x28, 0x28), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(line->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
  line->padAll(PAD_SMALL);
  lbl = new StaticText(line, rect_t{}, STR_TRIM);
  lv_obj_set_style_text_color(lbl->getLvObj(), lv_color_white(), LV_PART_MAIN);
  auto trimSw = new ToggleSwitch(line, rect_t{}, GET_SET_INVERTED(mix->carryTrim));
  applyDarkBtnStyle(trimSw->getLvObj());

  // Warning
  lbl = new StaticText(line, rect_t{}, STR_MIXWARNING);
  lv_obj_set_style_text_color(lbl->getLvObj(), lv_color_white(), LV_PART_MAIN);
  auto edit = new NumberEdit(line, rect_t{}, 0, 3,
                             GET_SET_DEFAULT(mix->mixWarn));
  edit->setZeroText(STR_OFF);
  applyDarkBtnStyle(edit->getLvObj());

  // Delay up/down precision
#if !NARROW_LAYOUT
  grid.setColSpan(2);
#endif
  line = form->newLine(grid);
  lv_obj_set_style_bg_color(line->getLvObj(), lv_color_make(0x28, 0x28, 0x28), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(line->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
  line->padAll(PAD_SMALL);
  lbl = new StaticText(line, rect_t{}, STR_MIX_DELAY_PREC);
  lv_obj_set_style_text_color(lbl->getLvObj(), lv_color_white(), LV_PART_MAIN);
  auto delayPrec = new Choice(line, rect_t{}, &STR_VPREC[1], 0, 1,
             GET_DEFAULT(mix->delayPrec),
             [=](int newValue) {
              mix->delayPrec = newValue;
              delayUp->clearTextFlag(PREC2);
              delayUp->setTextFlag(mix->delayPrec ? PREC2 : PREC1);
              delayUp->update();
              delayDn->clearTextFlag(PREC2);
              delayDn->setTextFlag(mix->delayPrec ? PREC2 : PREC1);
              delayDn->update();
              SET_DIRTY();
             });
  applyDarkBtnStyle(delayPrec->getLvObj());
#if !NARROW_LAYOUT
  grid.setColSpan(1);
#endif

  // Delay up
  line = form->newLine(grid);
  lv_obj_set_style_bg_color(line->getLvObj(), lv_color_make(0x28, 0x28, 0x28), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(line->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
  line->padAll(PAD_SMALL);
  lbl = new StaticText(line, rect_t{}, STR_DELAYUP);
  lv_obj_set_style_text_color(lbl->getLvObj(), lv_color_white(), LV_PART_MAIN);
  delayUp = new NumberEdit(line, rect_t{}, 0, DELAY_MAX,
                           GET_DEFAULT(mix->delayUp),
                           SET_VALUE(mix->delayUp, newValue), mix->delayPrec ? PREC2 : PREC1);
  delayUp->setSuffix("s");
  applyDarkBtnStyle(delayUp->getLvObj());

  // Delay down
  lbl = new StaticText(line, rect_t{}, STR_DELAYDOWN);
  lv_obj_set_style_text_color(lbl->getLvObj(), lv_color_white(), LV_PART_MAIN);
  delayDn = new NumberEdit(line, rect_t{}, 0, DELAY_MAX,
                           GET_DEFAULT(mix->delayDown),
                           SET_VALUE(mix->delayDown, newValue), mix->delayPrec ? PREC2 : PREC1);
  delayDn->setSuffix("s");
  applyDarkBtnStyle(delayDn->getLvObj());

  // Slow up/down precision
#if !NARROW_LAYOUT
  grid.setColSpan(2);
#endif
  line = form->newLine(grid);
  lv_obj_set_style_bg_color(line->getLvObj(), lv_color_make(0x28, 0x28, 0x28), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(line->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
  line->padAll(PAD_SMALL);
  lbl = new StaticText(line, rect_t{}, STR_MIX_SLOW_PREC);
  lv_obj_set_style_text_color(lbl->getLvObj(), lv_color_white(), LV_PART_MAIN);
  auto slowPrec = new Choice(line, rect_t{}, &STR_VPREC[1], 0, 1,
             GET_DEFAULT(mix->speedPrec),
             [=](int newValue) {
              mix->speedPrec = newValue;
              slowUp->clearTextFlag(PREC2);
              slowUp->setTextFlag(mix->speedPrec ? PREC2 : PREC1);
              slowUp->update();
              slowDn->clearTextFlag(PREC2);
              slowDn->setTextFlag(mix->speedPrec ? PREC2 : PREC1);
              slowDn->update();
              SET_DIRTY();
             });
  applyDarkBtnStyle(slowPrec->getLvObj());
#if !NARROW_LAYOUT
  grid.setColSpan(1);
#endif

  // Slow up
  line = form->newLine(grid);
  lv_obj_set_style_bg_color(line->getLvObj(), lv_color_make(0x28, 0x28, 0x28), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(line->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
  line->padAll(PAD_SMALL);
  lbl = new StaticText(line, rect_t{}, STR_SLOWUP);
  lv_obj_set_style_text_color(lbl->getLvObj(), lv_color_white(), LV_PART_MAIN);
  slowUp = new NumberEdit(line, rect_t{}, 0, DELAY_MAX, GET_DEFAULT(mix->speedUp),
                          SET_VALUE(mix->speedUp, newValue), mix->speedPrec ? PREC2 : PREC1);
  slowUp->setSuffix("s");
  applyDarkBtnStyle(slowUp->getLvObj());

  // Slow down
  lbl = new StaticText(line, rect_t{}, STR_SLOWDOWN);
  lv_obj_set_style_text_color(lbl->getLvObj(), lv_color_white(), LV_PART_MAIN);
  slowDn = new NumberEdit(line, rect_t{}, 0, DELAY_MAX, GET_DEFAULT(mix->speedDown),
                          SET_VALUE(mix->speedDown, newValue), mix->speedPrec ? PREC2 : PREC1);
  slowDn->setSuffix("s");
  applyDarkBtnStyle(slowDn->getLvObj());
}
