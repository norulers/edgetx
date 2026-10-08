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

#include "bottom_dock.h"

#include "bitmaps.h"
#include "button.h"
#include "elrs_param_browser.h"
#include "etx_lv_theme.h"
#include "model_setup.h"
#include "quick_menu.h"
#include "static.h"
#include "view_main.h"

// ---------------------------------------------------------------------------
// Dock button LVGL style — dark background, highlighted on press
// ---------------------------------------------------------------------------

lv_obj_t* BottomDock::_focusSentinel = nullptr;

// Inverse-video focus: children (icon/text) flip to dark via USER_1
struct DockFocusData { lv_obj_t* icon; lv_obj_t* text; };

static void dockBtn_focused_cb(lv_event_t* e)
{
  auto* fd = static_cast<DockFocusData*>(lv_event_get_user_data(e));
  lv_obj_add_state(fd->icon, LV_STATE_USER_1);
  lv_obj_add_state(fd->text, LV_STATE_USER_1);
}

static void dockBtn_defocused_cb(lv_event_t* e)
{
  auto* fd = static_cast<DockFocusData*>(lv_event_get_user_data(e));
  lv_obj_clear_state(fd->icon, LV_STATE_USER_1);
  lv_obj_clear_state(fd->text, LV_STATE_USER_1);
}

// RTN short-press → deselect the focused button
static void dockBtn_key_cb(lv_event_t* e)
{
  uint32_t key = lv_event_get_key(e);
  if (key == LV_KEY_ESC) {
    lv_obj_t* s = BottomDock::getFocusSentinel();
    if (s) lv_group_focus_obj(s);
  }
}

static void dockBtn_constructor(const lv_obj_class_t* class_p, lv_obj_t* obj)
{
  etx_solid_bg(obj, COLOR_BLACK_INDEX, LV_PART_MAIN);
  etx_solid_bg(obj, COLOR_DARKGREY_INDEX, LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(obj, 0, LV_PART_MAIN);
  // Focused: light bg (icon/text colors handled via focused_cb + USER_1)
  etx_solid_bg(obj, COLOR_THEME_QM_FG_INDEX, LV_PART_MAIN | LV_STATE_FOCUSED);
}

static const lv_obj_class_t dockBtn_class = {
    .base_class = &lv_btn_class,
    .constructor_cb = dockBtn_constructor,
    .destructor_cb = nullptr,
    .user_data = nullptr,
    .event_cb = nullptr,
    .width_def = LV_SIZE_CONTENT,
    .height_def = BottomDock::DOCK_H,
    .editable = LV_OBJ_CLASS_EDITABLE_INHERIT,
    .group_def = LV_OBJ_CLASS_GROUP_DEF_TRUE,
    .instance_size = sizeof(lv_btn_t),
};

static lv_obj_t* dockBtn_create(lv_obj_t* parent)
{
  return etx_create(&dockBtn_class, parent);
}

// ---------------------------------------------------------------------------
// Dock button definition table
// icon, label string, QMPage to open
// ---------------------------------------------------------------------------

struct DockBtnDef {
  EdgeTxIcon icon;
  const char* label;
  QMPage page;
};

static constexpr int NUM_DOCK_BUTTONS = 4;

static const DockBtnDef dockBtns[NUM_DOCK_BUTTONS] = {
    {ICON_RADIO,         "ELRS",   QM_RADIO_HARDWARE},
    {ICON_MODEL_SETUP,   "MODEL",  QM_MODEL_SETUP   },
    {ICON_RADIO_SETUP,   "SYSTEM", QM_RADIO_SETUP   },
    {ICON_TOOLS_APPS,    "TOOLS",  QM_TOOLS_APPS    },
};

// Icon pixel height inside the dock (leave room for label below)
static LAYOUT_VAL_SCALED(DOCK_ICON_SIZE, 26)
static LAYOUT_VAL_SCALED(DOCK_LABEL_H,  14)
static LAYOUT_VAL_SCALED(DOCK_ICON_Y,    4)
static LAYOUT_VAL_SCALED(DOCK_SEP_H,     2)

// ---------------------------------------------------------------------------

void BottomDock::addDockButton(coord_t x, coord_t btnW, EdgeTxIcon icon,
                               const char* label,
                               std::function<uint8_t()> action)
{
  auto btn = new ButtonBase(this, {x, 0, btnW, DOCK_H}, std::move(action),
                            dockBtn_create);

  // Icon — centred horizontally, white normally; dark when focused (USER_1)
  coord_t iconX = (btnW - DOCK_ICON_SIZE) / 2;
  auto iconPtr = new StaticIcon(btn, iconX, DOCK_ICON_Y, icon, COLOR_WHITE_INDEX);
  etx_img_color(iconPtr->getLvObj(), COLOR_THEME_QM_BG_INDEX, LV_STATE_USER_1);

  // Label — light grey normally; dark when focused (USER_1)
  coord_t labelY = DOCK_H - DOCK_LABEL_H - PAD_SMALL;
  auto textPtr = new StaticText(btn, {0, labelY, btnW, DOCK_LABEL_H}, label,
                 COLOR_LIGHTGREY_INDEX, CENTERED | FONT(XS));
  etx_txt_color(textPtr->getLvObj(), COLOR_THEME_QM_BG_INDEX, LV_STATE_USER_1);

  // Register focused/defocused/cancel callbacks for inverse-video effect
  auto* fd = new DockFocusData{iconPtr->getLvObj(), textPtr->getLvObj()};
  lv_obj_add_event_cb(btn->getLvObj(), dockBtn_focused_cb,   LV_EVENT_FOCUSED,   fd);
  lv_obj_add_event_cb(btn->getLvObj(), dockBtn_defocused_cb, LV_EVENT_DEFOCUSED, fd);
  lv_obj_add_event_cb(btn->getLvObj(), dockBtn_key_cb,       LV_EVENT_KEY,       nullptr);

  // Vertical separator after each button (except the last)
  if (x + btnW < LCD_W) {
    auto sep = lv_obj_create(lvobj);
    lv_obj_set_pos(sep, x + btnW - 1, DOCK_SEP_H);
    lv_obj_set_size(sep, 1, DOCK_H - DOCK_SEP_H * 2);
    etx_solid_bg(sep, COLOR_DARKGREY_INDEX);  // subtle dark separator
    lv_obj_clear_flag(sep, LV_OBJ_FLAG_CLICKABLE);
  }
}

// ---------------------------------------------------------------------------

BottomDock::BottomDock(Window* parent) :
    Window(parent, {0, (coord_t)(LCD_H - DOCK_H), LCD_W, DOCK_H})
{
  setWindowFlag(NO_FOCUS);
  etx_solid_bg(lvobj, COLOR_BLACK_INDEX);  // pure black to match FPV layout bg

  // Focus sentinel: invisible 0×0 object added first to the encoder group.
  // Focusing it removes any visible selection highlight (RTN = deselect).
  _focusSentinel = lv_obj_create(lvobj);
  lv_obj_set_size(_focusSentinel, 0, 0);
  lv_obj_set_pos(_focusSentinel, 0, 0);
  lv_obj_clear_flag(_focusSentinel, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_opa(_focusSentinel, LV_OPA_TRANSP, LV_PART_MAIN);
  // Set user_data to ViewMain (parent) so that when sentinel is focused,
  // non-LVGL key events (PGUP/PGDN/SYS/MDL/TELE) are dispatched to ViewMain
  // instead of being silently dropped (keyboardDriverRead checks user_data).
  lv_obj_set_user_data(_focusSentinel, parent);
  lv_group_t* sentinelGrp = lv_group_get_default();
  if (sentinelGrp) lv_group_add_obj(sentinelGrp, _focusSentinel);

  // Top accent line — cyan, matches the LAP arc accent colour
  auto topLine = lv_obj_create(lvobj);
  lv_obj_set_pos(topLine, 0, 0);
  lv_obj_set_size(topLine, LCD_W, DOCK_SEP_H);
  etx_solid_bg(topLine, COLOR_CYAN_INDEX);
  lv_obj_clear_flag(topLine, LV_OBJ_FLAG_CLICKABLE);

  coord_t btnW = LCD_W / NUM_DOCK_BUTTONS;

  for (int i = 0; i < NUM_DOCK_BUTTONS; i++) {
    if (i == 0) {
      // ELRS — open native parameter browser
      addDockButton(i * btnW, btnW, dockBtns[i].icon, dockBtns[i].label,
                    []() -> uint8_t {
                      new ElrsParamBrowser(ViewMain::instance());
                      return 0;
                    });
    } else {
      auto page = dockBtns[i].page;
      addDockButton(i * btnW, btnW, dockBtns[i].icon, dockBtns[i].label,
                    [=]() -> uint8_t {
                      if (page == QM_MODEL_SETUP)
                        new ModelMenuPage();
                      else if (page == QM_RADIO_SETUP)
                        new RadioMenuPage();
                      else if (page == QM_TOOLS_APPS)
                        new ToolsMenuPage();
                      else
                        QuickMenu::openPage(page);
                      return 0;
                    });
    }
  }

  // All buttons and their callbacks are now fully set up.
  // Explicitly restore focus to the sentinel so no button shows as selected on boot.
  if (_focusSentinel) lv_group_focus_obj(_focusSentinel);
}
