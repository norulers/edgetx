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

#include "input_edit.h"

#include "curve_param.h"
#include "curveedit.h"
#include "edgetx.h"
#include "etx_lv_theme.h"
#include "fm_matrix.h"
#include "getset_helpers.h"
#include "gvar_numberedit.h"
#include "input_source.h"
#include "source_numberedit.h"
#include "switchchoice.h"
#include "textedit.h"
#include "timer_setup.h"

#define SET_DIRTY() storageDirty(EE_MODEL)

#if LANDSCAPE
static const lv_coord_t col_dsc[] = {LV_GRID_FR(3), LV_GRID_FR(8),
                                     LV_GRID_TEMPLATE_LAST};
#else
static const lv_coord_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(2),
                                     LV_GRID_TEMPLATE_LAST};
#endif
static const lv_coord_t row_dsc[] = {LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};

class InputEditAdvanced : public Page
{
 public:
  InputEditAdvanced(uint8_t input_n, uint8_t index) : Page(ICON_MODEL_INPUTS)
  {
    std::string title2(getSourceString(MIXSRC_FIRST_INPUT + input_n));
    header->setTitle(STR_MENUINPUTS);
    header->setTitle2(title2);

    // Dark FPV header icons
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(header->getLvObj()); i++) {
      auto child = lv_obj_get_child(header->getLvObj(), i);
      if (lv_obj_check_type(child, &lv_canvas_class))
        lv_obj_add_flag(child, LV_OBJ_FLAG_HIDDEN);
    }
    auto leftBg = new StaticIcon(header, 0, 0, ICON_TOPLEFT_BG, COLOR_THEME_PRIMARY2_INDEX);
    lv_obj_set_style_img_recolor_opa(leftBg->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_img_recolor(leftBg->getLvObj(), lv_color_make(0x22, 0x22, 0x22), LV_PART_MAIN);
    leftBg->setTop((EdgeTxStyles::MENU_HEADER_HEIGHT - leftBg->height()) / 2);
    auto leftIco = new StaticIcon(leftBg, 0, 0, ICON_MODEL_INPUTS, COLOR_THEME_PRIMARY2_INDEX);
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

    FlexGridLayout grid(col_dsc, row_dsc, PAD_TINY);
    body->setFlexLayout();
    body->padAll(PAD_SMALL);

    ExpoData* input = expoAddress(index);

    // Side
    auto line = body->newLine(grid);
    lv_obj_set_style_bg_color(line->getLvObj(), lv_color_make(0x28, 0x28, 0x28), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(line->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
    line->padAll(PAD_SMALL);
    auto lbl = new StaticText(line, rect_t{}, STR_SIDE);
    lv_obj_set_style_text_color(lbl->getLvObj(), lv_color_white(), LV_PART_MAIN);
    auto side = new Choice(
        line, rect_t{}, STR_VCURVEFUNC, 1, 3,
        [=]() -> int16_t { return 4 - input->mode; },
        [=](int16_t newValue) {
          input->mode = 4 - newValue;
          Messaging::send(Messaging::CURVE_UPDATE);
          SET_DIRTY();
        });
    applyDarkBtnStyle(side->getLvObj());

    // Trim
    line = body->newLine(grid);
    lv_obj_set_style_bg_color(line->getLvObj(), lv_color_make(0x28, 0x28, 0x28), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(line->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
    line->padAll(PAD_SMALL);
    lbl = new StaticText(line, rect_t{}, STR_TRIM);
    lv_obj_set_style_text_color(lbl->getLvObj(), lv_color_white(), LV_PART_MAIN);
    const auto trimLast = TRIM_OFF + keysGetMaxTrims() - 1;
    auto c = new Choice(line, rect_t{}, -TRIM_OFF, trimLast,
                        GET_VALUE(-input->trimSource),
                        SET_VALUE(input->trimSource, -newValue));
    applyDarkBtnStyle(c->getLvObj());

    uint16_t srcRaw = input->srcRaw;
    c->setAvailableHandler([=](int value) {
      return value != TRIM_ON || srcRaw <= MIXSRC_LAST_STICK;
    });
    c->setTextHandler([=](int value) -> std::string {
      return getTrimSourceLabel(srcRaw, -value);
    });

    // Flight modes
    if (modelFMEnabled()) {
      line = body->newLine(grid);
      lv_obj_set_style_bg_color(line->getLvObj(), lv_color_make(0x28, 0x28, 0x28), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(line->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
      line->padAll(PAD_SMALL);
      lbl = new StaticText(line, rect_t{}, STR_FLMODE);
      lv_obj_set_style_text_color(lbl->getLvObj(), lv_color_white(), LV_PART_MAIN);
      new FMMatrix<ExpoData>(line, rect_t{}, input);
    }
  }
};

InputEditWindow::InputEditWindow(int8_t input, uint8_t index) :
    Page(ICON_MODEL_INPUTS), input(input), index(index)
{
  header->setTitle(STR_MENUINPUTS);
  headerSwitchName = header->setTitle2("");

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
  auto leftIco = new StaticIcon(leftBg, 0, 0, ICON_MODEL_INPUTS, COLOR_THEME_PRIMARY2_INDEX);
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

  etx_txt_color(headerSwitchName->getLvObj(), COLOR_THEME_ACTIVE_INDEX,
                LV_STATE_USER_1);
  etx_font(headerSwitchName->getLvObj(), FONT_BOLD_INDEX, LV_STATE_USER_1);

  setTitle();

#if PORTRAIT
  body->padAll(PAD_ZERO);

  auto box = new Window(body, rect_t{0, 0, body->width(), body->height() - INPUT_EDIT_CURVE_HEIGHT - PAD_TINY * 2});
  auto box_obj = box->getLvObj();
  etx_scrollbar(box_obj);
  box->padAll(PAD_SMALL);

  auto form = new Window(box, rect_t{});
  buildBody(form);

  preview = new Curve(
      body, rect_t{(LCD_W - INPUT_EDIT_CURVE_WIDTH) / 2, body->height() - INPUT_EDIT_CURVE_HEIGHT - PAD_TINY, INPUT_EDIT_CURVE_WIDTH, INPUT_EDIT_CURVE_HEIGHT},
      [=](int x) -> int {
        ExpoData* line = expoAddress(index);
        int16_t anas[MAX_INPUTS] = {0};
        applyExpos(anas, e_perout_mode_inactive_flight_mode, line->srcRaw, x);
        return anas[line->chn];
      },
      [=]() -> int { return getValue(expoAddress(index)->srcRaw); });
#else
  body->padAll(PAD_SMALL);
  buildBody(body);

  preview = new Curve(
      this, rect_t{LCD_W - INPUT_EDIT_CURVE_WIDTH - PAD_LARGE, EdgeTxStyles::MENU_HEADER_HEIGHT + PAD_TINY, INPUT_EDIT_CURVE_WIDTH, INPUT_EDIT_CURVE_HEIGHT},
      [=](int x) -> int {
        ExpoData* line = expoAddress(index);
        int16_t anas[MAX_INPUTS] = {0};
        applyExpos(anas, e_perout_mode_inactive_flight_mode, line->srcRaw, x);
        return anas[line->chn];
      },
      [=]() -> int { return getValue(expoAddress(index)->srcRaw); });
#endif
}

void InputEditWindow::setTitle()
{
  headerSwitchName->setText(getSourceString(MIXSRC_FIRST_INPUT + input));
}

void InputEditWindow::buildBody(Window* form)
{
  FlexGridLayout grid(col_dsc, row_dsc, PAD_TINY);
  form->setFlexLayout(LV_FLEX_FLOW_COLUMN, PAD_ZERO);

  ExpoData* input = expoAddress(index);

  // Input Name
  auto line = form->newLine(grid);
  lv_obj_set_style_bg_color(line->getLvObj(), lv_color_make(0x28, 0x28, 0x28), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(line->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
  line->padAll(PAD_SMALL);
  auto lbl = new StaticText(line, rect_t{}, STR_INPUTNAME);
  lv_obj_set_style_text_color(lbl->getLvObj(), lv_color_white(), LV_PART_MAIN);
  auto nameEdit = new ModelTextEdit(line, rect_t{}, g_model.inputNames[input->chn],
                    LEN_INPUT_NAME,
                    [=]() {
                      setTitle();
                    });
  applyDarkBtnStyle(nameEdit->getLvObj());

  // Line Name
  line = form->newLine(grid);
  lv_obj_set_style_bg_color(line->getLvObj(), lv_color_make(0x28, 0x28, 0x28), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(line->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
  line->padAll(PAD_SMALL);
  lbl = new StaticText(line, rect_t{}, STR_EXPONAME);
  lv_obj_set_style_text_color(lbl->getLvObj(), lv_color_white(), LV_PART_MAIN);
  auto lineEdit = new ModelTextEdit(line, rect_t{}, input->name, LEN_EXPOMIX_NAME);
  applyDarkBtnStyle(lineEdit->getLvObj());

  // Source
  line = form->newLine(grid);
  lv_obj_set_style_bg_color(line->getLvObj(), lv_color_make(0x28, 0x28, 0x28), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(line->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
  line->padAll(PAD_SMALL);
  lbl = new StaticText(line, rect_t{}, STR_SOURCE);
  lv_obj_set_style_text_color(lbl->getLvObj(), lv_color_white(), LV_PART_MAIN);
  auto src = new InputSource(line, input);
  lv_obj_set_style_grid_cell_x_align(src->getLvObj(), LV_GRID_ALIGN_STRETCH, 0);
  applyDarkBtnStyle(src->getLvObj());

  // Weight
  line = form->newLine(grid);
  lv_obj_set_style_bg_color(line->getLvObj(), lv_color_make(0x28, 0x28, 0x28), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(line->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
  line->padAll(PAD_SMALL);
  lbl = new StaticText(line, rect_t{}, STR_WEIGHT);
  lv_obj_set_style_text_color(lbl->getLvObj(), lv_color_white(), LV_PART_MAIN);
  auto gvar =
      new SourceNumberEdit(line, -100, 100, GET_DEFAULT(input->weight),
                           [=](int32_t newValue) {
                             input->weight = newValue;
                             updatePreview = true;
                             SET_DIRTY();
                           }, MIXSRC_FIRST);
  gvar->setSuffix("%");
  applyDarkBtnStyle(gvar->getLvObj());

  // Offset
  line = form->newLine(grid);
  lv_obj_set_style_bg_color(line->getLvObj(), lv_color_make(0x28, 0x28, 0x28), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(line->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
  line->padAll(PAD_SMALL);
  lbl = new StaticText(line, rect_t{}, STR_OFFSET);
  lv_obj_set_style_text_color(lbl->getLvObj(), lv_color_white(), LV_PART_MAIN);
  gvar = new SourceNumberEdit(line, -100, 100,
                              GET_DEFAULT(input->offset), [=](int32_t newValue) {
                                input->offset = newValue;
                                updatePreview = true;
                                SET_DIRTY();
                              }, MIXSRC_FIRST);
  gvar->setSuffix("%");
  applyDarkBtnStyle(gvar->getLvObj());

  // Switch
  line = form->newLine(grid);
  lv_obj_set_style_bg_color(line->getLvObj(), lv_color_make(0x28, 0x28, 0x28), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(line->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
  line->padAll(PAD_SMALL);
  lbl = new StaticText(line, rect_t{}, STR_SWITCH);
  lv_obj_set_style_text_color(lbl->getLvObj(), lv_color_white(), LV_PART_MAIN);
  auto sw = new SwitchChoice(line, rect_t{}, SWSRC_FIRST_IN_MIXES, SWSRC_LAST_IN_MIXES,
                   GET_DEFAULT(input->swtch),
                   [=](int newValue) {
                     input->swtch = newValue;
                     updatePreview = true;
                     SET_DIRTY();
                   });
  applyDarkBtnStyle(sw->getLvObj());

  // Curve
  line = form->newLine(grid);
  lv_obj_set_style_bg_color(line->getLvObj(), lv_color_make(0x28, 0x28, 0x28), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(line->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
  line->padAll(PAD_SMALL);
  lbl = new StaticText(line, rect_t{}, STR_CURVE);
  lv_obj_set_style_text_color(lbl->getLvObj(), lv_color_white(), LV_PART_MAIN);
  auto param =
      new CurveParam(line, rect_t{}, &input->curve,
        [=](int32_t newValue) {
          input->curve.value = newValue;
          updatePreview = true;
          SET_DIRTY();
        }, MIXSRC_FIRST, input->srcRaw);
  lv_obj_set_style_grid_cell_x_align(param->getLvObj(), LV_GRID_ALIGN_STRETCH,
                                     0);
  applyDarkBtnStyle(param->getLvObj());

  line = form->newLine(grid);
  line->padAll(PAD_LARGE);
  auto btn =
      new TextButton(line, rect_t{}, LV_SYMBOL_SETTINGS, [=]() -> uint8_t {
        new InputEditAdvanced(this->input, index);
        return 0;
      });
  lv_obj_set_width(btn->getLvObj(), lv_pct(100));
  applyDarkBtnStyle(btn->getLvObj());
}

void InputEditWindow::checkEvents()
{
  if (_deleted) return;

  ExpoData* input = expoAddress(index);

  getvalue_t val;
  SourceNumVal v;

  v.rawValue = input->weight;
  if (v.isSource) {
    val = getValue(v.value);
    if (val != lastWeightVal) {
      lastWeightVal = val;
      updatePreview = true;
    }
  }

  v.rawValue = input->offset;
  if (v.isSource) {
    val = getValue(v.value);
    if (val != lastOffsetVal) {
      lastOffsetVal = val;
      updatePreview = true;
    }
  }

  v.rawValue = input->curve.value;
  if (v.isSource) {
    val = getValue(v.value);
    if (val != lastCurveVal) {
      lastCurveVal = val;
      updatePreview = true;
    }
  }

  uint8_t activeIdx = 255;
  for (int i = 0; i < MAX_EXPOS; i += 1) {
    auto inp = expoAddress(i);
    if (inp->chn == input->chn) {
      if (getSwitch(inp->swtch)) {
        activeIdx = i;
        break;
      }
    }
  }
  if (activeIdx != lastActiveIndex) {
    updatePreview = true;
    lastActiveIndex = activeIdx;
  }

  if (lastActiveIndex == index) {
    lv_obj_add_state(headerSwitchName->getLvObj(), LV_STATE_USER_1);
  } else {
    lv_obj_clear_state(headerSwitchName->getLvObj(), LV_STATE_USER_1);
  }

  if (updatePreview) {
    updatePreview = false;
    Messaging::send(Messaging::CURVE_UPDATE);
  }

  Page::checkEvents();
}
