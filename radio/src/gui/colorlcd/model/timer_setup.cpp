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

#include "timer_setup.h"

#include <memory>
#include <utility>
#include <vector>

#include "button.h"
#include "edgetx.h"
#include "etx_lv_theme.h"
#include "dialog.h"
#include "mainwindow.h"
#include "modal_window.h"
#include "static.h"
#include "textedit.h"
#include "timeedit.h"

#define SET_DIRTY() storageDirty(EE_MODEL)

static const char* kTimerNames[] = {STR_TIMER_1, STR_TIMER_2, STR_TIMER_3};

static const char* swName(int16_t swtch)
{
  if (swtch == SWSRC_NONE) return "---";
  static char s[16];
  getSwitchPositionName(s, swtch);
  return s;
}

// Half of 70%-width popup content area
static constexpr coord_t POPUP_W = LCD_W * 70 / 100;
static constexpr coord_t POPUP_W_SMALL = LCD_W * 34 / 100;
static constexpr coord_t POPUP_HALF_W = POPUP_W / 2;
static constexpr coord_t POPUP_MAX_H = LCD_H * 80 / 100;

// ModalWindow subclass that closes itself on EXIT key
struct PopupWindow : ModalWindow {
  using ModalWindow::ModalWindow;
  void onCancel() override { deleteLater(); }
};

// Create a popup (inherits 50% opacity black bg) that closes on EXIT key
static PopupWindow* createPopup(bool closeOnClickOutside)
{
  return new PopupWindow(closeOnClickOutside);
}

// Create the centered form container with title + scrollable list
static std::pair<Window*, Window*> createPopupForm(PopupWindow* dlg,
                                                   const char* title,
                                                   coord_t width)
{
  auto form = new Window(dlg, rect_t{});
  form->padAll(PAD_ZERO);
  form->setFlexLayout(LV_FLEX_FLOW_COLUMN, PAD_ZERO, width, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(form->getLvObj(), lv_color_make(0x18, 0x18, 0x18),
                            LV_PART_MAIN);
  lv_obj_set_style_bg_opa(form->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_outline_width(form->getLvObj(), 1, LV_PART_MAIN);
  lv_obj_set_style_outline_color(form->getLvObj(), lv_color_make(0x44, 0x44, 0x44),
                                 LV_PART_MAIN);
  lv_obj_set_style_pad_all(form->getLvObj(), 0, LV_PART_MAIN);
  lv_obj_set_style_max_height(form->getLvObj(), POPUP_MAX_H, LV_PART_MAIN);
  lv_obj_center(form->getLvObj());

  // Intercept EXIT key: LV_EVENT_CANCEL bubbles up from focused child to form
  lv_obj_add_event_cb(form->getLvObj(), [](lv_event_t* e) {
    auto* d = (PopupWindow*)lv_event_get_user_data(e);
    d->deleteLater();
  }, LV_EVENT_CANCEL, dlg);

  // Title bar
  auto hdr = new StaticText(form, {0, 0, LV_PCT(100), 0}, title,
                            COLOR_THEME_QM_FG_INDEX);
  lv_obj_set_style_bg_color(hdr->getLvObj(), lv_color_make(0x22, 0x22, 0x22),
                            LV_PART_MAIN);
  lv_obj_set_style_bg_opa(hdr->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
  hdr->padAll(PAD_SMALL);

  // Scrollable item list with visible scrollbar
  auto list = new Window(form, rect_t{});
  list->setFlexLayout(LV_FLEX_FLOW_COLUMN, PAD_TINY, LV_PCT(100),
                      POPUP_MAX_H - EdgeTxStyles::UI_ELEMENT_HEIGHT);
  list->padAll(PAD_MEDIUM);
  lv_obj_set_scroll_dir(list->getLvObj(), LV_DIR_VER);
  lv_obj_set_scrollbar_mode(list->getLvObj(), LV_SCROLLBAR_MODE_ON);
  // Make scrollbar visible against dark background
  lv_obj_set_style_bg_color(list->getLvObj(), lv_color_make(0x88, 0x88, 0x88),
                            LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_opa(list->getLvObj(), LV_OPA_COVER, LV_PART_SCROLLBAR);
  lv_obj_set_style_width(list->getLvObj(), PAD_MEDIUM, LV_PART_SCROLLBAR);
  lv_obj_set_style_pad_all(list->getLvObj(), PAD_TINY, LV_PART_SCROLLBAR);

  return {form, list};
}

// Apply the dark popup button style used in the original timer setup
void applyDarkBtnStyle(lv_obj_t* btn)
{
  lv_obj_set_style_bg_color(btn, lv_color_make(0x28, 0x28, 0x28), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_text_color(btn, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
  lv_obj_set_style_outline_width(btn, 0, LV_PART_MAIN | LV_STATE_FOCUS_KEY);
  lv_obj_set_style_bg_color(btn, lv_color_make(0x00, 0xA0, 0x00), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_text_color(btn, lv_color_black(), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(btn, lv_color_make(0xFF, 0x8C, 0x00), LV_PART_MAIN | LV_STATE_FOCUSED);
  lv_obj_set_style_text_color(btn, lv_color_black(), LV_PART_MAIN | LV_STATE_FOCUSED);
}

// Create a row with label on left and value on right, like ELRS param browser
static lv_obj_t* createSetupRow(Window* parent, const char* label,
                                const char* value,
                                std::function<uint8_t(void)> pressHandler)
{
  auto row = new TextButton(parent, rect_t{}, "", pressHandler);
  lv_obj_t* rowObj = row->getLvObj();
  lv_obj_set_size(rowObj, LV_PCT(100), EdgeTxStyles::UI_ELEMENT_HEIGHT + PAD_SMALL);
  applyDarkBtnStyle(rowObj);

  // Left label
  auto lbl = new StaticText(row, {PAD_MEDIUM, 0, POPUP_HALF_W - PAD_MEDIUM, 0},
                            label, COLOR_THEME_QM_FG_INDEX, LEFT);
  lv_obj_align(lbl->getLvObj(), LV_ALIGN_LEFT_MID, 0, 0);

  // Right value
  lv_obj_t* valLabel = lv_label_create(rowObj);
  etx_font(valLabel, FONT_STD_INDEX);
  lv_obj_set_style_text_color(valLabel, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_text_align(valLabel, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
  lv_obj_set_width(valLabel, POPUP_HALF_W - PAD_LARGE);
  lv_obj_align(valLabel, LV_ALIGN_RIGHT_MID, -PAD_LARGE, 0);
  if (value) lv_label_set_text(valLabel, value);

  return valLabel;
}

// Open a dark-themed text edit dialog for name editing
// Layout identical to LabelDialog, only style changed to match timer popup
static void openNameEditDialog(const char* title, char* nameBuf, uint8_t nameLen,
                               std::function<void()> onSave)
{
  auto dlg = createPopup(false);

  auto form = new Window(dlg, rect_t{});
  form->padAll(PAD_ZERO);
  form->setFlexLayout(LV_FLEX_FLOW_COLUMN, PAD_ZERO, LCD_W * 0.8,
                      LV_SIZE_CONTENT);
  // Dark form background
  lv_obj_set_style_bg_color(form->getLvObj(), lv_color_make(0x18, 0x18, 0x18),
                            LV_PART_MAIN);
  lv_obj_set_style_bg_opa(form->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_center(form->getLvObj());

  // Dark title bar
  auto hdr = new StaticText(form, {0, 0, LV_PCT(100), 0}, title,
                            COLOR_THEME_QM_FG_INDEX);
  lv_obj_set_style_bg_color(hdr->getLvObj(), lv_color_make(0x22, 0x22, 0x22),
                            LV_PART_MAIN);
  lv_obj_set_style_bg_opa(hdr->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
  hdr->padAll(PAD_MEDIUM);

  // Edit box - same layout as LabelDialog
  auto box = new Window(form, rect_t{});
  box->padAll(PAD_MEDIUM);
  box->setFlexLayout(LV_FLEX_FLOW_ROW, 40, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_align(box->getLvObj(), LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_SPACE_BETWEEN);

  new TextEdit(box, rect_t{0, 0, LV_PCT(100), 0}, nameBuf, nameLen);

  // Button box - same layout as LabelDialog
  box = new Window(form, rect_t{});
  box->padAll(PAD_MEDIUM);
  box->setFlexLayout(LV_FLEX_FLOW_ROW, 40, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_align(box->getLvObj(), LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_SPACE_BETWEEN);

  auto cancelBtn = new TextButton(box, rect_t{0, 0, 96, 0}, STR_CANCEL, [=]() {
    dlg->deleteLater();
    return 0;
  });
  applyDarkBtnStyle(cancelBtn->getLvObj());

  auto saveBtn = new TextButton(box, rect_t{0, 0, 96, 0}, STR_SAVE, [=]() {
    onSave();
    dlg->deleteLater();
    return 0;
  });
  applyDarkBtnStyle(saveBtn->getLvObj());
}

// Open a dark-themed popup list for single selection
static void openListPopup(const std::string& title,
                          const std::vector<std::string>& items,
                          std::function<void(int)> onSelect,
                          coord_t width = POPUP_W,
                          bool rightAlign = false)
{
  auto dlg = createPopup(true);
  auto [form, list] = createPopupForm(dlg, title.c_str(), width);

  if (rightAlign) {
    lv_obj_align(form->getLvObj(), LV_ALIGN_RIGHT_MID, -PAD_LARGE, 0);
  }

  for (size_t i = 0; i < items.size(); i++) {
    auto btn = new TextButton(list,
        {0, 0, LV_PCT(100), EdgeTxStyles::UI_ELEMENT_HEIGHT + PAD_SMALL},
        items[i], [=]() -> uint8_t {
          dlg->deleteLater();
          onSelect(i);
          return 0;
        });
    applyDarkBtnStyle(btn->getLvObj());
  }
}

// Open a switch list popup with auto-detect:
// moving a physical switch highlights the matching item, ENT/touch confirms
static void openSwitchListPopup(std::function<void(int)> onSelect)
{
  auto dlg = createPopup(true);

  // Build switch items
  std::vector<std::string> items;
  std::vector<int> srcs;
  items.push_back("---");
  srcs.push_back(SWSRC_NONE);
  for (int i = SWSRC_FIRST_SWITCH; i <= SWSRC_LAST_SWITCH; i++) {
    if (isSwitchAvailable(i, ModelCustomFunctionsContext)) {
      char s[16];
      getSwitchPositionName(s, i);
      items.push_back(s);
      srcs.push_back(i);
    }
  }

  auto [form, list] = createPopupForm(dlg, STR_SWITCH, POPUP_W_SMALL);
  lv_obj_align(form->getLvObj(), LV_ALIGN_RIGHT_MID, -PAD_LARGE, 0);

  // Build buttons and map swtch -> button for auto-detect
  std::vector<lv_obj_t*> buttons;
  std::vector<int> buttonSrcs;

  for (size_t i = 0; i < items.size(); i++) {
    auto btn = new TextButton(list,
        {0, 0, LV_PCT(100), EdgeTxStyles::UI_ELEMENT_HEIGHT + PAD_SMALL},
        items[i], [=]() -> uint8_t {
          dlg->deleteLater();
          onSelect(srcs[i]);
          return 0;
        });
    applyDarkBtnStyle(btn->getLvObj());
    buttons.push_back(btn->getLvObj());
    buttonSrcs.push_back(srcs[i]);
  }

  // Auto-detect: poll getMovedSwitch() and focus the matching button
  // Runs continuously so the user can move multiple switches
  struct SwitchListCtx {
    std::vector<lv_obj_t*> buttons;
    std::vector<int> srcs;
    lv_timer_t* timer;
    swsrc_t lastSwtch = 0;
  };
  auto ctx = new SwitchListCtx{buttons, srcs, nullptr, 0};
  lv_obj_set_user_data(form->getLvObj(), ctx);

  ctx->timer = lv_timer_create(
      [](lv_timer_t* t) {
        auto form = (Window*)t->user_data;
        auto ctx = (SwitchListCtx*)lv_obj_get_user_data(form->getLvObj());
        if (!ctx) return;
        swsrc_t swtch = getMovedSwitch();
        if (swtch == 0 || swtch == ctx->lastSwtch) return;
        ctx->lastSwtch = swtch;
        // Find matching button and focus it
        for (size_t i = 0; i < ctx->srcs.size(); i++) {
          if (ctx->srcs[i] == swtch) {
            lv_group_focus_obj(ctx->buttons[i]);
            // Ensure focused button is visible in scroll area
            lv_obj_scroll_to_view(ctx->buttons[i], LV_ANIM_OFF);
            return;
          }
        }
      },
      100, form);

  // Cleanup on form delete
  lv_obj_add_event_cb(
      form->getLvObj(),
      [](lv_event_t* e) {
        auto ctx = (SwitchListCtx*)lv_obj_get_user_data(e->target);
        if (ctx) {
          if (ctx->timer) lv_timer_del(ctx->timer);
          delete ctx;
        }
      },
      LV_EVENT_DELETE, nullptr);
}

void TimerWindow::open(uint8_t timer)
{
  TimerData* p_timer = &g_model.timers[timer];

  const char* title = (timer < 3) ? kTimerNames[timer] : "Timer";

  auto dlg = createPopup(true);
  auto [form, list] = createPopupForm(dlg, title, POPUP_W);

  // ---- Name ----
  {
    const char* val = p_timer->name[0] ? p_timer->name : "---";
    auto nameValLabel = std::make_shared<lv_obj_t*>(nullptr);
    *nameValLabel = createSetupRow(list, STR_NAME, val, [=]() -> uint8_t {
      openNameEditDialog(STR_NAME, p_timer->name, LEN_TIMER_NAME, [=]() {
        p_timer->name[sizeof(p_timer->name) - 1] = '\0';
        lv_label_set_text(*nameValLabel,
                          p_timer->name[0] ? p_timer->name : "---");
        SET_DIRTY();
      });
      return 0;
    });
  }

  // ---- Mode ----
  {
    auto modeValLabel = std::make_shared<lv_obj_t*>(nullptr);
    *modeValLabel = createSetupRow(
        list, STR_MODE, STR_TIMER_MODES[p_timer->mode], [=]() -> uint8_t {
          std::vector<std::string> items;
          for (int i = 0; i <= TMRMODE_MAX; i++)
            items.push_back(STR_TIMER_MODES[i]);
          openListPopup(STR_MODE, items, [=](int i) {
            p_timer->mode = i;
            lv_label_set_text(*modeValLabel, STR_TIMER_MODES[i]);
            SET_DIRTY();
          }, POPUP_W_SMALL, true);
          return 0;
        });
  }

  // ---- Switch ----
  {
    auto switchValLabel = std::make_shared<lv_obj_t*>(nullptr);
    *switchValLabel = createSetupRow(
        list, STR_SWITCH, swName(p_timer->swtch), [=]() -> uint8_t {
          openSwitchListPopup([=](int swtch) {
            p_timer->swtch = swtch;
            lv_label_set_text(*switchValLabel, swName(p_timer->swtch));
            SET_DIRTY();
          });
          return 0;
        });
  }

  // ---- Start value (inline TimeEdit, no popup) ----
  {
    auto row = new Window(list, rect_t{});
    lv_obj_set_size(row->getLvObj(), LV_PCT(100),
                    EdgeTxStyles::UI_ELEMENT_HEIGHT + PAD_SMALL);
    row->padLeft(PAD_MEDIUM);
    row->padRight(PAD_MEDIUM);

    // Left label
    auto lbl = new StaticText(
        row, {PAD_MEDIUM, 0, POPUP_HALF_W - PAD_MEDIUM, 0}, STR_START,
        COLOR_THEME_QM_FG_INDEX, LEFT);
    lv_obj_align(lbl->getLvObj(), LV_ALIGN_LEFT_MID, 0, 0);

    // Right: TimeEdit for inline editing (ENT + scroll wheel)
    // Max 9:59:59 = 35999s, use TIMEHOUR for HH:MM:SS display
    auto te = new TimeEdit(
        row, {0, 0, LV_SIZE_CONTENT, 0}, 0, 35999,
        [=]() -> int32_t { return p_timer->start; },
        [=](int32_t v) {
          p_timer->start = v;
          timerSet(timer, v);
          SET_DIRTY();
        });
    te->setAccelFactor(16);
    te->setTextFlag(TIMEHOUR);
    te->update();
    lv_obj_set_style_bg_color(te->getLvObj(), lv_color_make(0x28, 0x28, 0x28),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(te->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(te->getLvObj(), lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(te->getLvObj(), lv_color_make(0x00, 0xA0, 0x00),
                              LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(te->getLvObj(), lv_color_black(),
                                LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(te->getLvObj(), lv_color_make(0xFF, 0x8C, 0x00),
                              LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_text_color(te->getLvObj(), lv_color_black(),
                                LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_align(te->getLvObj(), LV_ALIGN_RIGHT_MID, -PAD_LARGE, 0);
  }

  // ---- Minute Beep ----
  {
    auto beepValLabel = std::make_shared<lv_obj_t*>(nullptr);
    *beepValLabel = createSetupRow(
        list, STR_MINUTEBEEP,
        p_timer->minuteBeep ? "ON" : "OFF", [=]() -> uint8_t {
          p_timer->minuteBeep = !p_timer->minuteBeep;
          lv_label_set_text(*beepValLabel,
                            p_timer->minuteBeep ? "ON" : "OFF");
          SET_DIRTY();
          return 0;
        });
  }

  // ---- Countdown ----
  {
    int v = p_timer->countdownBeep;
    if (p_timer->extraHaptic) v += (COUNTDOWN_NON_HAPTIC_LAST + 1);
    auto cdValLabel = std::make_shared<lv_obj_t*>(nullptr);
    *cdValLabel = createSetupRow(
        list, STR_BEEPCOUNTDOWN, STR_VBEEPCOUNTDOWN[v], [=]() -> uint8_t {
          std::vector<std::string> items;
          for (int i = COUNTDOWN_SILENT; i < COUNTDOWN_COUNT; i++)
            items.push_back(STR_VBEEPCOUNTDOWN[i]);
          openListPopup(STR_BEEPCOUNTDOWN, items, [=](int i) {
            int idx = COUNTDOWN_SILENT + i;
            if (idx > COUNTDOWN_NON_HAPTIC_LAST + 1) {
              p_timer->extraHaptic = 1;
              p_timer->countdownBeep =
                  idx - (COUNTDOWN_NON_HAPTIC_LAST + 1);
            } else {
              p_timer->extraHaptic = 0;
              p_timer->countdownBeep = idx;
            }
            lv_label_set_text(*cdValLabel, STR_VBEEPCOUNTDOWN[idx]);
            SET_DIRTY();
          }, POPUP_W_SMALL, true);
          return 0;
        });
  }

  // ---- Countdown Start ----
  {
    std::string cdTitle = std::string(STR_BEEPCOUNTDOWN) + STR_START;
    auto cdStartValLabel = std::make_shared<lv_obj_t*>(nullptr);
    *cdStartValLabel = createSetupRow(
        list, cdTitle.c_str(), STR_COUNTDOWNVALUES[p_timer->countdownStart],
        [=]() -> uint8_t {
          std::vector<std::string> items;
          for (int i = 0; i < 4; i++)
            items.push_back(STR_COUNTDOWNVALUES[i]);
          openListPopup(cdTitle, items, [=](int i) {
            p_timer->countdownStart = i;
            lv_label_set_text(*cdStartValLabel, STR_COUNTDOWNVALUES[i]);
            SET_DIRTY();
          }, POPUP_W_SMALL, true);
          return 0;
        });
  }

  // ---- Persistent ----
  {
    auto persistValLabel = std::make_shared<lv_obj_t*>(nullptr);
    *persistValLabel = createSetupRow(
        list, STR_PERSISTENT, STR_VPERSISTENT[p_timer->persistent],
        [=]() -> uint8_t {
          std::vector<std::string> items;
          for (int i = 0; i < 3; i++)
            items.push_back(STR_VPERSISTENT[i]);
          openListPopup(STR_PERSISTENT, items, [=](int i) {
            p_timer->persistent = i;
            lv_label_set_text(*persistValLabel, STR_VPERSISTENT[i]);
            SET_DIRTY();
          }, POPUP_W_SMALL, true);
          return 0;
        });
  }
}
