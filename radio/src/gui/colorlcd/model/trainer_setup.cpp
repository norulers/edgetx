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

#include "trainer_setup.h"

#include "button.h"
#include "channel_range.h"
#include "choice.h"
#include "edgetx.h"
#include "etx_lv_theme.h"
#include "form.h"
#include "getset_helpers.h"
#include "menu.h"
#include "numberedit.h"
#include "ppm_settings.h"
#include "static.h"
#include "textedit.h"
#include "timer_setup.h"

#if defined(BLUETOOTH)
#include "trainer_bluetooth.h"
#endif

#define SET_DIRTY()     storageDirty(EE_MODEL)

static const lv_coord_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(2),
                                     LV_GRID_TEMPLATE_LAST};
static const lv_coord_t row_dsc[] = {LV_GRID_CONTENT,
                                     LV_GRID_TEMPLATE_LAST};

class TrainerModuleWindow : public Window
{
 public:
  TrainerModuleWindow(Window* parent);

  void checkEvents() override;
  void update();

 protected:
  ChannelRange* chRange = nullptr;

#if defined(BLUETOOTH)
  // StaticText *btChannelEnd = nullptr;
  // StaticText *btDistAddress = nullptr;
  // TextButton *btMasterButton = nullptr;
  // Menu *btPopUpMenu = nullptr;
  // bool btCanceled = false;

 private:
  // bool popupopen = false;
  // int devicecount = 0;
  // uint8_t lastbluetoothstate = BLUETOOTH_STATE_OFF;

  // void btDiscoverMenuItemChosen();
  // void btDiscoverMenuAddItem(const char *itm);

#endif
};

TrainerModuleWindow::TrainerModuleWindow(Window* parent) :
    Window(parent, rect_t{})
{
  setFlexLayout();
  update();
}

void TrainerModuleWindow::checkEvents()
{
// #if defined(BLUETOOTH)
//   if (popupopen) {
//     if (bluetooth.state == BLUETOOTH_STATE_DISCOVER_START ||
//         bluetooth.state == BLUETOOTH_STATE_DISCOVER_END) {
//       int cnt = min<uint8_t>(reusableBuffer.moduleSetup.bt.devicesCount,
//                              MAX_BLUETOOTH_DISTANT_ADDR);
//       if (devicecount < cnt) {
//         for (int i = 0; i < cnt - devicecount; i++) {
//           int index = devicecount + i;
//           btDiscoverMenuAddItem(reusableBuffer.moduleSetup.bt.devices[index]);
//         }
//         devicecount = cnt;
//       }
//     }
//   }
//   if (bluetooth.state != lastbluetoothstate) {
//     // TODO:
//     // if (!popupopen && !trChoiceOpen) update();
//     lastbluetoothstate = bluetooth.state;
//   }
// #endif
  Window::checkEvents();
}

// Recursively force white labels + darken row backgrounds for Dark FPV theme
static void darkenTrainerRows(lv_obj_t* obj)
{
  uint32_t cnt = lv_obj_get_child_cnt(obj);
  for (uint32_t i = 0; i < cnt; i++) {
    lv_obj_t* child = lv_obj_get_child(obj, i);
    if (lv_obj_check_type(child, &lv_label_class)) {
      lv_obj_set_style_text_color(child, lv_color_white(), LV_PART_MAIN);
    }
    if (!lv_obj_check_type(child, &lv_label_class) && lv_obj_get_child_cnt(child) > 0) {
      lv_obj_set_style_bg_color(child, lv_color_make(0x28, 0x28, 0x28), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(child, LV_OPA_COVER, LV_PART_MAIN);
    }
    if (lv_obj_check_type(child, &lv_textarea_class)) {
      applyDarkBtnStyle(child);
    }
    darkenTrainerRows(child);
  }
}

void TrainerModuleWindow::update()
{
  FlexGridLayout grid(col_dsc, row_dsc, PAD_TINY);
  clear();

  auto td = &g_model.trainerData;
  if (td->mode == TRAINER_MODE_OFF) return;

#if defined(BLUETOOTH)
  if (td->mode == TRAINER_MODE_MASTER_BLUETOOTH ||
      td->mode == TRAINER_MODE_SLAVE_BLUETOOTH) {

    auto bt = new BluetoothTrainerWindow(this);
    if (td->mode == TRAINER_MODE_SLAVE_BLUETOOTH)
      bt->setMaster(false);

    bt->refresh();
    // TODO: slave: channel range
  }
#endif

  if (td->mode == TRAINER_MODE_SLAVE) {

    // Channel range
    auto line = newLine(grid);
    new StaticText(line, rect_t{}, STR_CHANNELRANGE);
    chRange = new TrainerChannelRange(line);

    // PPM frame
    line = newLine(grid);
    new StaticText(line, rect_t{}, STR_PPMFRAME);
    auto obj = new PpmFrameSettings<TrainerModuleData>(line, td);

    // copy pointer to frame len edit object to channel range
    chRange->setPpmFrameLenEditObject(obj->getPpmFrameLenEditObject());
  }

  // Dark FPV: force labels white, darken rows
  darkenTrainerRows(lvobj);
}

TrainerPage::TrainerPage() : Page(ICON_MODEL_SETUP)
{
  header->setTitle(STR_MAIN_MODEL_SETTINGS);
  header->setTitle2(STR_TRAINER);

  // Dark FPV header
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(header->getLvObj()); i++) {
    auto child = lv_obj_get_child(header->getLvObj(), i);
    if (lv_obj_check_type(child, &lv_canvas_class))
      lv_obj_add_flag(child, LV_OBJ_FLAG_HIDDEN);
  }
  auto hdrLeftBg = new StaticIcon(header, 0, 0, ICON_TOPLEFT_BG, COLOR_THEME_PRIMARY2_INDEX);
  lv_obj_set_style_img_recolor_opa(hdrLeftBg->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_img_recolor(hdrLeftBg->getLvObj(), lv_color_make(0x22, 0x22, 0x22), LV_PART_MAIN);
  hdrLeftBg->setTop((EdgeTxStyles::MENU_HEADER_HEIGHT - hdrLeftBg->height()) / 2);
  auto hdrLeftIco = new StaticIcon(hdrLeftBg, 0, 0, ICON_MODEL_SETUP, COLOR_THEME_PRIMARY2_INDEX);
  lv_obj_set_style_img_recolor_opa(hdrLeftIco->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_img_recolor(hdrLeftIco->getLvObj(), lv_color_make(0xFF, 0x8C, 0x00), LV_PART_MAIN);
  hdrLeftIco->center(hdrLeftBg->width() + PAD_MEDIUM, hdrLeftBg->height());
  auto hdrRightBg = new StaticIcon(header, LCD_W, 0, ICON_TOPRIGHT_BG, COLOR_THEME_PRIMARY2_INDEX);
  lv_obj_set_style_img_recolor_opa(hdrRightBg->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_img_recolor(hdrRightBg->getLvObj(), lv_color_make(0x22, 0x22, 0x22), LV_PART_MAIN);
  hdrRightBg->setPos(LCD_W - hdrRightBg->width(), (EdgeTxStyles::MENU_HEADER_HEIGHT - hdrRightBg->height()) / 2);
  auto hdrRightIco = new StaticIcon(hdrRightBg, 0, 0, ICON_BTN_CLOSE, COLOR_THEME_PRIMARY2_INDEX);
  lv_obj_set_style_img_recolor_opa(hdrRightIco->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_img_recolor(hdrRightIco->getLvObj(), lv_color_make(0xFF, 0x8C, 0x00), LV_PART_MAIN);
  hdrRightIco->center(hdrRightBg->width() + PAD_MEDIUM, hdrRightBg->height());

  body->setFlexLayout();

  FlexGridLayout grid(col_dsc, row_dsc, PAD_TINY);

  // Mode row with dark card
  auto line = body->newLine(grid);
  lv_obj_set_style_bg_color(line->getLvObj(), lv_color_make(0x28, 0x28, 0x28), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(line->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
  line->padAll(PAD_SMALL);
  auto modeLbl = new StaticText(line, rect_t{}, STR_MODE);
  lv_obj_set_style_text_color(modeLbl->getLvObj(), lv_color_white(), LV_PART_MAIN);

  auto trainerChoice =
      new Choice(line, rect_t{}, STR_VTRAINERMODES, 0, TRAINER_MODE_MAX(),
                 GET_SET_DEFAULT(g_model.trainerData.mode));
  applyDarkBtnStyle(trainerChoice->getLvObj());
  trainerChoice->setAvailableHandler(isTrainerModeAvailable);

  auto trainerModule = new TrainerModuleWindow(body);

  TrainerModuleData* tr = &g_model.trainerData;
  trainerChoice->setSetValueHandler([=](int32_t newValue) {
    //TODO: move the BT stuff somewhere else?
#if defined(BLUETOOTH)
    memclear(bluetooth.distantAddr, sizeof(bluetooth.distantAddr));
    bluetooth.state = BLUETOOTH_STATE_OFF;
#endif
    tr->mode = newValue;
    trainerModule->update();
    SET_DIRTY();
  });
}
