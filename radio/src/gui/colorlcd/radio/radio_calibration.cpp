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

#include "radio_calibration.h"

#include "hal/adc_driver.h"
#include "edgetx.h"
#include "pagegroup.h"
#include "sliders.h"
#include "timer_setup.h"
#include "view_main_decoration.h"
#include "hw_inputs.h"

#include <memory>

uint8_t menuCalibrationState;

static const uint8_t stick_pointer[] __FLASH = {
#include "bmp_radio_stick_pointer.lbm"
};
static const uint8_t stick_background[] __FLASH = {
#include "bmp_radio_stick_background.lbm"
};

class StickCalibrationWindow : public Window
{
 public:
  StickCalibrationWindow(Window *parent, const rect_t &rect, uint8_t stickX,
                         uint8_t stickY) :
      Window(parent, rect), stickX(stickX), stickY(stickY)
  {
    new StaticLZ4Image(this, 0, 0, (LZ4Bitmap *)stick_background);
    calibStick = new StaticLZ4Image(this, 0, 0, (LZ4Bitmap *)stick_pointer);
    checkEvents();
  }

  void checkEvents() override
  {
    int32_t x = calibratedAnalogs[stickX];
    int32_t y = calibratedAnalogs[stickY];
    coord_t dx = width() / 2 - CAL_CTR + (CAL_SIZ / 2 * x) / RESX;
    coord_t dy = height() / 2 - CAL_CTR - (CAL_SIZ / 2 * y) / RESX;
    lv_obj_set_pos(calibStick->getLvObj(), dx, dy);
  }

  static LAYOUT_VAL_SCALED(CAL_CTR, 9)
  static LAYOUT_VAL_SCALED(CAL_SIZ, 68)

 protected:
  uint8_t stickX, stickY;
  StaticLZ4Image *calibStick = nullptr;
};

RadioCalibrationPage::RadioCalibrationPage() :
    Page(ICON_RADIO_CALIBRATION)
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
  auto leftIco = new StaticIcon(leftBg, 0, 0, ICON_RADIO_CALIBRATION, COLOR_THEME_PRIMARY2_INDEX);
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

void RadioCalibrationPage::buildHeader(Window *window)
{
  header->setTitle(STR_MENUCALIBRATION);
  title2 = header->setTitle2("");
  etx_font(title2->getLvObj(), FONT_BOLD_INDEX);
}

void RadioCalibrationPage::buildBody(Window *window)
{
  window->padAll(PAD_ZERO);

  menuCalibrationState = CALIB_START;

  // The two sticks

  LZ4Bitmap *bg = (LZ4Bitmap *)stick_background;

  new StickCalibrationWindow(
      window,
      {window->width() / 3 - bg->width / 2,
       window->height() / 2 - bg->height / 2, bg->width, bg->height},
      0, 1);

  auto max_sticks = adcGetMaxInputs(ADC_INPUT_MAIN);
  if (max_sticks > 2) {
    new StickCalibrationWindow(
        window,
        {window->width() * 2 / 3 - bg->width / 2,
         window->height() / 2 - bg->height / 2, bg->width, bg->height},
        3, 2);
  }

  new ViewMainDecoration(window, true);

  axisBtn = new TextButton(window, {AXIS_X, PAD_LARGE, AXIS_W, 0}, STR_STICKS,
                 [=]() -> uint8_t {
                   new HWInputDialog<HWSticks>(STR_STICKS);
                   return 0;
                 });
  applyDarkBtnStyle(axisBtn->getLvObj());

  potsBtn = new TextButton(window, {POTS_X, PAD_LARGE, POTS_W, 0}, STR_POTS,
                 [=]() -> uint8_t {
                   new HWInputDialog<HWPots>(STR_POTS, HWPots::POTS_WINDOW_WIDTH);
                   return 0;
                 });
  applyDarkBtnStyle(potsBtn->getLvObj());

  nxtBtn = new TextButton(window, {NXT_X, PAD_LARGE, NXT_W, 0}, "",
                 [=]() -> uint8_t {
                   nextStep();
                   return 0;
                 });
  applyDarkBtnStyle(nxtBtn->getLvObj());

  setState();
}

void RadioCalibrationPage::setState()
{
  axisBtn->hide();
  potsBtn->hide();

  switch (menuCalibrationState) {
    case CALIB_START:
      title2->setText("");
      nxtBtn->setText(STR_START);
      break;
    case CALIB_SET_MIDPOINT:
      title2->setText(STR_SETMIDPOINT);
      nxtBtn->setText(STR_NEXT);
      break;
    case CALIB_MOVE_STICKS:
      title2->setText(STR_MOVESTICKSPOTS);
      nxtBtn->setText(STR_NEXT);
      axisBtn->show();
      potsBtn->show();
      break;
    case CALIB_STORE:
      title2->setText(STR_CALIB_DONE);
      nxtBtn->setText(STR_EXIT);
      break;
    case CALIB_FINISHED:
      break;
  }
}

void RadioCalibrationPage::checkEvents()
{
  Page::checkEvents();

  if (menuCalibrationState == CALIB_SET_MIDPOINT) {
    adcCalibSetMidPoint();
  } else if (menuCalibrationState == CALIB_MOVE_STICKS) {
    adcCalibSetMinMax();
  }
}

void RadioCalibrationPage::onCancel()
{
  if (menuCalibrationState != CALIB_START &&
      menuCalibrationState != CALIB_STORE) {
    menuCalibrationState = CALIB_START;
    setState();
  } else {
    Page::onCancel();
  }
}

void RadioCalibrationPage::nextStep()
{
  menuCalibrationState++;

  if (menuCalibrationState == CALIB_FINISHED)
    deleteLater();

  if (menuCalibrationState == CALIB_STORE)
    adcCalibStore();

  setState();
}

void startCalibration() { new RadioCalibrationPage(); }
