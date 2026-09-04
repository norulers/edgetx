/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "layout_fpv_dash.h"

#include "bitmaps.h"
#include "edgetx.h"
#include "etx_lv_theme.h"
#include "static.h"
#include "strhelpers.h"
#include "translations/translations.h"
#include "view_main.h"
#include "model_select.h"
#include "dialog.h"
#include "filechoice.h"
#include "libui/menu.h"
#include "mainwindow.h"
#include "model/timer_setup.h"
#include "modal_window.h"
#include "choice.h"
#include "switchchoice.h"
#include "textedit.h"
#include "timeedit.h"
#include "toggleswitch.h"
#include "storage/modelslist.h"
#include "model/model_select.h"
#include "telemetry/crossfire.h"
#include "pulses/modules_helpers.h"
#include "mixer_scheduler.h"
#include "radio/elrs_param_browser.h"

// --- Geometry constants (480x272) -------------------------------------------
#include "bottom_dock.h"

static lv_obj_t* s_modelNameObj = nullptr;  // for async USER_1 clear

// ── BitmapPreviewMenu ────────────────────────────────────────────────────────
// Thumbnail picker built on top of the standard `Menu` (proven to work as the
// base class for `FileChoice::openMenu`).  We keep the menu's own UI exactly
// as-is — only ADD a `StaticBitmap` preview pane (and filename label) on the
// right side of the screen as siblings on the menu's full-screen modal
// background.  The current selection is polled in `checkEvents()`; preview
// reloads are debounced 150 ms so encoder scrolling doesn't issue an SD read
// per tick.
//
// Layout (480 × 272):
//   Menu content centred (~180 wide → x ∈ [150, 330]).
//   Preview pane at x = 336, y = 60, 138 × 88.
//   Filename label at x = 336, y = 154, 138 × 18.

class BitmapPreviewMenu : public Menu
{
 public:
  BitmapPreviewMenu(std::vector<std::string> files,
                    std::string folderPath,
                    int currentIdx,
                    std::function<void(int)> onSelectCb)
      : Menu(false, 180),
        fileList(std::move(files)),
        folderPath(std::move(folderPath)),
        onSelectCb(std::move(onSelectCb))
  {
    setTitle(STR_BITMAP);

    // Allow EXIT key to close the picker — pressing ESC triggers onCancel().
    lv_obj_add_event_cb(lvobj, [](lv_event_t* e) {
      auto code = lv_indev_get_key(lv_indev_get_act());
      if (code == LV_KEY_ESC) {
        auto* self = static_cast<BitmapPreviewMenu*>(lv_event_get_user_data(e));
        if (self && !self->deleted()) self->onCancel();
      }
    }, LV_EVENT_KEY, this);

    // Add lines (buffered → single layout pass via updateLines()).
    for (size_t i = 0; i < fileList.size(); i++) {
      int idx = (int)i;
      const std::string& name = fileList[i];
      std::string text = name.empty() ? "---" : name;
      addLineBuffered(text, [this, idx]() {
        if (this->onSelectCb) this->onSelectCb(idx);
      });
    }
    updateLines();

    // Start at the user's current selection.
    if (currentIdx >= 0 && currentIdx < (int)fileList.size())
      select(currentIdx);

    // Build preview pane (children of the menu's full-screen modal lvobj).
    buildPreviewUI();

    // Seed lastSelection so the very first checkEvents() tick triggers a load.
    lastSelection = -2;
  }

  ~BitmapPreviewMenu() override
  {
    if (previewTimer) {
      lv_timer_del(previewTimer);
      previewTimer = nullptr;
    }
  }

  // Poll selection() each tick and debounce-load the thumbnail.
  void checkEvents() override
  {
    Menu::checkEvents();
    int sel = selection();
    if (sel != lastSelection) {
      lastSelection = sel;
      schedulePreview(sel);
    }
  }

 private:
  std::vector<std::string> fileList;
  std::string              folderPath;
  std::function<void(int)> onSelectCb;
  StaticBitmap*            previewBitmap = nullptr;
  lv_timer_t*              previewTimer  = nullptr;
  int                      lastSelection = -2;
  int                      pendingIdx    = -1;

  static void previewTimerCb(lv_timer_t* t)
  {
    auto* self = static_cast<BitmapPreviewMenu*>(t->user_data);
    if (self->deleted()) return;
    self->previewTimer = nullptr;  // one-shot self-deletes
    self->loadPreview(self->pendingIdx);
  }

  void schedulePreview(int idx)
  {
    pendingIdx = idx;
    if (previewTimer) {
      lv_timer_reset(previewTimer);
    } else {
      previewTimer = lv_timer_create(previewTimerCb, 150, this);
      lv_timer_set_repeat_count(previewTimer, 1);
    }
  }

  void loadPreview(int idx)
  {
    if (!previewBitmap) return;
    if (idx < 0 || idx >= (int)fileList.size() || fileList[idx].empty()) {
      previewBitmap->clearSource();
      return;
    }
    std::string path = folderPath + PATH_SEPARATOR + fileList[idx];
    watchdogSuspend(200 /*2s*/);  // image decode contends for FatFS mutex
    previewBitmap->setSource(path.c_str());
  }

  void buildPreviewUI()
  {
    constexpr coord_t PREV_W = 140;
    constexpr coord_t DIV_W  = 1;   // 1-px vertical separator

    // The Menu content box is always the first child of the modal lvobj.
    lv_obj_t* content_lv = lv_obj_get_child(lvobj, 0);
    if (!content_lv) return;
    lv_obj_update_layout(content_lv);

    coord_t cw = lv_obj_get_width(content_lv);
    coord_t ch = lv_obj_get_height(content_lv);
    if (ch <= 0) ch = 140;

    // ── ELRS-green theme: match elrs_param_browser style ─────────────────
    // Content box background → #181818 (ELRS panel bg)
    lv_obj_set_style_bg_color(content_lv,
        lv_color_make(0x18, 0x18, 0x18),
        LV_PART_MAIN);
    lv_obj_set_style_bg_opa(content_lv, LV_OPA_COVER, LV_PART_MAIN);
    // Header: ELRS green bg + white text
    lv_obj_t* header_lv = lv_obj_get_child(content_lv, 0);
    if (header_lv) {
      lv_obj_set_style_bg_color(header_lv,
          lv_color_make(0x00, 0xA0, 0x00),
          LV_PART_MAIN);
      lv_obj_set_style_bg_opa(header_lv, LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_set_style_text_color(header_lv,
          lv_color_white(),
          LV_PART_MAIN);
    }
    // Table rows: dark bg + white text; selected row ELRS green bg + white text
    lv_obj_t* body_lv = lv_obj_get_child(content_lv, 1);
    if (body_lv) {
      lv_obj_set_style_bg_color(body_lv,
          lv_color_make(0x18, 0x18, 0x18),
          LV_PART_ITEMS);
      lv_obj_set_style_text_color(body_lv,
          lv_color_white(),
          LV_PART_ITEMS);
      lv_obj_set_style_bg_color(body_lv,
          lv_color_make(0x00, 0xA0, 0x00),
          LV_PART_ITEMS | LV_STATE_EDITED);
      lv_obj_set_style_text_color(body_lv,
          lv_color_white(),
          LV_PART_ITEMS | LV_STATE_EDITED);
    }
    // ────────────────────────────────────────────────────────────────────────

    // ── Step 1: remove the per-box outline from the menu content so we can
    //           add one unified outline around the combined panel. ──────────
    lv_obj_remove_style(content_lv, (lv_style_t*)&(styles->outline),
                        LV_PART_MAIN);
    lv_obj_remove_style(content_lv, (lv_style_t*)&(styles->outline_color_normal),
                        LV_PART_MAIN);

    // Shift content left so [content | sep | preview] is centred as a block.
    const coord_t total_w = cw + DIV_W + PREV_W;
    lv_obj_align(content_lv, LV_ALIGN_CENTER, -(DIV_W + PREV_W) / 2, 0);

    // ── Step 2: unified outer frame (BELOW everything in Z-order). ──────────
    // Transparent fill + single outline around the combined block.
    lv_obj_t* frame = lv_obj_create(lvobj);
    lv_obj_set_size(frame, total_w, ch);
    lv_obj_align(frame, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(frame, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(frame, 0, LV_PART_MAIN);
    lv_obj_clear_flag(frame, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    etx_obj_add_style(frame, styles->outline, LV_PART_MAIN);
    etx_obj_add_style(frame, styles->outline_color_normal, LV_PART_MAIN);
    // Push behind content_lv (index 0) so clicks reach content & preview.
    lv_obj_move_to_index(frame, 0);

    // ── Step 3: 1-px separator (ELRS border grey). ────────────────────
    lv_obj_t* sep = lv_obj_create(lvobj);
    lv_obj_remove_style_all(sep);
    lv_obj_set_size(sep, DIV_W, ch);
    lv_obj_align(sep, LV_ALIGN_CENTER, (cw - PREV_W) / 2, 0);
    lv_obj_set_style_bg_color(sep,
        lv_color_make(0x44, 0x44, 0x44),
        LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(sep, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // ── Step 4: preview bitmap (ELRS panel bg). ───────────────────────
    previewBitmap = new StaticBitmap(this, {0, 0, PREV_W, ch}, nullptr);
    lv_obj_t* pb = previewBitmap->getLvObj();
    lv_obj_set_style_bg_color(pb,
        lv_color_make(0x18, 0x18, 0x18),
        LV_PART_MAIN);
    lv_obj_set_style_bg_opa(pb, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_align(pb, LV_ALIGN_CENTER, (cw + DIV_W) / 2, 0);
  }
};

// FileChoice subclass — opens the thumbnail-aware menu above instead of the
// plain `Menu` that the base class would create.
class BitmapPicker : public FileChoice
{
 public:
  using FileChoice::FileChoice;

 protected:
  void openMenu() override
  {
    watchdogSuspend(200 /*2s*/);
    loadFiles();
    if (fileCount == 0) {
      auto* dlg = new MessageDialog(STR_SDCARD, STR_NO_FILES_ON_SD);
      applyFpvDarkDialogStyle(dlg);
      return;
    }
    setEditMode(true);

    std::vector<std::string> files;
    files.reserve(getMax() + 1);
    for (int i = 0; i <= getMax(); i++) files.push_back(getString(i));

    auto* menu = new BitmapPreviewMenu(
        std::move(files), folder, selectedIdx,
        [this](int idx) {
          if (idx >= 0 && idx <= getMax()) setValue(idx);
        });
    menu->setCloseHandler([this]() { setEditMode(false); });
  }

 private:
  // Apply FPV dark-tech styling to a standard MessageDialog so it matches
  // the dashboard aesthetic (#181818 bg, ELRS green header, white text).
  static void applyFpvDarkDialogStyle(MessageDialog* dlg)
  {
    lv_obj_t* overlay = dlg->getLvObj();
    // BaseDialog structure: overlay → content(Win) → [header, form]
    lv_obj_t* content = lv_obj_get_child(overlay, 0);
    if (!content) return;

    // Content panel → #181818
    lv_obj_set_style_bg_color(content,
        lv_color_make(0x18, 0x18, 0x18), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(content, LV_OPA_COVER, LV_PART_MAIN);

    uint32_t cc = lv_obj_get_child_cnt(content);
    for (uint32_t i = 0; i < cc; i++) {
      lv_obj_t* child = lv_obj_get_child(content, i);
      if (i == 0) {
        // Header → ELRS green bg + white text
        lv_obj_set_style_bg_color(child,
            lv_color_make(0x00, 0xA0, 0x00), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(child, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_text_color(child, lv_color_white(), LV_PART_MAIN);
      } else {
        // Form bg → #181818
        lv_obj_set_style_bg_color(child,
            lv_color_make(0x18, 0x18, 0x18), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(child, LV_OPA_COVER, LV_PART_MAIN);
        // Message text widgets → white
        uint32_t fc = lv_obj_get_child_cnt(child);
        for (uint32_t j = 0; j < fc; j++) {
          lv_obj_t* txt = lv_obj_get_child(child, j);
          lv_obj_set_style_text_color(txt, lv_color_white(), LV_PART_MAIN);
        }
      }
    }
  }
};

// Focus highlight via USER_1 (not LV_STATE_FOCUSED) — immune to LVGL focus restoration.
static void fpvModelName_focused_cb(lv_event_t* e)
{
  lv_obj_add_state(lv_event_get_target(e), LV_STATE_USER_1);
}

static void fpvModelName_defocused_cb(lv_event_t* e)
{
  lv_obj_clear_state(lv_event_get_target(e), LV_STATE_USER_1);
}

static void fpvSentinelFocusAsync(void*)
{
  if (s_modelNameObj) lv_obj_clear_state(s_modelNameObj, LV_STATE_USER_1);
  lv_obj_t* s = BottomDock::getFocusSentinel();
  if (s) lv_group_focus_obj(s);
}

static void fpvModelLabelsWindow_deleted_cb(lv_event_t*)
{
  lv_async_call(fpvSentinelFocusAsync, nullptr);
}

static void fpvModelName_clicked_cb(lv_event_t* e)
{
  lv_obj_clear_state(lv_event_get_target(e), LV_STATE_PRESSED);
  if (s_modelNameObj) lv_obj_clear_state(s_modelNameObj, LV_STATE_USER_1);
  lv_obj_t* s = BottomDock::getFocusSentinel();
  if (s) lv_group_focus_obj(s);
  auto* mlw = new ModelLabelsWindow();
  lv_obj_add_event_cb(mlw->getLvObj(), fpvModelLabelsWindow_deleted_cb,
                      LV_EVENT_DELETE, nullptr);
}

static void fpvModelName_cancel_cb(lv_event_t* e)
{
  lv_obj_clear_state(lv_event_get_target(e), LV_STATE_USER_1);
  lv_obj_t* s = BottomDock::getFocusSentinel();
  if (s) lv_group_focus_obj(s);
}

static void fpvModelBitmap_focused_cb(lv_event_t* e) {
  lv_obj_add_state(lv_event_get_target(e), LV_STATE_USER_1);
}
static void fpvModelBitmap_defocused_cb(lv_event_t* e) {
  lv_obj_clear_state(lv_event_get_target(e), LV_STATE_USER_1);
}
static void fpvModelBitmap_cancel_cb(lv_event_t* e) {
  lv_obj_clear_state(lv_event_get_target(e), LV_STATE_USER_1);
  lv_obj_t* s = BottomDock::getFocusSentinel();
  if (s) lv_group_focus_obj(s);
}

// Tapping or pressing ENTER on the model bitmap opens the bitmap file chooser.
static void fpvModelBitmap_clicked_cb(lv_event_t* e)
{
  auto* picker = static_cast<FileChoice*>(lv_event_get_user_data(e));
  if (picker) picker->onClicked();
}

// Timer label: encoder focus/defocus/cancel helpers.
static void fpvTimerLabel_focused_cb(lv_event_t* e)
{
  lv_obj_add_state(lv_event_get_target(e), LV_STATE_USER_1);
}
static void fpvTimerLabel_defocused_cb(lv_event_t* e)
{
  lv_obj_clear_state(lv_event_get_target(e), LV_STATE_USER_1);
}
static void fpvTimerLabel_cancel_cb(lv_event_t* e)
{
  lv_obj_clear_state(lv_event_get_target(e), LV_STATE_USER_1);
  lv_obj_t* s = BottomDock::getFocusSentinel();
  if (s) lv_group_focus_obj(s);
}

// Prevent LV_EVENT_CLICKED from firing after a long press release on touch.
static bool timerLongPressed = false;

// Short press: open timer settings dialog
static void fpvTimer1_clicked_cb(lv_event_t* e)
{
  if (timerLongPressed) {
    timerLongPressed = false;
    return;
  }
  lv_obj_clear_state(lv_event_get_target(e), LV_STATE_PRESSED | LV_STATE_USER_1);
  lv_obj_t* s = BottomDock::getFocusSentinel();
  if (s) lv_group_focus_obj(s);
  TimerWindow::open(0);
}
static void fpvTimer2_clicked_cb(lv_event_t* e)
{
  if (timerLongPressed) {
    timerLongPressed = false;
    return;
  }
  lv_obj_clear_state(lv_event_get_target(e), LV_STATE_PRESSED | LV_STATE_USER_1);
  lv_obj_t* s = BottomDock::getFocusSentinel();
  if (s) lv_group_focus_obj(s);
  TimerWindow::open(1);
}

// Long press: reset timer
static void fpvTimer1_long_pressed_cb(lv_event_t* e)
{
  timerLongPressed = true;
  lv_obj_clear_state(lv_event_get_target(e), LV_STATE_PRESSED | LV_STATE_USER_1);
  lv_obj_t* s = BottomDock::getFocusSentinel();
  if (s) lv_group_focus_obj(s);
  timerReset(0);
}
static void fpvTimer2_long_pressed_cb(lv_event_t* e)
{
  timerLongPressed = true;
  lv_obj_clear_state(lv_event_get_target(e), LV_STATE_PRESSED | LV_STATE_USER_1);
  lv_obj_t* s = BottomDock::getFocusSentinel();
  if (s) lv_group_focus_obj(s);
  timerReset(1);
}

// Right-top: battery bar (horizontal, above timer, where LAP was)
static LAYOUT_VAL_SCALED(BAT_X, 269)
static LAYOUT_VAL_SCALED(BAT_Y,  10)
static LAYOUT_VAL_SCALED(BAT_W, 180)  // right edge = 460, avoids trim bar (17px at x=463)
static LAYOUT_VAL_SCALED(BAT_H,  26)

// Centre: model image
static LAYOUT_VAL_SCALED(IMG_X,  54)
static LAYOUT_VAL_SCALED(IMG_Y,  20)
static LAYOUT_VAL_SCALED(IMG_W, 192)
static LAYOUT_VAL_SCALED(IMG_H, 114)

// Centre-bottom: arm state + model name
static LAYOUT_VAL_SCALED(ARM_Y, 155)
static LAYOUT_VAL_SCALED(ARM_H,  44)
static LAYOUT_VAL_SCALED(MNAME_Y, 200)
static LAYOUT_VAL_SCALED(MNAME_H,  20)

// Right: timer 2 (top) + timer 1 (below)
static LAYOUT_VAL_SCALED(TMR_X,   269)
static LAYOUT_VAL_SCALED(TMR2_Y,   44)  // timer 1 — just below battery bar
static LAYOUT_VAL_SCALED(TMR2_H,   60)  // FONT_XL height
static LAYOUT_VAL_SCALED(TMR_Y,    98)  // timer 2 — below timer 1
static LAYOUT_VAL_SCALED(TMR_W,   200)  // 8 chars × ~24px + margin, fixed-width format
static LAYOUT_VAL_SCALED(TMR_H,    60)

// Format timer ticks (100ms units) -> "MM:SS.cc"  (fixed 8 chars, no horizontal jumping)
static std::string formatTimer(uint32_t val)
{
  uint32_t hundredths = val % 100;
  uint32_t secs  = (val / 100) % 60;
  uint32_t mins  = (val / 6000);
  char buf[16];
  snprintf(buf, sizeof(buf), "%u:%02u.%02u", (unsigned)mins, (unsigned)secs, (unsigned)hundredths);
  return buf;
}

FpvDashLayout::FpvDashLayout(Window* parent, const LayoutFactory* factory,
                             int screenNum, uint8_t zoneCount, uint8_t* zoneMap)
    : Layout(parent, factory, screenNum, zoneCount, zoneMap)
{
  // Main area background — pure black
  lv_obj_set_style_bg_color(lvobj,
      lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(lvobj, LV_OPA_COVER, LV_PART_MAIN);

  setWindowFlag(NO_FOCUS);
  // NO_FOCUS clears CLICKABLE; restore it so this window captures touch
  // and prevents propagation to ViewMain::onClicked() → QuickMenu.
  lv_obj_add_flag(lvobj, LV_OBJ_FLAG_CLICKABLE);
  delayLoad();
}

FpvDashLayout::~FpvDashLayout()
{
  // s_modelNameObj is set during delayedInit() and referenced by the
  // fpvSentinelFocusAsync callback (via lv_async_call).  Clear it here so
  // that if the callback fires after this layout is destroyed it doesn't
  // dereference the now-deleted LVGL object.
  if (s_modelNameObj == modelName) s_modelNameObj = nullptr;
}

// Intentionally empty — do not open QuickMenu on background touch.
void FpvDashLayout::onClicked() {}

void FpvDashLayout::delayedInit()
{
  const coord_t tY = EdgeTxStyles::MENU_HEADER_HEIGHT;

  // Hide standard topbar; use our own FPV header.
  // setEdgeTxButtonVisible(0) must be called explicitly because the headerIcon
  // is a child of ViewMain (not of TopBar), so setVisible(0) alone does not
  // hide it.  Without this call, the EdgeTX logo button remains visible after
  // a model switch because loadCustomScreens→updateTopbarVisibility reads
  // LAYOUT_OPTION_TOPBAR before delayedInit has cleared it.
  g_model.getScreenLayoutData(screenNum)->options[LAYOUT_OPTION_TOPBAR].value.boolValue = false;
  if (auto* vm = ViewMain::instance()) {
    vm->getTopbar()->setVisible(0.0);
    vm->getTopbar()->setEdgeTxButtonVisible(0.0);
  }

  // Shrink decoration so bottom trims don't overlap the dock
  lv_obj_set_height(decoration->getLvObj(), LCD_H - BottomDock::DOCK_H);

  // FPV header bar: black, full width, thin cyan-tinged bottom border
  lv_obj_t* fpvHeader = lv_obj_create(lvobj);
  lv_obj_set_pos(fpvHeader, 0, 0);
  lv_obj_set_size(fpvHeader, LCD_W, EdgeTxStyles::MENU_HEADER_HEIGHT);
  // Header bar bg — ELRS panel grey #181818
  lv_obj_set_style_bg_color(fpvHeader,
      lv_color_make(0x18, 0x18, 0x18), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(fpvHeader, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(fpvHeader, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(fpvHeader, 0, LV_PART_MAIN);
  lv_obj_set_style_border_side(fpvHeader, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
  lv_obj_set_style_border_width(fpvHeader, 2, LV_PART_MAIN);
  lv_obj_set_style_border_color(fpvHeader,
      lv_color_make(0x44, 0x44, 0x44), LV_PART_MAIN);
  lv_obj_clear_flag(fpvHeader, LV_OBJ_FLAG_CLICKABLE);

  // Centred ELRS rate + power label (replaces EdgeTX title)
  hdrElrsLabel = etx_label_create(fpvHeader, FONT_STD_INDEX);
  lv_obj_set_size(hdrElrsLabel, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_hor(hdrElrsLabel, 6, LV_PART_MAIN);
  lv_obj_set_style_pad_ver(hdrElrsLabel, 1, LV_PART_MAIN);
  lv_obj_set_style_text_align(hdrElrsLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  // Centre between model name (left) and RSSI bars (right)
  lv_obj_align(hdrElrsLabel, LV_ALIGN_CENTER, -25, 0);
  // ELRS green color for rate display
  lv_obj_set_style_text_color(hdrElrsLabel,
      lv_color_make(0x00, 0xA0, 0x00), LV_PART_MAIN);
  lv_label_set_text(hdrElrsLabel, "-Hz  -mW");
  // Clickable: touch or ENTER opens power popup
  lv_obj_add_flag(hdrElrsLabel, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(hdrElrsLabel, LV_OBJ_FLAG_SCROLLABLE);
  etx_solid_bg(hdrElrsLabel, COLOR_DARKGREY_INDEX, LV_STATE_PRESSED);
  // Orange focus highlight (matches other FPV elements)
  lv_obj_set_style_bg_color(hdrElrsLabel,
      lv_color_make(0xFF, 0x8C, 0x00),
      LV_PART_MAIN | LV_STATE_USER_1);
  lv_obj_set_style_bg_opa(hdrElrsLabel, LV_OPA_COVER, LV_STATE_USER_1);
  lv_obj_set_style_text_color(hdrElrsLabel,
      lv_color_black(),
      LV_PART_MAIN | LV_STATE_USER_1);
  etx_obj_add_style(hdrElrsLabel, styles->rounded, LV_STATE_USER_1);
  lv_obj_add_event_cb(hdrElrsLabel, [](lv_event_t* e) {
    auto* self = static_cast<FpvDashLayout*>(lv_event_get_user_data(e));
    if (self) self->showElrsPowerPopup();
  }, LV_EVENT_CLICKED, this);
  // Focus/defocus/cancel callbacks for encoder navigation
  lv_obj_add_event_cb(hdrElrsLabel, [](lv_event_t* e) {
    lv_obj_add_state(lv_event_get_target(e), LV_STATE_USER_1);
  }, LV_EVENT_FOCUSED, nullptr);
  lv_obj_add_event_cb(hdrElrsLabel, [](lv_event_t* e) {
    lv_obj_clear_state(lv_event_get_target(e), LV_STATE_USER_1);
  }, LV_EVENT_DEFOCUSED, nullptr);
  lv_obj_add_event_cb(hdrElrsLabel, [](lv_event_t* e) {
    lv_obj_clear_state(lv_event_get_target(e), LV_STATE_USER_1);
    lv_obj_t* s = BottomDock::getFocusSentinel();
    if (s) lv_group_focus_obj(s);
  }, LV_EVENT_CANCEL, nullptr);

  // Header right: [RSSI bars] [VOL] [VOLT_TEXT] [BATT] — 6px gap, vertically centred
  static constexpr coord_t HDR_CY          = EdgeTxStyles::MENU_HEADER_HEIGHT / 2;  // 26
  static constexpr coord_t HDR_GAP         = 6;
  static constexpr coord_t HDR_BATT_W      = 22;
  static constexpr coord_t HDR_BATT_H      = 13;
  static constexpr coord_t HDR_BATT_X      = LCD_W - HDR_GAP - HDR_BATT_W;         // 452
  static constexpr coord_t HDR_BATT_Y      = HDR_CY - HDR_BATT_H / 2;              // 20

  hdrBattIcon = new StaticIcon(this, HDR_BATT_X, HDR_BATT_Y, ICON_TOPMENU_TXBATT, COLOR_WHITE_INDEX);

  hdrBattFill = lv_obj_create(this->getLvObj());
  lv_obj_set_pos(hdrBattFill, HDR_BATT_X + 2, HDR_BATT_Y + 2);
  lv_obj_set_size(hdrBattFill, 0, 9);
  lv_obj_set_style_bg_opa(hdrBattFill, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(hdrBattFill, lv_color_make(0x00, 0xFF, 0x80), LV_PART_MAIN);
  lv_obj_set_style_border_width(hdrBattFill, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(hdrBattFill, 0, LV_PART_MAIN);
  lv_obj_clear_flag(hdrBattFill, LV_OBJ_FLAG_CLICKABLE);

  // Voltage text: FONT_XS line_height=17, label 18px tall centered at HDR_CY
  // Width 46px (fits "10.0V"), right-aligned so text abuts battery icon
  static constexpr coord_t HDR_VOLT_H = 18;
  static constexpr coord_t HDR_VOLT_W = 46;
  static constexpr coord_t HDR_VOLT_X = HDR_BATT_X - HDR_GAP - HDR_VOLT_W;  // 408
  static constexpr coord_t HDR_VOLT_Y = HDR_CY - HDR_VOLT_H / 2;             // 17
  hdrVoltText = etx_label_create(this, FONT_XS_INDEX);
  lv_obj_set_pos(hdrVoltText, HDR_VOLT_X, HDR_VOLT_Y);
  lv_obj_set_size(hdrVoltText, HDR_VOLT_W, HDR_VOLT_H);
  lv_obj_set_style_text_align(hdrVoltText, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
  etx_txt_color(hdrVoltText, COLOR_WHITE_INDEX);
  lv_label_set_text(hdrVoltText, "--V");

  // Volume icon: right-align within 30px slot, vertically centred
  // Actual icon widths: v0=18 v1=15 v2=19 v3=24 v4=30
  static constexpr coord_t HDR_VOL_H = 16;
  static constexpr coord_t HDR_VOL_W = 30;
  static constexpr coord_t HDR_VOL_X = HDR_VOLT_X - HDR_GAP - HDR_VOL_W;  // 374
  static constexpr coord_t HDR_VOL_Y = HDR_CY - HDR_VOL_H / 2;             // 18
  static const coord_t volIconW[5]   = {18, 15, 19, 24, 30};
  for (int i = 0; i < 5; i++) {
    hdrVolIcon[i] = new StaticIcon(
        this, HDR_VOL_X + HDR_VOL_W - volIconW[i], HDR_VOL_Y,
        (EdgeTxIcon)(ICON_TOPMENU_VOLUME_0 + i),
        COLOR_WHITE_INDEX);
    hdrVolIcon[i]->hide();
  }
  hdrVolIcon[0]->show();

  // RSSI signal bars: 5 bars, bottom baseline = HDR_CY + HDR_BATT_H/2
  static constexpr coord_t HDR_RSSI_BAR_W   = 3;
  static constexpr coord_t HDR_RSSI_BAR_SZ  = 5;
  static constexpr coord_t HDR_RSSI_W       = 4 * HDR_RSSI_BAR_SZ + HDR_RSSI_BAR_W;  // 23px
  static constexpr coord_t HDR_RSSI_X       = HDR_VOL_X - HDR_GAP - HDR_RSSI_W;      // 345
  static constexpr coord_t HDR_RSSI_BOTTOM  = HDR_CY + HDR_BATT_H / 2 + 1;           // 33
  static const uint8_t rssiBarH[]           = {5, 7, 9, 11, 13};
  for (int i = 0; i < 5; i++) {
    uint8_t h = rssiBarH[i];
    hdrRssiBars[i] = lv_obj_create(this->getLvObj());
    lv_obj_set_pos(hdrRssiBars[i], HDR_RSSI_X + i * HDR_RSSI_BAR_SZ, HDR_RSSI_BOTTOM - h);
    lv_obj_set_size(hdrRssiBars[i], HDR_RSSI_BAR_W, h);
    lv_obj_set_style_radius(hdrRssiBars[i], 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(hdrRssiBars[i], 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(hdrRssiBars[i], LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(hdrRssiBars[i], lv_color_make(0x40, 0x40, 0x40), LV_PART_MAIN);
    lv_obj_set_style_bg_color(hdrRssiBars[i], lv_color_make(0x00, 0xFF, 0x80), LV_STATE_USER_1);
    lv_obj_clear_flag(hdrRssiBars[i], LV_OBJ_FLAG_CLICKABLE);
  }

  battOutline = lv_obj_create(lvobj);
  lv_obj_set_pos(battOutline, BAT_X, tY + BAT_Y);
  lv_obj_set_size(battOutline, BAT_W, BAT_H);
  lv_obj_set_style_radius(battOutline, 4, LV_PART_MAIN);
  etx_border_color(battOutline, COLOR_BRIGHTGREEN_INDEX);
  lv_obj_set_style_border_width(battOutline, 2, LV_PART_MAIN);
  etx_remove_bg_color(battOutline);
  lv_obj_clear_flag(battOutline, LV_OBJ_FLAG_CLICKABLE);

  battFill = lv_obj_create(lvobj);
  lv_obj_set_pos(battFill, BAT_X + 2, tY + BAT_Y + 2);
  lv_obj_set_size(battFill, 0, BAT_H - 4);
  lv_obj_set_style_radius(battFill, 2, LV_PART_MAIN);
  etx_solid_bg(battFill, COLOR_BRIGHTGREEN_INDEX);
  lv_obj_clear_flag(battFill, LV_OBJ_FLAG_CLICKABLE);

  battPctLabel = etx_label_create(lvobj, FONT_STD_INDEX);
  lv_obj_set_pos(battPctLabel, BAT_X, tY + BAT_Y);
  lv_obj_set_size(battPctLabel, BAT_W, BAT_H);
  lv_obj_set_style_text_align(battPctLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_pad_all(battPctLabel, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_top(battPctLabel, (BAT_H - 17) / 2, LV_PART_MAIN);
  etx_txt_color(battPctLabel, COLOR_BRIGHTGREEN_INDEX);
  lv_label_set_text(battPctLabel, "-%");
  // Clickable: touch or ENTER opens battery setup
  lv_obj_add_flag(battPctLabel, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(battPctLabel, LV_OBJ_FLAG_SCROLLABLE);
  etx_solid_bg(battPctLabel, COLOR_DARKGREY_INDEX, LV_STATE_PRESSED);
  lv_obj_add_event_cb(battPctLabel, [](lv_event_t* e) {
    auto* self = static_cast<FpvDashLayout*>(lv_event_get_user_data(e));
    if (self) self->showBattSetupPopup();
  }, LV_EVENT_CLICKED, this);
  lv_obj_set_style_bg_color(battPctLabel,
      lv_color_make(0xFF, 0x8C, 0x00),
      LV_PART_MAIN | LV_STATE_USER_1);
  lv_obj_set_style_bg_opa(battPctLabel, LV_OPA_COVER, LV_STATE_USER_1);
  lv_obj_set_style_text_color(battPctLabel,
      lv_color_black(),
      LV_PART_MAIN | LV_STATE_USER_1);
  etx_obj_add_style(battPctLabel, styles->rounded, LV_STATE_USER_1);
  lv_obj_add_event_cb(battPctLabel, [](lv_event_t* e) {
    auto* self = static_cast<FpvDashLayout*>(lv_event_get_user_data(e));
    if (self) self->setBattFocusHighlight(true);
  }, LV_EVENT_FOCUSED, this);
  lv_obj_add_event_cb(battPctLabel, [](lv_event_t* e) {
    auto* self = static_cast<FpvDashLayout*>(lv_event_get_user_data(e));
    if (self) self->setBattFocusHighlight(false);
  }, LV_EVENT_DEFOCUSED, this);
  lv_obj_add_event_cb(battPctLabel, [](lv_event_t* e) {
    auto* self = static_cast<FpvDashLayout*>(lv_event_get_user_data(e));
    if (self) self->setBattFocusHighlight(false);
    lv_obj_t* s = BottomDock::getFocusSentinel();
    if (s) lv_group_focus_obj(s);
  }, LV_EVENT_CANCEL, this);

  // Model bitmap
  modelBitmap = new StaticBitmap(this, {IMG_X, tY + IMG_Y, IMG_W, IMG_H}, nullptr);

  // Hidden file-chooser used to pick the model bitmap via a menu popup.
  m_bitmapPicker = new BitmapPicker(
      this, {0, 0, 0, 0},
      BITMAPS_PATH, BITMAPS_EXT, LEN_BITMAP_NAME,
      []() { return std::string(g_model.header.bitmap, LEN_BITMAP_NAME); },
      [=](std::string newValue) {
        strncpy(g_model.header.bitmap, newValue.c_str(), LEN_BITMAP_NAME);
        auto* m = modelslist.getCurrentModel();
        if (m) {
          strncpy(m->modelBitmap, newValue.c_str(), LEN_BITMAP_NAME);
          m->modelBitmap[LEN_BITMAP_NAME] = '\0';
        }
        storageDirty(EE_MODEL);
        memset(lastBitmap, 0, sizeof(lastBitmap));  // force redraw on next tick
      },
      false, STR_BITMAP);
  lv_obj_add_flag(m_bitmapPicker->getLvObj(), LV_OBJ_FLAG_HIDDEN);

  // Touch + encoder ENTER both open the bitmap file chooser.
  lv_obj_add_flag(modelBitmap->getLvObj(), LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(modelBitmap->getLvObj(), fpvModelBitmap_clicked_cb,
                      LV_EVENT_CLICKED, m_bitmapPicker);
  // Border highlight on USER_1 (focus) state — orange (matches timer)
  lv_obj_set_style_border_color(modelBitmap->getLvObj(),
      lv_color_make(0xFF, 0x8C, 0x00),
      LV_PART_MAIN | LV_STATE_USER_1);
  lv_obj_set_style_border_width(modelBitmap->getLvObj(), 2,
                                LV_PART_MAIN | LV_STATE_USER_1);
  lv_obj_add_event_cb(modelBitmap->getLvObj(), fpvModelBitmap_focused_cb,
                      LV_EVENT_FOCUSED,   nullptr);
  lv_obj_add_event_cb(modelBitmap->getLvObj(), fpvModelBitmap_defocused_cb,
                      LV_EVENT_DEFOCUSED, nullptr);
  lv_obj_add_event_cb(modelBitmap->getLvObj(), fpvModelBitmap_cancel_cb,
                      LV_EVENT_CANCEL,    nullptr);

  // Arm state label — clickable, opens arming mode setup
  armLabel = etx_label_create(lvobj, FONT_XL_INDEX);
  lv_obj_set_pos(armLabel, 100, tY + ARM_Y);
  lv_obj_set_size(armLabel, 280, ARM_H);
  etx_txt_color(armLabel, COLOR_GREY_INDEX);
  etx_obj_add_style(armLabel, styles->text_align_center, LV_PART_MAIN);
  lv_label_set_text(armLabel, "DISARMED");
  lv_obj_add_flag(armLabel, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(armLabel, LV_OBJ_FLAG_SCROLLABLE);
  // Pressed: dark grey bg
  etx_solid_bg(armLabel, COLOR_DARKGREY_INDEX, LV_STATE_PRESSED);
  // Click → arming setup popup
  lv_obj_add_event_cb(armLabel, [](lv_event_t* e) {
    auto* self = static_cast<FpvDashLayout*>(lv_event_get_user_data(e));
    if (self) self->showArmSetupPopup();
  }, LV_EVENT_CLICKED, this);
  lv_obj_set_style_bg_color(armLabel,
      lv_color_make(0xFF, 0x8C, 0x00),
      LV_PART_MAIN | LV_STATE_USER_1);
  lv_obj_set_style_bg_opa(armLabel, LV_OPA_COVER, LV_STATE_USER_1);
  lv_obj_set_style_text_color(armLabel,
      lv_color_black(),
      LV_PART_MAIN | LV_STATE_USER_1);
  etx_obj_add_style(armLabel, styles->rounded, LV_STATE_USER_1);
  lv_obj_add_event_cb(armLabel, [](lv_event_t* e) {
    lv_obj_add_state(lv_event_get_target(e), LV_STATE_USER_1);
  }, LV_EVENT_FOCUSED, nullptr);
  lv_obj_add_event_cb(armLabel, [](lv_event_t* e) {
    lv_obj_clear_state(lv_event_get_target(e), LV_STATE_USER_1);
  }, LV_EVENT_DEFOCUSED, nullptr);
  lv_obj_add_event_cb(armLabel, [](lv_event_t* e) {
    lv_obj_clear_state(lv_event_get_target(e), LV_STATE_USER_1);
    lv_obj_t* s = BottomDock::getFocusSentinel();
    if (s) lv_group_focus_obj(s);
  }, LV_EVENT_CANCEL, nullptr);

  // Model name — header left, vertically centred
  // LV_SIZE_CONTENT: border follows text width dynamically
  static constexpr coord_t HDR_MN_X = 8;
  static constexpr coord_t HDR_MN_H = 22;
  static constexpr coord_t HDR_MN_Y = HDR_CY - HDR_MN_H / 2;
  modelName = etx_label_create(this, FONT_STD_INDEX);
  lv_obj_set_pos(modelName, HDR_MN_X, HDR_MN_Y);
  lv_obj_set_size(modelName, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_hor(modelName, 3, LV_PART_MAIN);
  lv_obj_set_style_pad_ver(modelName, 1, LV_PART_MAIN);
  etx_txt_color(modelName, COLOR_WHITE_INDEX);
  lv_obj_set_style_text_align(modelName, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
  lv_label_set_text(modelName, "---");
  s_modelNameObj = modelName;
  // Touch or encoder ENTER both open model manager.
  lv_obj_add_flag(modelName, LV_OBJ_FLAG_CLICKABLE);
  etx_solid_bg(modelName, COLOR_DARKGREY_INDEX, LV_STATE_PRESSED);
  lv_obj_add_event_cb(modelName, fpvModelName_clicked_cb,   LV_EVENT_CLICKED,   nullptr);
  // Orange focus (matches timer selection): orange bg + white text
  lv_obj_set_style_bg_color(modelName,
      lv_color_make(0xFF, 0x8C, 0x00),
      LV_PART_MAIN | LV_STATE_USER_1);
  lv_obj_set_style_bg_opa(modelName, LV_OPA_COVER, LV_STATE_USER_1);
  lv_obj_set_style_text_color(modelName,
      lv_color_black(),
      LV_PART_MAIN | LV_STATE_USER_1);
  etx_obj_add_style(modelName, styles->rounded, LV_STATE_USER_1);
  lv_obj_add_event_cb(modelName, fpvModelName_focused_cb,   LV_EVENT_FOCUSED,   nullptr);
  lv_obj_add_event_cb(modelName, fpvModelName_defocused_cb, LV_EVENT_DEFOCUSED, nullptr);
  lv_obj_add_event_cb(modelName, fpvModelName_cancel_cb,    LV_EVENT_CANCEL,    nullptr);

  // Timer 1 (top, below battery bar)
  timer2Label = etx_label_create(lvobj, FONT_XL_INDEX);
  lv_obj_set_pos(timer2Label, TMR_X, tY + TMR2_Y);
  lv_obj_set_size(timer2Label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(timer2Label, 0, LV_PART_MAIN);
  etx_txt_color(timer2Label, COLOR_WHITE_INDEX);
  etx_bg_color(timer2Label, COLOR_THEME_QM_BG_INDEX);
  etx_obj_add_style(timer2Label, styles->bg_opacity_90, LV_PART_MAIN);
  etx_obj_add_style(timer2Label, styles->text_align_left, LV_PART_MAIN);
  lv_label_set_text(timer2Label, "0:00.00");
  lv_obj_add_flag(timer2Label, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(timer2Label, LV_OBJ_FLAG_SCROLLABLE);
  // Pressed & focused: same orange highlight
  lv_obj_set_style_bg_color(timer2Label, lv_color_make(0xFF, 0x8C, 0x00), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(timer2Label, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_text_color(timer2Label, lv_color_white(), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(timer2Label, lv_color_make(0xFF, 0x8C, 0x00), LV_PART_MAIN | LV_STATE_USER_1);
  lv_obj_set_style_bg_opa(timer2Label, LV_OPA_COVER, LV_STATE_USER_1);
  lv_obj_set_style_text_color(timer2Label, lv_color_white(), LV_PART_MAIN | LV_STATE_USER_1);
  etx_obj_add_style(timer2Label, styles->rounded, LV_STATE_USER_1);
  lv_obj_add_event_cb(timer2Label, fpvTimer1_clicked_cb,       LV_EVENT_CLICKED,       nullptr);
  lv_obj_add_event_cb(timer2Label, fpvTimer1_long_pressed_cb,  LV_EVENT_LONG_PRESSED,  nullptr);
  lv_obj_add_event_cb(timer2Label, fpvTimerLabel_focused_cb,   LV_EVENT_FOCUSED,       nullptr);
  lv_obj_add_event_cb(timer2Label, fpvTimerLabel_defocused_cb, LV_EVENT_DEFOCUSED, nullptr);
  lv_obj_add_event_cb(timer2Label, fpvTimerLabel_cancel_cb,    LV_EVENT_CANCEL,    nullptr);

  // Timer 2 (below T1)
  timerLabel = etx_label_create(lvobj, FONT_XL_INDEX);
  lv_obj_set_pos(timerLabel, TMR_X, tY + TMR_Y);
  lv_obj_set_size(timerLabel, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(timerLabel, 0, LV_PART_MAIN);
  etx_txt_color(timerLabel, COLOR_WHITE_INDEX);
  etx_bg_color(timerLabel, COLOR_THEME_QM_BG_INDEX);
  etx_obj_add_style(timerLabel, styles->bg_opacity_90, LV_PART_MAIN);
  etx_obj_add_style(timerLabel, styles->text_align_left, LV_PART_MAIN);
  lv_label_set_text(timerLabel, "0:00.00");
  lv_obj_add_flag(timerLabel, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(timerLabel, LV_OBJ_FLAG_SCROLLABLE);
  // Pressed & focused: same orange highlight
  lv_obj_set_style_bg_color(timerLabel, lv_color_make(0xFF, 0x8C, 0x00), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(timerLabel, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_text_color(timerLabel, lv_color_white(), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(timerLabel, lv_color_make(0xFF, 0x8C, 0x00), LV_PART_MAIN | LV_STATE_USER_1);
  lv_obj_set_style_bg_opa(timerLabel, LV_OPA_COVER, LV_STATE_USER_1);
  lv_obj_set_style_text_color(timerLabel, lv_color_white(), LV_PART_MAIN | LV_STATE_USER_1);
  etx_obj_add_style(timerLabel, styles->rounded, LV_STATE_USER_1);
  lv_obj_add_event_cb(timerLabel, fpvTimer2_clicked_cb,        LV_EVENT_CLICKED,       nullptr);
  lv_obj_add_event_cb(timerLabel, fpvTimer2_long_pressed_cb,   LV_EVENT_LONG_PRESSED,  nullptr);
  lv_obj_add_event_cb(timerLabel, fpvTimerLabel_focused_cb,    LV_EVENT_FOCUSED,       nullptr);
  lv_obj_add_event_cb(timerLabel, fpvTimerLabel_defocused_cb,  LV_EVENT_DEFOCUSED, nullptr);
  lv_obj_add_event_cb(timerLabel, fpvTimerLabel_cancel_cb,     LV_EVENT_CANCEL,    nullptr);

  // ── Encoder navigation order ──
  lv_group_t* navGrp = lv_group_get_default();
  if (navGrp) {
    lv_group_add_obj(navGrp, modelName);              // 1. top-left header
    lv_group_add_obj(navGrp, hdrElrsLabel);            // 2. centre header (ELRS info)
    lv_group_add_obj(navGrp, modelBitmap->getLvObj());// 3. center
    lv_group_add_obj(navGrp, armLabel);                // 4. center-bottom (DISARMED)
    lv_group_add_obj(navGrp, battPctLabel);            // 4. top-right (battery)
    lv_group_add_obj(navGrp, timer2Label);             // 5. right (timer 1)
    lv_group_add_obj(navGrp, timerLabel);              // 6. right (timer 2)
  }

  // Re-run updateDecorations() so derived vtable is used
  updateDecorations();

  // Recolour trim tracks to dark grey
  {
    lv_obj_t* dec = decoration->getLvObj();
    uint32_t nBoxes = lv_obj_get_child_cnt(dec);
    for (uint32_t bi = 0; bi < nBoxes; bi++) {
      lv_obj_t* box = lv_obj_get_child(dec, bi);
      lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, LV_PART_MAIN);
      uint32_t nWins = lv_obj_get_child_cnt(box);
      for (uint32_t wi = 0; wi < nWins; wi++) {
        lv_obj_t* win = lv_obj_get_child(box, wi);
        lv_obj_set_style_bg_opa(win, LV_OPA_TRANSP, LV_PART_MAIN);
        if (lv_obj_get_child_cnt(win) > 0) {
          lv_obj_t* bar = lv_obj_get_child(win, 0);
          lv_obj_set_style_bg_color(bar, lv_color_make(0x30, 0x30, 0x30), LV_PART_MAIN);
          lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
        }
      }
    }
  }

  updateTopbarInfo();
  updateBattery();
  updateTelemetry();
  updateModelInfo();
  updateTimer();
}

void FpvDashLayout::updateTopbarInfo()
{
  if (!loaded || _deleted) return;

  int16_t vbat = g_vbat100mV;
  if (vbat != lastHdrBatt) {
    lastHdrBatt = vbat;
    static constexpr coord_t BATT_FILL_MAX_W = 20;
    // GET_TXBATT_BARS uses g_eeGeneral.vBatMin/Max — same as RadioInfoWidget
    coord_t bars = (coord_t)GET_TXBATT_BARS(BATT_FILL_MAX_W);
    lv_obj_set_width(hdrBattFill, bars);
    int pct = bars * 100 / BATT_FILL_MAX_W;
    lv_color_t col = pct >= 40 ? lv_color_make(0x00, 0xFF, 0x80)
                   : pct >= 20 ? lv_color_make(0xFF, 0xD0, 0x00)
                               : lv_color_make(0xFF, 0x30, 0x30);
    lv_obj_set_style_bg_color(hdrBattFill, col, LV_PART_MAIN);
    if (hdrVoltText) {
      char buf[12];
      snprintf(buf, sizeof(buf), "%.1fV", vbat * 0.1f);
      lv_label_set_text(hdrVoltText, buf);
    }
  }

  uint8_t vol;
  if (requiredSpeakerVolume == 0 || g_eeGeneral.beepMode == e_mode_quiet)
    vol = 0;
  else if (requiredSpeakerVolume < 7)
    vol = 1;
  else if (requiredSpeakerVolume < 13)
    vol = 2;
  else if (requiredSpeakerVolume < 19)
    vol = 3;
  else
    vol = 4;

  if (vol != lastHdrVol) {
    lastHdrVol = vol;
    for (int i = 0; i < 5; i++) {
      if (!hdrVolIcon[i]) continue;
      if (i == (int)vol) hdrVolIcon[i]->show();
      else               hdrVolIcon[i]->hide();
    }
  }
}

void FpvDashLayout::updateBattery()
{
  if (!loaded || _deleted) return;

  int pct = -1;
  float volts = lastRxbtAvail ? (lastRxbtValue * 0.1f) : -1.0f;

  // Show scanning progress bar on battery bar
  if (_scanning) {
    int scanPct = (_rxbtSearchTimer * 100) / 300;  // 0→100% over 3s
    if (scanPct > 100) scanPct = 100;

    coord_t fillW = (coord_t)((BAT_W - 4) * scanPct / 100);
    lv_obj_set_pos(battFill, BAT_X + 2, EdgeTxStyles::MENU_HEADER_HEIGHT + BAT_Y + 2);
    lv_obj_set_size(battFill, fillW, BAT_H - 4);
    etx_solid_bg(battFill, COLOR_YELLOW_INDEX);
    etx_border_color(battOutline, COLOR_YELLOW_INDEX);

    char buf[16];
    snprintf(buf, sizeof(buf), "%s %d%%", STR_DISCOVER, scanPct);
    lv_label_set_text(battPctLabel, buf);
    lastBattPct = -1;  // force redraw when scan completes
    return;
  }

  if (volts > 0) {
    // Auto-detect or use configured cell count
    uint8_t cells = _battCells;
    if (cells == 0) {
      // Auto-detect cell count from voltage (supports LiHV 4.35V/cell)
      if      (volts < 5.0f)  cells = 1;
      else if (volts < 9.5f)  cells = 2;
      else if (volts < 13.5f) cells = 3;
      else if (volts < 18.0f) cells = 4;
      else if (volts < 22.0f) cells = 5;
      else                     cells = 6;
    }

    float cellV = volts / cells;
    float fullV  = _battHV ? 4.35f : 4.20f;
    float emptyV = 3.50f;
    pct = (int)((cellV - emptyV) / (fullV - emptyV) * 100.0f);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
  }

  if (pct < 0) pct = 0;  // no telemetry → empty
  if (pct == lastBattPct) return;
  lastBattPct = pct;

  // Horizontal bar: grows left→right
  coord_t fillW = (coord_t)((BAT_W - 4) * pct / 100);
  lv_obj_set_pos(battFill, BAT_X + 2, EdgeTxStyles::MENU_HEADER_HEIGHT + BAT_Y + 2);
  lv_obj_set_size(battFill, fillW, BAT_H - 4);

  LcdColorIndex col = pct < 20 ? COLOR_RED_INDEX
                    : pct < 40 ? COLOR_YELLOW_INDEX
                               : COLOR_BRIGHTGREEN_INDEX;
  etx_solid_bg(battFill, col);
  etx_border_color(battOutline, col);
  etx_txt_color(battPctLabel, COLOR_WHITE_INDEX);

  char buf[12];
  if (volts > 0)
    snprintf(buf, sizeof(buf), "%.1fV %d%%", volts, pct);
  else
    snprintf(buf, sizeof(buf), "--V");
  lv_label_set_text(battPctLabel, buf);
}

void FpvDashLayout::updateTelemetry()
{
  if (!loaded || _deleted) return;

  // RSSI bars
  uint8_t rssi = (uint8_t)TELEMETRY_RSSI();
  if (rssi != lastRssi) {
    lastRssi = rssi;
    // Update header RSSI bars (thresholds same as RadioInfoWidget)
    static const uint8_t rssiThresholds[] = {30, 40, 50, 60, 80};
    for (int i = 0; i < 5; i++) {
      if (rssi >= rssiThresholds[i])
        lv_obj_add_state(hdrRssiBars[i], LV_STATE_USER_1);
      else
        lv_obj_clear_state(hdrRssiBars[i], LV_STATE_USER_1);
    }
  }

  // RxBt sensor discovery — keep searching while _scanning is true
  if (rxbtSensorIdx == -2) {
    for (int i = 0; i < MAX_TELEMETRY_SENSORS; i++) {
      // Accept both available (ever received) and fresh (recently updated)
      if (!telemetryItems[i].isAvailable() && !telemetryItems[i].isFresh())
        continue;
      if (strncmp(g_model.telemetrySensors[i].label, "RxBt", TELEM_LABEL_LEN) == 0) {
        rxbtSensorIdx = i;
        _scanning = false;
        allowNewSensors = false;
        break;
      }
    }
  }

  // Scanning timeout: stop after ~3 seconds if not found
  if (_scanning && rxbtSensorIdx == -2) {
    if (++_rxbtSearchTimer > 300) {  // 300 * 10ms = 3s
      _rxbtSearchTimer = 0;
      rxbtSensorIdx = -1;
      _scanning = false;
      allowNewSensors = false;
    }
  }

  // Read RxBt voltage — keep last known value when telemetry is briefly stale
  if (rxbtSensorIdx >= 0) {
    if (telemetryItems[rxbtSensorIdx].isFresh()) {
      int32_t val = telemetryItems[rxbtSensorIdx].value;
      if (val != lastRxbtValue || !lastRxbtAvail) {
        lastRxbtValue = val;
        lastRxbtAvail = true;
        updateBattery();
      }
    }
    // Don't clear value on brief telemetry gaps — just keep last known
  }
}

void FpvDashLayout::updateModelInfo()
{
  if (!loaded || _deleted) return;

  char name[LEN_MODEL_NAME + 1];
  strAppend(name, g_model.header.name, LEN_MODEL_NAME);
  if (strncmp(name, lastModelName, LEN_MODEL_NAME) != 0) {
    strncpy(lastModelName, name, LEN_MODEL_NAME);
    lv_label_set_text(modelName, name[0] ? name : "---");
  }

  // Update model bitmap
  char bmp[LEN_BITMAP_NAME + 1];
  strAppend(bmp, g_model.header.bitmap, LEN_BITMAP_NAME);
  if (strncmp(bmp, lastBitmap, LEN_BITMAP_NAME) != 0) {
    strncpy(lastBitmap, bmp, LEN_BITMAP_NAME);
    if (modelBitmap) {
      if (bmp[0]) {
        std::string fullpath = std::string(BITMAPS_PATH PATH_SEPARATOR) + bmp;
        modelBitmap->setSource(fullpath.c_str());
      } else {
        modelBitmap->clearSource();
      }
    }
  }

  // Arm state — explicit CRSF/ELRS arming setup wins over wizard SF and CH5
  bool armed = false;
  bool resolved = false;

  // Resolve the same module the arm setup popup writes to
  int armModuleIdx = getElrsModuleIdx();
  if (armModuleIdx < 0) {
    if (isModuleCrossfire(INTERNAL_MODULE))
      armModuleIdx = INTERNAL_MODULE;
    else if (isModuleCrossfire(EXTERNAL_MODULE))
      armModuleIdx = EXTERNAL_MODULE;
  }

  if (armModuleIdx >= 0) {
    ModuleData* md = &g_model.moduleData[armModuleIdx];
    if (md->crsf.crsfArmingMode == ARMING_MODE_SWITCH) {
      swsrc_t sw = md->crsf.crsfArmingTrigger;
      if (sw != SWSRC_NONE) {
        armed = getSwitch(sw, 0);
        resolved = true;
      }
    } else {
      armed = (channelOutputs[4] > 0);
      resolved = true;
    }
  }

  // Fallback: wizard-style safety switch (FUNC_OVERRIDE_CHANNEL holds motor low)
  if (!resolved) {
    for (int i = 0; i < MAX_SPECIAL_FUNCTIONS; i++) {
      auto* cfn = &g_model.customFn[i];
      if (CFN_ACTIVE(cfn) && CFN_FUNC(cfn) == FUNC_OVERRIDE_CHANNEL &&
          CFN_SWITCH(cfn) != SWSRC_NONE) {
        armed = !getSwitch(CFN_SWITCH(cfn), 0);
        resolved = true;
        break;
      }
    }
  }

  if (!resolved) armed = (channelOutputs[4] > 0);

  _lastArmed = armed;

  lv_label_set_text(armLabel, armed ? "ARMED" : "DISARMED");
  etx_txt_color(armLabel, armed ? COLOR_RED_INDEX : COLOR_GREY_INDEX);
}

void FpvDashLayout::updateTimer()
{
  if (!loaded || _deleted) return;

  // Timer 1 (top, below battery bar)
  uint32_t val = (uint32_t)abs(timersStates[0].val);
  uint8_t state = timersStates[0].state;
  if (val != lastTimerVal || state != lastTimerState) {
    lastTimerVal = val;
    lastTimerState = state;
    lv_label_set_text(timer2Label, formatTimer(val).c_str());
    if (state == TMR_RUNNING)
      etx_txt_color(timer2Label, COLOR_ORANGE_INDEX);
    else
      etx_txt_color(timer2Label, COLOR_WHITE_INDEX);
  }

  // Timer 2 (below T1)
  uint32_t val2 = (uint32_t)abs(timersStates[1].val);
  uint8_t state2 = timersStates[1].state;
  if (val2 != lastTimer2Val || state2 != lastTimer2State) {
    lastTimer2Val = val2;
    lastTimer2State = state2;
    lv_label_set_text(timerLabel, formatTimer(val2).c_str());
    if (state2 == TMR_RUNNING)
      etx_txt_color(timerLabel, COLOR_ORANGE_INDEX);
    else
      etx_txt_color(timerLabel, COLOR_WHITE_INDEX);
  }
}

void FpvDashLayout::checkEvents()
{
  Layout::checkEvents();

  updateTopbarInfo();
  updateElrsHeader();
  updateBattery();
  updateTelemetry();
  updateModelInfo();
  updateTimer();
}

int FpvDashLayout::getElrsModuleIdx()
{
  if (isModuleELRS(INTERNAL_MODULE))
    return INTERNAL_MODULE;
  if (isModuleELRS(EXTERNAL_MODULE))
    return EXTERNAL_MODULE;
  return -1;
}

void FpvDashLayout::showArmSetupPopup()
{
  int idx = getElrsModuleIdx();
  if (idx < 0) return;

  // Only show for ELRS >= 4.0 (matches built-in RF menu behaviour)
  if (!CRSF_ELRS_MIN_VER(idx, 4, 0)) return;

  ModuleData* md = &g_model.moduleData[idx];

  auto* menu = new Menu();
  menu->setTitle(STR_CRSF_ARMING_MODE);

  menu->addLine(STR_CRSF_ARMING_MODES[ARMING_MODE_CH5], [=]() {
    md->crsf.crsfArmingMode = ARMING_MODE_CH5;
    storageDirty(EE_MODEL);
  }, [=]() { return md->crsf.crsfArmingMode == ARMING_MODE_CH5; });

  menu->addLine(STR_CRSF_ARMING_MODES[ARMING_MODE_SWITCH], [=]() {
    md->crsf.crsfArmingMode = ARMING_MODE_SWITCH;
    storageDirty(EE_MODEL);
    // Use SwitchChoice for categorized switch picker (matches built-in RF menu)
    auto* swChoice = new SwitchChoice(this, {0, 0, 0, 0},
        SWSRC_FIRST, SWSRC_LAST,
        [=]() -> int16_t { return md->crsf.crsfArmingTrigger; },
        [=](int16_t v) { md->crsf.crsfArmingTrigger = v; storageDirty(EE_MODEL); });
    swChoice->setAvailableHandler([=](int sw) { return isSwitchAvailableForArming(sw); });
    lv_obj_add_flag(swChoice->getLvObj(), LV_OBJ_FLAG_HIDDEN);
    swChoice->onClicked();
  }, [=]() { return md->crsf.crsfArmingMode == ARMING_MODE_SWITCH; });
}

void FpvDashLayout::setBattFocusHighlight(bool focused)
{
  if (!battPctLabel) return;
  if (focused)
    lv_obj_add_state(battPctLabel, LV_STATE_USER_1);
  else
    lv_obj_clear_state(battPctLabel, LV_STATE_USER_1);
}

void FpvDashLayout::startSensorDiscovery()
{
  rxbtSensorIdx = -2;  // trigger re-discovery on next updateTelemetry()
  _scanning = true;
  _rxbtSearchTimer = 0;
  allowNewSensors = true;  // enable auto-discovery of new telemetry sensors
}

void FpvDashLayout::showBattSetupPopup()
{
  // Popup that closes on EXIT key (matches timer popup behaviour)
  struct PopupWindow : ModalWindow {
    using ModalWindow::ModalWindow;
    void onCancel() override { deleteLater(); }
  };

  // FPV dark popup style (matching timer/flight-mode popups)
  static constexpr coord_t POPUP_W = LCD_W * 70 / 100;
  auto* dlg = new PopupWindow(true);

  auto* form = new Window(dlg, {0, 0, POPUP_W, LV_SIZE_CONTENT});
  form->padAll(PAD_ZERO);
  form->setFlexLayout(LV_FLEX_FLOW_COLUMN, PAD_ZERO, POPUP_W, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(form->getLvObj(), lv_color_make(0x18, 0x18, 0x18), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(form->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_outline_width(form->getLvObj(), 1, LV_PART_MAIN);
  lv_obj_set_style_outline_color(form->getLvObj(), lv_color_make(0x44, 0x44, 0x44), LV_PART_MAIN);
  lv_obj_center(form->getLvObj());
  // EXIT key closes popup
  lv_obj_add_event_cb(form->getLvObj(), [](lv_event_t* e) {
    auto* d = static_cast<PopupWindow*>(lv_event_get_user_data(e));
    d->deleteLater();
  }, LV_EVENT_CANCEL, dlg);

  // Title bar (#222222 bg, white text)
  auto* hdr = new StaticText(form, {0, 0, LV_PCT(100), 0}, STR_BATT_LABEL,
                             COLOR_THEME_QM_FG_INDEX);
  lv_obj_set_style_bg_color(hdr->getLvObj(), lv_color_make(0x22, 0x22, 0x22), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(hdr->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
  hdr->padAll(PAD_SMALL);

  // Content area — matches timer popup list style
  auto* content = new Window(form, {0, 0, LV_PCT(100), LV_SIZE_CONTENT});
  content->padAll(PAD_MEDIUM);
  content->setFlexLayout(LV_FLEX_FLOW_COLUMN, PAD_TINY, LV_PCT(100), LV_SIZE_CONTENT);

  static constexpr coord_t HALF_W = POPUP_W / 2;

  // ── Row helper: dark-styled row matching timer popup ──
  auto makeRow = [](Window* parent) -> Window* {
    auto* r = new Window(parent, {0, 0, LV_PCT(100), EdgeTxStyles::UI_ELEMENT_HEIGHT + PAD_SMALL});
    r->padAll(PAD_ZERO);
    lv_obj_set_style_bg_color(r->getLvObj(), lv_color_make(0x28, 0x28, 0x28), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(r->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
    return r;
  };

  // Row 1: Cells
  auto* row1 = makeRow(content);
  auto* lbl1 = new StaticText(row1, {PAD_MEDIUM, 0, LV_SIZE_CONTENT, LV_SIZE_CONTENT},
                              STR_FPV_BATT_CELLS, COLOR_THEME_QM_FG_INDEX, LEFT);
  lv_obj_align(lbl1->getLvObj(), LV_ALIGN_LEFT_MID, 0, 0);
  static const char* cellOpts[] = {STR_FPV_BATT_AUTO,"1S","2S","3S","4S","5S","6S","7S","8S",nullptr};
  auto* cellChoice = new Choice(row1, {0, 0, HALF_W - PAD_LARGE, EdgeTxStyles::STD_FONT_HEIGHT},
      cellOpts, 0, 8,
      [=]() -> int { return _battCells; },
      [=](int v) { _battCells = v; });
  applyDarkBtnStyle(cellChoice->getLvObj());
  lv_obj_align(cellChoice->getLvObj(), LV_ALIGN_RIGHT_MID, -PAD_LARGE, 0);

  // Row 2: HV
  auto* row2 = makeRow(content);
  auto* lbl2 = new StaticText(row2, {PAD_MEDIUM, 0, LV_SIZE_CONTENT, LV_SIZE_CONTENT},
                              STR_FPV_BATT_HV, COLOR_THEME_QM_FG_INDEX, LEFT);
  lv_obj_align(lbl2->getLvObj(), LV_ALIGN_LEFT_MID, 0, 0);
  auto* hvToggle = new ToggleSwitch(row2, {0, 0, 40, EdgeTxStyles::STD_FONT_HEIGHT},
      [=]() -> uint8_t { return _battHV ? 1 : 0; },
      [=](uint8_t v) { _battHV = (v != 0); });
  lv_obj_align(hvToggle->getLvObj(), LV_ALIGN_RIGHT_MID, -PAD_LARGE, 0);

  // Row 3: Scan
  auto* scanBtn = new TextButton(content, {0, 0, LV_PCT(100), EdgeTxStyles::UI_ELEMENT_HEIGHT},
                                 STR_DISCOVER_SENSORS, [=]() -> uint8_t {
    startSensorDiscovery();
    dlg->deleteLater();
    return 1;
  });
  applyDarkBtnStyle(scanBtn->getLvObj());
}

// ─────────────────────────────────────────────────────────────────────────────
// ELRS Header: display current packet rate + TX power in the FPV title bar.
// Uses the background cache loader (ElrsBgLoader) via triggerCacheLoad().
// ─────────────────────────────────────────────────────────────────────────────

// ─────────────────────────────────────────────────────────────────────────────
// ELRS Header: display current packet rate + TX power in the FPV title bar.
// Uses ElrsParamBrowser::triggerCacheLoad() → ElrsBgLoader for background loading.
// ─────────────────────────────────────────────────────────────────────────────

void FpvDashLayout::updateElrsHeader()
{
  if (!loaded || _deleted || !hdrElrsLabel) return;

  bool hasCrsfModule = (isModuleCrossfire(INTERNAL_MODULE) ||
                        isModuleCrossfire(EXTERNAL_MODULE));

  // ── Break chicken-and-egg: PING until the module replies with DEVICE_INFO
  //     and sets isELRS = true.  Once ELRS is confirmed the loader takes over.
  if (hasCrsfModule && !isModuleELRS(INTERNAL_MODULE) &&
      !isModuleELRS(EXTERNAL_MODULE)) {
    tmr10ms_t now = get_tmr10ms();
    if (now - _lastElrsPing >= 10) {  // every 100ms
      _lastElrsPing = now;
      // Quick inline PING — same format as ElrsParamBrowser::sendPing()
      uint8_t modIdx = 0xFF;
      for (uint8_t m = 0; m < NUM_MODULES; m++) {
        if (moduleState[m].protocol == PROTOCOL_CHANNELS_CROSSFIRE) {
          modIdx = m; break;
        }
      }
      if (modIdx != 0xFF && outputTelemetryBuffer.isAvailable()) {
        uint8_t ep = (modIdx == INTERNAL_MODULE) ? 0 : TELEMETRY_ENDPOINT_SPORT;
        outputTelemetryBuffer.pushByte(MODULE_ADDRESS);
        outputTelemetryBuffer.pushByte(2 + 2);
        outputTelemetryBuffer.pushByte(0x28);
        outputTelemetryBuffer.pushByte(0x00);
        outputTelemetryBuffer.pushByte(0xEF);
        outputTelemetryBuffer.pushByte(crc8(outputTelemetryBuffer.data + 2, 3));
        outputTelemetryBuffer.setDestination(ep);
      }
    }
  }

  // ── Use existing background cache loader (ElrsBgLoader). ───────────
  if (hasCrsfModule && !g_elrsCache.valid) {
    static tmr10ms_t _nextRetry = 0;
    tmr10ms_t now = get_tmr10ms();
    if (now >= _nextRetry) {
      _nextRetry = now + 30;  // retry every 300 ms
      ElrsParamBrowser::triggerCacheLoad();
    }
  }

  int elrsIdx = getElrsModuleIdx();
  if (!hasCrsfModule && elrsIdx < 0) {
    lv_label_set_text(hdrElrsLabel, "no ELRS");
    return;
  }

  // ── Fast path — available long before the parameter cache is populated:
  //     the mixer scheduler period gives the packet rate once the module
  //     reports its refresh rate, TPWR arrives with the first LinkStatistics
  //     frame. The cache only refines these values below. ────────────────
  char     rateStr[16] = "-Hz";
  uint32_t txPowerMw   = 0;
  bool     pwFound     = false;

  // Until the module syncs, the scheduler still runs at the baudrate-derived
  // default, which has nothing to do with the actual RF packet rate.
  int crsfIdx = isModuleCrossfire(INTERNAL_MODULE)   ? INTERNAL_MODULE
              : isModuleCrossfire(EXTERNAL_MODULE)   ? EXTERNAL_MODULE
                                                     : -1;
  if (crsfIdx >= 0 && getModuleSyncStatus(crsfIdx).isValid()) {
    uint32_t period = getMixerSchedulerPeriod();
    if (period) {
      unsigned rateHz = 1000000U / period;
      if (rateHz > 0) snprintf(rateStr, sizeof(rateStr), "%uHz", rateHz);
    }
  }

  // TPWR sensor already holds mW (CRSF power index is mapped in crossfire.cpp)
  if (elrsTpwrIdx < 0) {
    tmr10ms_t now = get_tmr10ms();
    if (now - _lastTpwrScan >= 50) {  // rescan every 500 ms until found
      _lastTpwrScan = now;
      for (int i = 0; i < MAX_TELEMETRY_SENSORS; i++) {
        if (!telemetryItems[i].isAvailable() && !telemetryItems[i].isFresh())
          continue;
        if (strncmp(g_model.telemetrySensors[i].label, "TPWR", TELEM_LABEL_LEN) == 0) {
          elrsTpwrIdx = i;
          break;
        }
      }
    }
  }
  if (elrsTpwrIdx >= 0 && telemetryItems[elrsTpwrIdx].isAvailable()) {
    txPowerMw = (uint32_t)telemetryItems[elrsTpwrIdx].value;
    pwFound = true;
  }

  // ── Rate: refine with the cached Packet Rate label when available ──────
  if (g_elrsCache.valid && !g_elrsCache.fields.empty()) {
    for (auto& f : g_elrsCache.fields) {
      std::string lower = f.name;
      for (auto& c : lower) c = (char)tolower((unsigned char)c);
      if (lower.find("packet") == std::string::npos
          && lower.find("rate") == std::string::npos) continue;
      if (f.value >= 0 && (size_t)f.value < f.options.size()) {
        std::string label = f.options[f.value];
        // Keep the "D" prefix — D250Hz (DVDA) is not the same as plain 250Hz
        // Strip sensitivity suffix e.g. "(-105dBm)"
        auto paren = label.find('(');
        if (paren != std::string::npos)
          label.erase(paren);
        while (!label.empty() && label.back() == ' ') label.pop_back();
        strncpy(rateStr, label.c_str(), sizeof(rateStr) - 1);
        rateStr[sizeof(rateStr) - 1] = '\0';
      }
      break;
    }
  }

  // ── Power: refine with the cached Power field when available ──────────
  if (g_elrsCache.valid && !g_elrsCache.fields.empty()) {
    for (auto& f : g_elrsCache.fields) {
      if (f.type != ElrsParamBrowser::FT_SELECT) continue;
      std::string lower = f.name;
      for (auto& c : lower) c = (char)tolower((unsigned char)c);
      if (lower.find("power") == std::string::npos
          && lower.find("pwr") == std::string::npos) continue;
      if (f.value >= 0 && (size_t)f.value < f.options.size()) {
        txPowerMw = (uint32_t)atoi(f.options[f.value].c_str());
        pwFound = true;
      }
      break;
    }
  }

  if (strcmp(rateStr, lastElrsRateStr) != 0 || txPowerMw != lastElrsTxPower ||
      pwFound != lastElrsPwFound) {
    strncpy(lastElrsRateStr, rateStr, sizeof(lastElrsRateStr) - 1);
    lastElrsTxPower = txPowerMw;
    lastElrsPwFound = pwFound;

    char buf[48];
    if (pwFound) {
      if (txPowerMw >= 1000)
        snprintf(buf, sizeof(buf), "%s  %.1fW", rateStr, txPowerMw / 1000.0f);
      else
        snprintf(buf, sizeof(buf), "%s  %umW", rateStr, (unsigned)txPowerMw);
    } else {
      snprintf(buf, sizeof(buf), "%s  -mW", rateStr);
    }
    lv_label_set_text(hdrElrsLabel, buf);

    lv_color_t pwColor;
    if (txPowerMw <= 100)
      pwColor = lv_color_make(0x00, 0xA0, 0x00);
    else if (txPowerMw <= 500)
      pwColor = lv_color_make(0xFF, 0xD0, 0x00);
    else
      pwColor = lv_color_make(0xFF, 0x30, 0x30);
    lv_obj_set_style_text_color(hdrElrsLabel, pwColor, LV_PART_MAIN);
  }
}

// CRSF output helper — pushes a single CRSF frame onto the output telemetry buffer
static bool fpvCrsfPush(uint8_t cmd, const uint8_t* payload, uint8_t len)
{
  if (!outputTelemetryBuffer.isAvailable()) return false;

  // Find active CRSF module
  uint8_t modIdx = 0xFF;
  for (uint8_t m = 0; m < NUM_MODULES; m++) {
    if (moduleState[m].protocol == PROTOCOL_CHANNELS_CROSSFIRE) {
      modIdx = m;
      break;
    }
  }
  if (modIdx == 0xFF) return false;

  // Frame: [MODULE_ADDR | length(2+len) | cmd | payload... | CRC]
  outputTelemetryBuffer.pushByte(0xEE);          // MODULE_ADDRESS
  outputTelemetryBuffer.pushByte(2 + len);        // length
  outputTelemetryBuffer.pushByte(cmd);
  for (uint8_t i = 0; i < len; i++)
    outputTelemetryBuffer.pushByte(payload[i]);
  outputTelemetryBuffer.pushByte(
      crc8(outputTelemetryBuffer.data + 2, 1 + len));

  uint8_t ep = (modIdx == INTERNAL_MODULE) ? 0 : TELEMETRY_ENDPOINT_SPORT;
  outputTelemetryBuffer.setDestination(ep);
  return true;
}

// Async CRSF write payload — heap-copied so it survives menu destruction
struct FpvCrsfWriteCtx {
  uint8_t cmd;
  uint8_t payload[8];
  uint8_t len;
};

static void fpvCrsfPushAsyncCb(lv_timer_t* t)
{
  auto* ctx = (FpvCrsfWriteCtx*)t->user_data;
  if (ctx) {
    fpvCrsfPush(ctx->cmd, ctx->payload, ctx->len);
    delete ctx;
  }
  lv_timer_del(t);
}

static void fpvCrsfPushDeferred(uint8_t cmd, const uint8_t* payload, uint8_t len)
{
  auto* ctx = new FpvCrsfWriteCtx();
  ctx->cmd = cmd;
  ctx->len = len;
  memcpy(ctx->payload, payload, len);
  auto* timer = lv_timer_create(fpvCrsfPushAsyncCb, 50, ctx);
  lv_timer_set_repeat_count(timer, 1);  // one-shot
}

void FpvDashLayout::showElrsPowerPopup()
{
  int elrsIdx = getElrsModuleIdx();
  if (elrsIdx < 0) return;

  if (!hdrElrsLabel) return;
  lv_obj_clear_state(hdrElrsLabel, LV_STATE_PRESSED);

  lv_obj_t* s = BottomDock::getFocusSentinel();
  if (s) lv_group_focus_obj(s);

  // ── Use cached Power field if available; otherwise standard levels ──
  int pwrFieldId = -1;
  int curSel = 0;
  std::vector<std::string> options;

  if (g_elrsCache.valid && !g_elrsCache.fields.empty()) {
    for (auto& f : g_elrsCache.fields) {
      if (f.type != ElrsParamBrowser::FT_SELECT) continue;
      std::string lower = f.name;
      for (auto& c : lower) c = (char)tolower((unsigned char)c);
      if (lower.find("power") == std::string::npos
          && lower.find("pwr") == std::string::npos) continue;
      pwrFieldId = f.id;
      curSel = f.value;
      // Format raw option values with mW/W units
      for (auto& opt : f.options) {
        uint32_t mw = (uint32_t)atoi(opt.c_str());
        char buf[16];
        if (mw >= 1000)
          snprintf(buf, sizeof(buf), "%.1fW", mw / 1000.0f);
        else
          snprintf(buf, sizeof(buf), "%umW", (unsigned)mw);
        options.push_back(buf);
      }
      break;
    }
  }

  if (options.empty()) {
    // Standard ELRS power levels (10mW–2W)
    static const uint32_t pwr[] = {10, 25, 50, 100, 250, 500, 1000, 2000};
    for (size_t i = 0; i < DIM(pwr); i++) {
      char buf[16];
      if (pwr[i] >= 1000)
        snprintf(buf, sizeof(buf), "%.1fW", pwr[i] / 1000.0f);
      else
        snprintf(buf, sizeof(buf), "%umW", (unsigned)pwr[i]);
      options.push_back(buf);
    }
  }

  auto* menu = new Menu();
  menu->setTitle(pwrFieldId > 0 ? "TX Power" : "RF Power");

  for (size_t i = 0; i < options.size(); i++) {
    menu->addLine(options[i], [=]() {
      uint8_t val = (uint8_t)i;
      int fid = (pwrFieldId > 0) ? pwrFieldId : 0;
      uint8_t payload[4] = {0xEE, 0xEF, (uint8_t)fid, val};
      fpvCrsfPushDeferred(0x2D, payload, 4);
      // Update cache immediately so FPV header & param browser show new value
      if (g_elrsCache.valid && fid >= 1 &&
          (size_t)fid <= g_elrsCache.fields.size()) {
        g_elrsCache.fields[fid - 1].value = val;
      }
      // Force FPV header redraw on next checkEvents tick
      lastElrsRateStr[0] = '\0';
      lastElrsTxPower = UINT32_MAX;
    }, [=]() { return (int)i == curSel; });
  }
}

// Factory registration
static const LayoutOption fpvDashOptions[] = {
    LAYOUT_OPTIONS_END
};

static uint8_t fpvDashZmap[] = {
    LAYOUT_MAP_0, LAYOUT_MAP_0, LAYOUT_MAP_FULL, LAYOUT_MAP_FULL,
};

BaseLayoutFactory<FpvDashLayout> layoutFpvDash(
    "LayoutFpvDash", "FPV Dashboard",
    fpvDashOptions, 0, fpvDashZmap);
