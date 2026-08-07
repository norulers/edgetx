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

#include "model_flightmodes.h"

#include "button.h"
#include "edgetx.h"
#include "etx_lv_theme.h"
#include "getset_helpers.h"
#include "list_line_button.h"
#include "numberedit.h"
#include "page.h"
#include "switchchoice.h"
#include "textedit.h"
#include "timer_setup.h"

#define SET_DIRTY() storageDirty(EE_MODEL)

static std::string getFMTrimStr(uint8_t mode, bool spacer)
{
  mode &= 0x1F;
  if (mode == TRIM_MODE_NONE) return "-";
  if (mode == TRIM_MODE_3POS) return "3P";
  std::string str((mode & 1) ? "+" : "=");
  if (spacer) str += " ";
  mode >>= 1;
  if (mode > MAX_FLIGHT_MODES - 1) mode = MAX_FLIGHT_MODES - 1;
  str += '0' + mode;
  return str;
}

static const lv_coord_t line_col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1),
                                          LV_GRID_TEMPLATE_LAST};

static const lv_coord_t line_row_dsc[] = {LV_GRID_CONTENT,
                                          LV_GRID_TEMPLATE_LAST};

#if LANDSCAPE
static const lv_coord_t trims_col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1),
                                           LV_GRID_TEMPLATE_LAST};
#else
static const lv_coord_t trims_col_dsc[] = {LV_GRID_FR(1),
                                           LV_GRID_TEMPLATE_LAST};
#endif

class TrimEdit : public Window
{
 public:
  TrimEdit(Window* parent, int trimId, int fmId) :
      Window(parent, rect_t{}), trimId(trimId), fmId(fmId)
  {
    setWindowFlag(NO_FOCUS);

    padAll(PAD_TINY);
    setFlexLayout(LV_FLEX_FLOW_ROW, PAD_TINY, LV_SIZE_CONTENT);

    trim_t* tr = &g_model.flightModeData[fmId].trim[trimId];

    lastTrim = tr->value;

    auto tr_btn = new TextButton(
        this, rect_t{0, 0, TR_BTN_W, 0}, getSourceString(MIXSRC_FIRST_TRIM + trimId),
        [=]() {
          tr->mode = (tr->mode == TRIM_MODE_NONE) ? 0 : TRIM_MODE_NONE;
          tr_mode->setValue(tr->mode);
          showControls();
          SET_DIRTY();
          return tr->mode == 0;
        });
    applyDarkBtnStyle(tr_btn->getLvObj());

    if (tr->mode != TRIM_MODE_NONE) tr_btn->check();

    tr_mode = new Choice(this, rect_t{0, 0, EdgeTxStyles::EDIT_FLD_WIDTH_NARROW, 0}, 0, 2 * MAX_FLIGHT_MODES,
                         GET_DEFAULT(tr->mode), [=](int val) {
                           tr->mode = val;
                           showControls();
                           SET_DIRTY();
                         });
    applyDarkBtnStyle(tr_mode->getLvObj());
    tr_mode->setTextHandler(
        [=](uint8_t mode) { return getFMTrimStr(mode, true); });
    tr_mode->setAvailableHandler([=](int mode) {
      if (fmId > 0)
        return ((mode & 1) == 0) || ((mode >> 1) != fmId) ||
               (mode == TRIM_MODE_3POS);
      return (mode == 0) || (mode == TRIM_MODE_3POS);
    });

    tr_value = new NumberEdit(
        this, rect_t{0, 0, EdgeTxStyles::EDIT_FLD_WIDTH_NARROW, 0}, g_model.extendedTrims ? -512 : -128,
        g_model.extendedTrims ? 512 : 128, GET_SET_DEFAULT(tr->value));
    applyDarkBtnStyle(tr_value->getLvObj());

    showControls();
  }

  static LAYOUT_VAL_SCALED(TR_BTN_W, 58)

 protected:
  int trimId;
  int fmId;
  int lastTrim;
  Choice* tr_mode = {nullptr};
  NumberEdit* tr_value = {nullptr};

  void showControls()
  {
    uint8_t mode = g_model.flightModeData[fmId].trim[trimId].mode;

    bool checked = (mode != TRIM_MODE_NONE);
    bool showValue = (fmId == 0 && mode != TRIM_MODE_3POS) || ((mode & 1) || (mode >> 1 == fmId));

    tr_mode->show(checked);
    tr_value->show(checked && showValue);
  }

  void checkEvents() override
  {
    const auto& fm = g_model.flightModeData[fmId];
    if (lastTrim != fm.trim[trimId].value) {
      lastTrim = fm.trim[trimId].value;
      tr_value->setValue(lastTrim);
    }
    Window::checkEvents();
  }
};

class FlightModeEdit : public Page
{
 public:
  FlightModeEdit(uint8_t index) : Page(ICON_MODEL_FLIGHT_MODES), index(index)
  {
    std::string title2 = std::string(STR_FM) + std::to_string(index);
    header->setTitle(STR_MENUFLIGHTMODES);
    header->setTitle2(title2);

    // Dark FPV theme background
    lv_obj_set_style_bg_color(body->getLvObj(), lv_color_make(0x18, 0x18, 0x18),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(body->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);

    FlexGridLayout grid(line_col_dsc, line_row_dsc, PAD_TINY);
    body->setFlexLayout();

    FlightModeData* p_fm = &g_model.flightModeData[index];

    // Flight mode name
    auto line = body->newLine(grid);
    lv_obj_set_style_bg_color(line->getLvObj(), lv_color_make(0x28, 0x28, 0x28),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(line->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
    line->padAll(PAD_SMALL);
    auto lbl = new StaticText(line, rect_t{}, STR_NAME);
    lv_obj_set_style_text_color(lbl->getLvObj(), lv_color_white(), LV_PART_MAIN);
    auto te = new ModelTextEdit(line, rect_t{}, p_fm->name, LEN_FLIGHT_MODE_NAME);
    applyDarkBtnStyle(te->getLvObj());

    if (index > 0) {
      // Switch
      line = body->newLine(grid);
      lv_obj_set_style_bg_color(line->getLvObj(), lv_color_make(0x28, 0x28, 0x28),
                                LV_PART_MAIN);
      lv_obj_set_style_bg_opa(line->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
      line->padAll(PAD_SMALL);
      lbl = new StaticText(line, rect_t{}, STR_SWITCH);
      lv_obj_set_style_text_color(lbl->getLvObj(), lv_color_white(), LV_PART_MAIN);
      auto sw = new SwitchChoice(line, rect_t{}, SWSRC_FIRST_IN_MIXES,
                       SWSRC_LAST_IN_MIXES, GET_SET_DEFAULT(p_fm->swtch));
      applyDarkBtnStyle(sw->getLvObj());
    }

    // Fade in
    line = body->newLine(grid);
    lv_obj_set_style_bg_color(line->getLvObj(), lv_color_make(0x28, 0x28, 0x28),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(line->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
    line->padAll(PAD_SMALL);
    lbl = new StaticText(line, rect_t{}, STR_FADEIN);
    lv_obj_set_style_text_color(lbl->getLvObj(), lv_color_white(), LV_PART_MAIN);
    auto fi = new NumberEdit(line, rect_t{}, 0, DELAY_MAX, GET_DEFAULT(p_fm->fadeIn),
                   SET_VALUE(p_fm->fadeIn, newValue), PREC1);
    applyDarkBtnStyle(fi->getLvObj());

    // Fade out
    line = body->newLine(grid);
    lv_obj_set_style_bg_color(line->getLvObj(), lv_color_make(0x28, 0x28, 0x28),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(line->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
    line->padAll(PAD_SMALL);
    lbl = new StaticText(line, rect_t{}, STR_FADEOUT);
    lv_obj_set_style_text_color(lbl->getLvObj(), lv_color_white(), LV_PART_MAIN);
    auto fo = new NumberEdit(line, rect_t{}, 0, DELAY_MAX, GET_DEFAULT(p_fm->fadeOut),
                   SET_VALUE(p_fm->fadeOut, newValue), PREC1);
    applyDarkBtnStyle(fo->getLvObj());

    // Trims
    line = body->newLine(grid);
    lv_obj_set_style_bg_color(line->getLvObj(), lv_color_make(0x28, 0x28, 0x28),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(line->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
    line->padAll(PAD_SMALL);
    lbl = new StaticText(line, rect_t{}, STR_TRIMS);
    lv_obj_set_style_text_color(lbl->getLvObj(), lv_color_white(), LV_PART_MAIN);

    FlexGridLayout trim_grid(trims_col_dsc, line_row_dsc, PAD_SMALL);

    for (int t = 0; t < keysGetMaxTrims(); t++) {
      if ((t % TRIMS_PER_LINE) == 0) {
        line = body->newLine(trim_grid);
        line->padAll(PAD_TINY);
      }

      new TrimEdit(line, t, index);
    }

    // Style header icon to match model setup FPV theme (dark bg + orange icon)
    lv_obj_t* hdr = header->getLvObj();
    // Hide original canvas-based HeaderIcon/HeaderBackIcon
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(hdr); i++) {
      auto child = lv_obj_get_child(hdr, i);
      if (lv_obj_check_type(child, &lv_canvas_class))
        lv_obj_add_flag(child, LV_OBJ_FLAG_HIDDEN);
    }
    // Left: dark bg shape + orange flight modes icon
    auto leftBg = new StaticIcon(header, 0, 0, ICON_TOPLEFT_BG, COLOR_THEME_PRIMARY2_INDEX);
    lv_obj_set_style_img_recolor_opa(leftBg->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_img_recolor(leftBg->getLvObj(), lv_color_make(0x22, 0x22, 0x22), LV_PART_MAIN);
    leftBg->setTop((EdgeTxStyles::MENU_HEADER_HEIGHT - leftBg->height()) / 2);
    auto leftIco = new StaticIcon(leftBg, 0, 0, ICON_MODEL_FLIGHT_MODES, COLOR_THEME_PRIMARY2_INDEX);
    lv_obj_set_style_img_recolor_opa(leftIco->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_img_recolor(leftIco->getLvObj(), lv_color_make(0xFF, 0x8C, 0x00), LV_PART_MAIN);
    leftIco->center(leftBg->width() + PAD_MEDIUM, leftBg->height());
    // Right: dark bg shape + orange close icon
    auto rightBg = new StaticIcon(header, LCD_W, 0, ICON_TOPRIGHT_BG, COLOR_THEME_PRIMARY2_INDEX);
    lv_obj_set_style_img_recolor_opa(rightBg->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_img_recolor(rightBg->getLvObj(), lv_color_make(0x22, 0x22, 0x22), LV_PART_MAIN);
    rightBg->setPos(LCD_W - rightBg->width(),
                    (EdgeTxStyles::MENU_HEADER_HEIGHT - rightBg->height()) / 2);
    auto rightIco = new StaticIcon(rightBg, 0, 0, ICON_BTN_CLOSE, COLOR_THEME_PRIMARY2_INDEX);
    lv_obj_set_style_img_recolor_opa(rightIco->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_img_recolor(rightIco->getLvObj(), lv_color_make(0xFF, 0x8C, 0x00), LV_PART_MAIN);
    rightIco->center(rightBg->width() + PAD_MEDIUM, rightBg->height());
  }

  static LAYOUT_SIZE(TRIMS_PER_LINE, 2, 1)

 protected:
  uint8_t index;
};

class FlightModeBtn : public ListLineButton
{
 public:
  FlightModeBtn(Window* parent, int index) : ListLineButton(parent, index)
  {
    padAll(PAD_ZERO);
    setHeight(BTN_H);
    applyDarkBtnStyle(lvobj);

    delayLoad();
  }

  void delayedInit() override
  {
    lv_obj_enable_style_refresh(false);

    check(isActive());

    fmID = etx_create(&fm_id_class, lvobj);
    lv_obj_set_pos(fmID, FMID_X, FMID_Y);
    char label[8];
    getFlightModeString(label, index + 1);
    lv_label_set_text(fmID, label);

    fmName = etx_create(&fm_name_class, lvobj);
    lv_obj_set_pos(fmName, NAME_X, NAME_Y);
    fmSwitch = etx_create(&fm_switch_class, lvobj);
    lv_obj_set_pos(fmSwitch, SWTCH_X, SWTCH_Y);

    for (int i = 0; i < keysGetMaxTrims() && i < MAX_FMTRIMS; i += 1) {
      fmTrimMode[i] = etx_create(&fm_trim_mode_class, lvobj);
      lv_obj_set_pos(fmTrimMode[i], TRIM_X + i * TRIM_W, TRIM_Y);
      fmTrimValue[i] = etx_create(&fm_trim_value_class, lvobj);
      lv_obj_set_pos(fmTrimValue[i], TRIM_X + i * TRIM_W, TRIM_Y + TRIM_H);
    }

    fmFadeIn = etx_create(&fm_fade_class, lvobj);
    lv_obj_set_pos(fmFadeIn, FADE_X, FADE_Y);
    fmFadeOut = etx_create(&fm_fade_class, lvobj);
    lv_obj_set_pos(fmFadeOut, FADE_X + FADE_W + PAD_TINY, FADE_Y);
    lv_obj_update_layout(lvobj);

    lv_obj_enable_style_refresh(true);
    lv_obj_refresh_style(lvobj, LV_PART_ANY, LV_STYLE_PROP_ANY);

    refresh();
  }

  bool isActive() const override { return (getFlightMode() == index); }

  void setTrimValue(uint8_t t)
  {
    lastTrim[t] = g_model.flightModeData[index].trim[t].value;

    uint8_t mode = g_model.flightModeData[index].trim[t].mode;
    bool checked = (mode != TRIM_MODE_NONE);
    bool showValue = (index == 0) || ((mode & 1) || (mode >> 1 == index));

    if (checked && showValue)
      lv_label_set_text(fmTrimValue[t],
                        formatNumberAsString(lastTrim[t]).c_str());
    else
      lv_label_set_text(fmTrimValue[t], "");
  }

  void checkEvents() override
  {
    ListLineButton::checkEvents();
    if (!refreshing && loaded) {
      refreshing = true;
      for (int t = 0; t < keysGetMaxTrims() && t < MAX_FMTRIMS; t += 1) {
        if (lastTrim[t] != g_model.flightModeData[index].trim[t].value) {
          setTrimValue(t);
        }
      }
      refreshing = false;
    }
  }

  void refresh() override
  {
    if (!loaded) return;

    const auto& fm = g_model.flightModeData[index];

    if (fm.name[0] != '\0') {
      lv_label_set_text(fmName, fm.name);
    } else {
      lv_label_set_text(fmName, "");
    }

    if ((index > 0) && (fm.swtch != SWSRC_NONE)) {
      char label[16];
      getSwitchPositionName(label, fm.swtch);
      lv_label_set_text(fmSwitch, label);
    } else {
      lv_label_set_text(fmSwitch, "");
    }

    for (int i = 0; i < keysGetMaxTrims() && i < MAX_FMTRIMS; i += 1) {
      setTrimValue(i);
      lv_label_set_text(fmTrimMode[i], getFMTrimStr(fm.trim[i].mode, false).c_str());
    }

    lv_label_set_text(
        fmFadeIn,
        formatNumberAsString(fm.fadeIn, PREC1, 0, nullptr, "s").c_str());
    lv_label_set_text(
        fmFadeOut,
        formatNumberAsString(fm.fadeOut, PREC1, 0, nullptr, "s").c_str());
  }

  static LAYOUT_SIZE_SCALED(BTN_H, 36, 56)
  static LAYOUT_SIZE(MAX_FMTRIMS, 6, 4)
  static constexpr coord_t FMID_X = PAD_TINY;
  static LAYOUT_SIZE_SCALED(FMID_Y, 6, 16)
  static LAYOUT_SIZE_SCALED(FMID_W, 40, 46)
  static constexpr coord_t NAME_X = FMID_X + FMID_W + PAD_TINY;
  static LAYOUT_SIZE_SCALED(NAME_Y, 6, 0)
  static LAYOUT_SIZE_SCALED(NAME_W, 95, 160)
  static constexpr coord_t SWTCH_X = NAME_X + NAME_W + PAD_TINY;
  static LAYOUT_SIZE_SCALED(SWTCH_Y, 6, 0)
  static LAYOUT_VAL_SCALED(SWTCH_W, 50)
  static LAYOUT_SIZE(TRIM_X, SWTCH_X + SWTCH_W + PAD_TINY, FMID_X + FMID_W + PAD_TINY)
  static LAYOUT_SIZE_SCALED(TRIM_Y, 0, 20)
  static LAYOUT_SIZE_SCALED(TRIM_W, 30, 40)
  static LAYOUT_VAL_SCALED(TRIM_H, 16)
  static constexpr coord_t TRIMC_W = MAX_FMTRIMS * TRIM_W;
  static LAYOUT_VAL_SCALED(FADE_W, 45)
  static LAYOUT_SIZE_SCALED(FADE_Y, 6, 24)
  static constexpr coord_t FADE_X = ListLineButton::GRP_W - PAD_BORDER * 2 - FADE_W * 2 - PAD_TINY * 2;

 protected:
  bool refreshing = false;

  lv_obj_t* fmID = nullptr;
  lv_obj_t* fmName = nullptr;
  lv_obj_t* fmSwitch = nullptr;
  lv_obj_t* fmTrimMode[MAX_FMTRIMS] = {nullptr};
  lv_obj_t* fmTrimValue[MAX_FMTRIMS] = {nullptr};
  lv_obj_t* fmFadeIn = nullptr;
  lv_obj_t* fmFadeOut = nullptr;
  int lastTrim[MAX_FMTRIMS] = {0};

  static const lv_obj_class_t fm_id_class;
  static const lv_obj_class_t fm_name_class;
  static const lv_obj_class_t fm_switch_class;
  static const lv_obj_class_t fm_fade_class;
  static const lv_obj_class_t fm_trim_mode_class;
  static const lv_obj_class_t fm_trim_value_class;
};

static void fm_id_constructor(const lv_obj_class_t* class_p, lv_obj_t* obj)
{
  etx_obj_add_style(obj, styles->text_align_left, LV_PART_MAIN);
  lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
}

const lv_obj_class_t FlightModeBtn::fm_id_class = {
    .base_class = &lv_label_class,
    .constructor_cb = fm_id_constructor,
    .destructor_cb = nullptr,
    .user_data = nullptr,
    .event_cb = nullptr,
    .width_def = FlightModeBtn::FMID_W,
    .height_def = EdgeTxStyles::STD_FONT_HEIGHT,
    .editable = LV_OBJ_CLASS_EDITABLE_INHERIT,
    .group_def = LV_OBJ_CLASS_GROUP_DEF_INHERIT,
    .instance_size = sizeof(lv_label_t),
};

static void fm_name_constructor(const lv_obj_class_t* class_p, lv_obj_t* obj)
{
  etx_obj_add_style(obj, styles->text_align_left, LV_PART_MAIN);
  etx_font(obj, FONT_XS_INDEX);
  lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
}

const lv_obj_class_t FlightModeBtn::fm_name_class = {
    .base_class = &lv_label_class,
    .constructor_cb = fm_name_constructor,
    .destructor_cb = nullptr,
    .user_data = nullptr,
    .event_cb = nullptr,
    .width_def = FlightModeBtn::NAME_W,
    .height_def = EdgeTxStyles::STD_FONT_HEIGHT,
    .editable = LV_OBJ_CLASS_EDITABLE_INHERIT,
    .group_def = LV_OBJ_CLASS_GROUP_DEF_INHERIT,
    .instance_size = sizeof(lv_label_t),
};

static void fm_switch_constructor(const lv_obj_class_t* class_p, lv_obj_t* obj)
{
  etx_obj_add_style(obj, styles->text_align_left, LV_PART_MAIN);
  lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
}

const lv_obj_class_t FlightModeBtn::fm_switch_class = {
    .base_class = &lv_label_class,
    .constructor_cb = fm_switch_constructor,
    .destructor_cb = nullptr,
    .user_data = nullptr,
    .event_cb = nullptr,
    .width_def = FlightModeBtn::SWTCH_W,
    .height_def = EdgeTxStyles::STD_FONT_HEIGHT,
    .editable = LV_OBJ_CLASS_EDITABLE_INHERIT,
    .group_def = LV_OBJ_CLASS_GROUP_DEF_INHERIT,
    .instance_size = sizeof(lv_label_t),
};

static void fm_fade_constructor(const lv_obj_class_t* class_p, lv_obj_t* obj)
{
  etx_obj_add_style(obj, styles->text_align_right, LV_PART_MAIN);
  lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
}

const lv_obj_class_t FlightModeBtn::fm_fade_class = {
    .base_class = &lv_label_class,
    .constructor_cb = fm_fade_constructor,
    .destructor_cb = nullptr,
    .user_data = nullptr,
    .event_cb = nullptr,
    .width_def = FlightModeBtn::FADE_W,
    .height_def = EdgeTxStyles::STD_FONT_HEIGHT,
    .editable = LV_OBJ_CLASS_EDITABLE_INHERIT,
    .group_def = LV_OBJ_CLASS_GROUP_DEF_INHERIT,
    .instance_size = sizeof(lv_label_t),
};

static void fm_trim_mode_constructor(const lv_obj_class_t* class_p,
                                     lv_obj_t* obj)
{
  etx_obj_add_style(obj, styles->text_align_center, LV_PART_MAIN);
  lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
}

const lv_obj_class_t FlightModeBtn::fm_trim_mode_class = {
    .base_class = &lv_label_class,
    .constructor_cb = fm_trim_mode_constructor,
    .destructor_cb = nullptr,
    .user_data = nullptr,
    .event_cb = nullptr,
    .width_def = FlightModeBtn::TRIM_W,
    .height_def = FlightModeBtn::TRIM_H,
    .editable = LV_OBJ_CLASS_EDITABLE_INHERIT,
    .group_def = LV_OBJ_CLASS_GROUP_DEF_INHERIT,
    .instance_size = sizeof(lv_label_t),
};

static void fm_trim_value_constructor(const lv_obj_class_t* class_p,
                                      lv_obj_t* obj)
{
  etx_obj_add_style(obj, styles->text_align_center, LV_PART_MAIN);
  etx_font(obj, FONT_XS_INDEX);
  lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
}

const lv_obj_class_t FlightModeBtn::fm_trim_value_class = {
    .base_class = &lv_label_class,
    .constructor_cb = fm_trim_value_constructor,
    .destructor_cb = nullptr,
    .user_data = nullptr,
    .event_cb = nullptr,
    .width_def = FlightModeBtn::TRIM_W,
    .height_def = FlightModeBtn::TRIM_H,
    .editable = LV_OBJ_CLASS_EDITABLE_INHERIT,
    .group_def = LV_OBJ_CLASS_GROUP_DEF_INHERIT,
    .instance_size = sizeof(lv_label_t),
};

ModelFlightModesPage::ModelFlightModesPage(const PageDef& pageDef) :
    PageGroupItem(pageDef)
{
}

static const lv_coord_t fmt_col_dsc[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};

static const lv_coord_t fmt_row_dsc[] = {LV_GRID_CONTENT,
                                         LV_GRID_TEMPLATE_LAST};

void ModelFlightModesPage::build(Window* form)
{
  // Style header icon to match flight modes FPV theme
  Window* pg = form->getParent();
  if (pg) {
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(pg->getLvObj()); i++) {
      auto child = lv_obj_get_child(pg->getLvObj(), i);
      bool isHeader = false;
      for (uint32_t j = 0; j < lv_obj_get_child_cnt(child); j++) {
        if (lv_obj_check_type(lv_obj_get_child(child, j), &lv_canvas_class)) {
          isHeader = true;
          break;
        }
      }
      if (isHeader) {
        auto hdrWin = (Window*)lv_obj_get_user_data(child);
        // Hide original canvas-based icons
        for (uint32_t k = 0; k < lv_obj_get_child_cnt(child); k++) {
          auto hc = lv_obj_get_child(child, k);
          if (lv_obj_check_type(hc, &lv_canvas_class))
            lv_obj_add_flag(hc, LV_OBJ_FLAG_HIDDEN);
        }
        // Left: dark bg + orange flight modes icon
        auto leftBg = new StaticIcon(hdrWin, 0, 0, ICON_TOPLEFT_BG, COLOR_THEME_PRIMARY2_INDEX);
        lv_obj_set_style_img_recolor_opa(leftBg->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_img_recolor(leftBg->getLvObj(), lv_color_make(0x22, 0x22, 0x22), LV_PART_MAIN);
        leftBg->setTop((EdgeTxStyles::MENU_HEADER_HEIGHT - leftBg->height()) / 2);
        auto leftIco = new StaticIcon(leftBg, 0, 0, ICON_MODEL_FLIGHT_MODES, COLOR_THEME_PRIMARY2_INDEX);
        lv_obj_set_style_img_recolor_opa(leftIco->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_img_recolor(leftIco->getLvObj(), lv_color_make(0xFF, 0x8C, 0x00), LV_PART_MAIN);
        leftIco->center(leftBg->width() + PAD_MEDIUM, leftBg->height());
        // Right: dark bg + orange close icon
        auto rightBg = new StaticIcon(hdrWin, LCD_W, 0, ICON_TOPRIGHT_BG, COLOR_THEME_PRIMARY2_INDEX);
        lv_obj_set_style_img_recolor_opa(rightBg->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_img_recolor(rightBg->getLvObj(), lv_color_make(0x22, 0x22, 0x22), LV_PART_MAIN);
        rightBg->setPos(LCD_W - rightBg->width(),
                        (EdgeTxStyles::MENU_HEADER_HEIGHT - rightBg->height()) / 2);
        auto rightIco = new StaticIcon(rightBg, 0, 0, ICON_BTN_CLOSE, COLOR_THEME_PRIMARY2_INDEX);
        lv_obj_set_style_img_recolor_opa(rightIco->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_img_recolor(rightIco->getLvObj(), lv_color_make(0xFF, 0x8C, 0x00), LV_PART_MAIN);
        rightIco->center(rightBg->width() + PAD_MEDIUM, rightBg->height());
        break;
      }
    }
  }

  form->padAll(PAD_ZERO);
  form->padBottom(PAD_LARGE);

  for (int i = 0; i < MAX_FLIGHT_MODES; i++) {
    auto btn = new FlightModeBtn(form, i);
    lv_obj_set_pos(btn->getLvObj(), PAD_SMALL, i * (FlightModeBtn::BTN_H + PAD_THREE) + PAD_SMALL);
    btn->setWidth(ListLineButton::GRP_W);

    btn->setPressHandler([=]() {
      (new FlightModeEdit(i))->setCloseHandler([=]() { btn->refresh(); });
      return btn->isActive();
    });
  }

  trimCheck = new TextButton(
      form, rect_t{6, MAX_FLIGHT_MODES * (FlightModeBtn::BTN_H + PAD_THREE) + PAD_LARGE, ListLineButton::GRP_W, EdgeTxStyles::UI_ELEMENT_HEIGHT}, STR_CHECKTRIMS, [&]() -> uint8_t {
        if (trimsCheckTimer)
          trimsCheckTimer = 0;
        else
          trimsCheckTimer = 200;  // 2 seconds trims cancelled
        return trimsCheckTimer;
      });
  applyDarkBtnStyle(trimCheck->getLvObj());
}

void ModelFlightModesPage::checkEvents()
{
  trimCheck->check(trimsCheckTimer > 0);
}
