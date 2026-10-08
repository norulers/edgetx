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

#include "radio_hardware.h"

#include "edgetx.h"
#include "getset_helpers.h"
#include "hal/adc_driver.h"
#include "hw_extmodule.h"
#include "hw_inputs.h"
#include "hw_intmodule.h"
#include "hw_serial.h"
#include "numberedit.h"
#include "radio_calibration.h"
#include "radio_diaganas.h"
#include "radio_diagkeys.h"
#include "radio_setup.h"

#if defined(FUNCTION_SWITCHES)
#include "radio_cfs.h"
#include "radio_diagcustswitches.h"
#endif

#if defined(BLUETOOTH)
#include "hw_bluetooth.h"
#endif

#define SET_DIRTY() storageDirty(EE_GENERAL)

static void radioHardwareSubtitle(Window* parent, const char* text)
{
  new StaticText(parent, rect_t{}, text, COLOR_THEME_QM_FG_INDEX, FONT(BOLD));
}

static void styleSetupButtonGroup(SetupButtonGroup* group)
{
  auto obj = group->getLvObj();
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(obj); i += 1) {
    auto child = lv_obj_get_child(obj, i);
    if (!lv_obj_check_type(child, &lv_label_class)) {
      stylePageGroupControl(child);
    }
  }
}

static void styleRadioHardwareObject(lv_obj_t* obj)
{
  if (lv_obj_check_type(obj, &lv_label_class)) {
    lv_obj_set_style_text_color(obj, lv_color_white(), LV_PART_MAIN);
    return;
  } else {
    stylePageGroupControl(obj);
    if (lv_obj_check_type(obj, &lv_textarea_class)) {
      lv_obj_set_style_bg_color(obj, lv_color_make(0x28, 0x28, 0x28), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_set_style_text_color(obj, lv_color_white(), LV_PART_MAIN);
    }
    for (uint32_t gi = 0; gi < lv_obj_get_child_cnt(obj); gi++) {
      lv_obj_t* gc = lv_obj_get_child(obj, gi);
      if (lv_obj_check_type(gc, &lv_img_class)) {
        const void* src = lv_img_get_src(gc);
        if (lv_img_src_get_type(src) == LV_IMG_SRC_SYMBOL) {
          const char* sym = (const char*)src;
          if (strcmp(sym, LV_SYMBOL_DOWN) == 0 || strcmp(sym, LV_SYMBOL_DIRECTORY) == 0) {
            lv_obj_set_style_bg_color(obj, lv_color_make(0x28, 0x28, 0x28), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_text_color(obj, lv_color_white(), LV_PART_MAIN);
            lv_obj_set_style_bg_color(obj, lv_color_make(0xFF, 0x8C, 0x00), LV_PART_MAIN | LV_STATE_FOCUSED);
            lv_obj_set_style_text_color(obj, lv_color_black(), LV_PART_MAIN | LV_STATE_FOCUSED);
            lv_obj_set_style_img_recolor(gc, lv_color_white(), LV_PART_MAIN);
            lv_obj_set_style_img_recolor_opa(gc, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_img_recolor(gc, lv_color_black(), LV_PART_MAIN | LV_STATE_FOCUSED);
            lv_obj_set_style_img_recolor_opa(gc, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_FOCUSED);
            break;
          }
        }
      }
    }
  }

  for (uint32_t i = 0; i < lv_obj_get_child_cnt(obj); i += 1) {
    styleRadioHardwareObject(lv_obj_get_child(obj, i));
  }
}

#if PORTRAIT
static const lv_coord_t col_dsc[] = {LV_GRID_FR(13), LV_GRID_FR(19),
                                     LV_GRID_TEMPLATE_LAST};
#else
static const lv_coord_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(2),
                                     LV_GRID_TEMPLATE_LAST};
#endif

static const lv_coord_t row_dsc[] = {LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};

RadioHardwarePage::RadioHardwarePage(const PageDef& pageDef) :
    PageGroupItem(pageDef, PAD_TINY)
{
  enableVBatBridge();
}

void RadioHardwarePage::checkEvents() { enableVBatBridge(); }

void RadioHardwarePage::cleanup()
{
  disableVBatBridge();
}

class BatCalEdit : public NumberEdit
{
 public:
  BatCalEdit(Window* parent, const rect_t& rect) :
      NumberEdit(parent, rect, -127, 127,
                 GET_SET_DEFAULT(g_eeGeneral.txVoltageCalibration))
  {
    setDisplayHandler([](int32_t value) {
      return formatNumberAsString(getBatteryVoltage(), PREC2, 0, nullptr, "V");
    });
    lastBatVolts = getBatteryVoltage();
  }

 protected:
  uint16_t lastBatVolts = 0;

  void checkEvents() override
  {
    if (getBatteryVoltage() != lastBatVolts) {
      lastBatVolts = getBatteryVoltage();
      setValue(g_eeGeneral.txVoltageCalibration);
    }
  }
};

const static SetupLineDef setupLines[] = {
  {
    // Batt meter range - Range 3.0v to 16v
    STR_DEF(STR_BATTERY_RANGE),
    [](Window* parent, coord_t x, coord_t y) {
      auto batMin = new NumberEdit(
          parent, {x, y, EdgeTxStyles::EDIT_FLD_WIDTH_NARROW, 0}, -60 + 90, g_eeGeneral.vBatMax + 29 + 90,
          GET_SET_WITH_OFFSET(g_eeGeneral.vBatMin, 90), PREC1);
      batMin->setSuffix("V");
      new StaticText(parent, {x + EdgeTxStyles::EDIT_FLD_WIDTH_NARROW + PAD_SMALL, y + PAD_SMALL + 1, PAD_LARGE, EdgeTxStyles::STD_FONT_HEIGHT}, "-");
      auto batMax = new NumberEdit(
          parent, {x + EdgeTxStyles::EDIT_FLD_WIDTH_NARROW + PAD_LARGE + PAD_SMALL, y, EdgeTxStyles::EDIT_FLD_WIDTH_NARROW, 0}, g_eeGeneral.vBatMin - 29 + 120, 40 + 120,
          GET_SET_WITH_OFFSET(g_eeGeneral.vBatMax, 120), PREC1);
      batMax->setSuffix("V");

      batMin->setSetValueHandler([=](int32_t newValue) {
        g_eeGeneral.vBatMin = newValue - 90;
        SET_DIRTY();
        batMax->setMin(g_eeGeneral.vBatMin - 29 + 120);
      });

      batMax->setSetValueHandler([=](int32_t newValue) {
        g_eeGeneral.vBatMax = newValue - 120;
        SET_DIRTY();
        batMin->setMax(g_eeGeneral.vBatMax + 29 + 90);
      });
    }
  },
  {
    // Bat calibration
    STR_DEF(STR_BATT_CALIB),
    [](Window* parent, coord_t x, coord_t y) {
      new BatCalEdit(parent, {x, y, EdgeTxStyles::EDIT_FLD_WIDTH_NARROW, 0});
    }
  },
  {
    // RTC Batt check enable
    STR_DEF(STR_RTC_CHECK),
    [](Window* parent, coord_t x, coord_t y) {
      new ToggleSwitch(parent, {x, y, 0, 0},
                       GET_SET_INVERTED(g_eeGeneral.disableRtcWarning));

      // RTC Batt display
      new DynamicNumber<uint16_t>(
          parent,
          {x + ToggleSwitch::TOGGLE_W + PAD_SMALL, y + PAD_SMALL + 1, 0, 0},
          [] { return getRTCBatteryVoltage(); }, COLOR_THEME_QM_FG_INDEX, PREC2,
          "", "V");
    }
  },
  {
    // ADC filter
    STR_DEF(STR_JITTER_FILTER),
    [](Window* parent, coord_t x, coord_t y) {
      new ToggleSwitch(parent, {x, y, 0, 0}, GET_SET_INVERTED(g_eeGeneral.noJitterFilter));
    }
  },
#if defined(AUDIO_MUTE_GPIO)
  {
    // Mute audio
    STR_DEF(STR_AUDIO_MUTE),
    [](Window* parent, coord_t x, coord_t y) {
      new ToggleSwitch(parent, {x, y, 0, 0}, GET_SET_DEFAULT(g_eeGeneral.audioMuteEnable));
    }
  },
#endif
  {nullptr, nullptr},
};

const static PageButtonDef calibrationButtons[] = {
  {STR_DEF(STR_MENUCALIBRATION), []() { new RadioCalibrationPage(); }},
  {STR_DEF(STR_STICKS), []() { new HWInputDialog<HWSticks>(STR_STICKS); }},
  {STR_DEF(STR_POTS), []() { new HWInputDialog<HWPots>(STR_POTS, HWPots::POTS_WINDOW_WIDTH); }},
  {STR_DEF(STR_SWITCHES), []() { new HWInputDialog<HWSwitches>(STR_SWITCHES, HWSwitches::SW_WINDOW_WIDTH); }},
#if defined(FUNCTION_SWITCHES)
  {STR_DEF(STR_FUNCTION_SWITCHES), []() { new RadioFunctionSwitches(); }},
#endif
  {nullptr},
};

const static PageButtonDef debugButtons[] = {
  {STR_DEF(STR_ANALOGS_BTN), []() { new RadioAnalogsDiagsViewPageGroup(); }},
  {STR_DEF(STR_KEYS_BTN), []() { new RadioKeyDiagsPage(); }},
#if defined(FUNCTION_SWITCHES)
  {STR_DEF(STR_FS_BTN), []() { new RadioCustSwitchesDiagsPage(); }},
#endif
  {nullptr},
};

void RadioHardwarePage::build(Window* window)
{
  window->setFlexLayout(LV_FLEX_FLOW_COLUMN, PAD_TINY);
  window->padBottom(PAD_LARGE);

  // FPV-style header with dark bg shapes + orange icons
  Window* pg = window->getParent();
  Window* hdrWin = nullptr;
  if (pg && lv_obj_get_child_cnt(pg->getLvObj()) > 1) {
    lv_obj_t* hdrLv = lv_obj_get_child(pg->getLvObj(), 1);
    hdrWin = (Window*)lv_obj_get_user_data(hdrLv);
    lv_obj_set_style_bg_color(hdrLv, lv_color_make(0x18, 0x18, 0x18), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(hdrLv, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(hdrLv, LV_GRAD_DIR_NONE, LV_PART_MAIN);
    // Hide original blue canvas-based HeaderIcon/HeaderBackIcon
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(hdrLv); i++) {
      auto child = lv_obj_get_child(hdrLv, i);
      if (lv_obj_check_type(child, &lv_canvas_class))
        lv_obj_add_flag(child, LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (hdrWin) {
    // Left: dark bg shape + orange hardware icon
    auto leftBg = new StaticIcon(hdrWin, 0, 0, ICON_TOPLEFT_BG, COLOR_THEME_PRIMARY2_INDEX);
    lv_obj_set_style_img_recolor_opa(leftBg->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_img_recolor(leftBg->getLvObj(), lv_color_make(0x22, 0x22, 0x22), LV_PART_MAIN);
    leftBg->setTop((EdgeTxStyles::MENU_HEADER_HEIGHT - leftBg->height()) / 2);
    auto leftIco = new StaticIcon(leftBg, 0, 0, ICON_RADIO_HARDWARE, COLOR_THEME_PRIMARY2_INDEX);
    lv_obj_set_style_img_recolor_opa(leftIco->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_img_recolor(leftIco->getLvObj(), lv_color_make(0xFF, 0x8C, 0x00), LV_PART_MAIN);
    leftIco->center(leftBg->width() + PAD_MEDIUM, leftBg->height());

    // Right: dark bg shape + orange close icon
    auto rightBg = new StaticIcon(hdrWin, LCD_W, 0, ICON_TOPRIGHT_BG, COLOR_THEME_PRIMARY2_INDEX);
    lv_obj_set_style_img_recolor_opa(rightBg->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_img_recolor(rightBg->getLvObj(), lv_color_make(0x22, 0x22, 0x22), LV_PART_MAIN);
    rightBg->setPos(LCD_W - rightBg->width(),
                    (EdgeTxStyles::MENU_HEADER_HEIGHT - rightBg->height()) / 2);
    auto rightIco = new StaticIcon(rightBg, 0, 0, ICON_BTN_CLOSE, COLOR_THEME_PRIMARY2_INDEX);
    lv_obj_set_style_img_recolor_opa(rightIco->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_img_recolor(rightIco->getLvObj(), lv_color_make(0xFF, 0x8C, 0x00), LV_PART_MAIN);
    rightIco->center(rightBg->width() + PAD_MEDIUM, rightBg->height());
  }

  // FPV dark theme
  lv_obj_t* win = window->getLvObj();
  window->setWindowFlag(OPAQUE);
  lv_obj_set_style_bg_color(win, lv_color_make(0x18, 0x18, 0x18), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(win, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_grad_dir(win, LV_GRAD_DIR_NONE, LV_PART_MAIN);
  // Remove gradient from parent (PageGroupBase)
  if (pg) {
    lv_obj_t* plv = pg->getLvObj();
    lv_obj_set_style_bg_color(plv, lv_color_make(0x18, 0x18, 0x18), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(plv, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(plv, LV_GRAD_DIR_NONE, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(plv, lv_color_make(0x18, 0x18, 0x18), LV_PART_MAIN);
    lv_obj_set_style_bg_main_stop(plv, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_stop(plv, 255, LV_PART_MAIN);
  }

  SetupLine::showLines(window, 0, SubPage::EDT_X, padding, setupLines);

  FlexGridLayout grid(col_dsc, row_dsc, PAD_TINY);

#if defined(HARDWARE_INTERNAL_MODULE)
  radioHardwareSubtitle(window, STR_INTERNALRF);
  new InternalModuleWindow(window, grid);
#endif

#if defined(HARDWARE_EXTERNAL_MODULE) && defined(STM32F4)
  radioHardwareSubtitle(window, STR_EXTERNALRF);
  new ExternalModuleWindow(window, grid);
#endif

#if defined(BLUETOOTH)
  radioHardwareSubtitle(window, STR_BLUETOOTH);
  new BluetoothConfigWindow(window, grid);
#endif

  radioHardwareSubtitle(window, STR_AUX_SERIAL_MODE);
  new SerialConfigWindow(window, grid);

  // Calibration
  new Subtitle(window, STR_INPUTS);
  styleSetupButtonGroup(new SetupButtonGroup(window, {0, 0, LCD_W - padding * 2, 0}, BTN_COLS, calibrationButtons));

  // Debugs
  new Subtitle(window, STR_DEBUG);
  styleSetupButtonGroup(new SetupButtonGroup(window, {0, 0, LCD_W - padding * 2, 0}, FS_BTN_COLS, debugButtons));

  for (uint32_t i = 0; i < lv_obj_get_child_cnt(window->getLvObj()); i += 1) {
    styleRadioHardwareObject(lv_obj_get_child(window->getLvObj(), i));
  }
}
