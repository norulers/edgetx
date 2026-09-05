/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 */

#include "model_wizard.h"
#include "button.h"
#include "etx_lv_theme.h"
#include "filechoice.h"
#include "file_preview.h"
#include "input_mapping.h"
#include "layout.h"
#include "menus.h"
#include "mixes.h"
#include "modal_window.h"
#include "pagegroup.h"
#include "choice.h"
#include "dialog.h"
#include "numberedit.h"
#include "static.h"
#include "toggleswitch.h"

#include <memory>
#include <vector>

// ─── Construction ──────────────────────────────────────────────────────────

static void styleModelMenuObject(lv_obj_t* obj)
{
  lv_obj_set_style_bg_color(obj, lv_color_make(0x28, 0x28, 0x28), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_text_color(obj, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
  lv_obj_set_style_outline_width(obj, 0, LV_PART_MAIN | LV_STATE_FOCUS_KEY);
  lv_obj_set_style_bg_color(obj, lv_color_make(0xFF, 0x8C, 0x00), LV_PART_MAIN | LV_STATE_FOCUSED);
  lv_obj_set_style_text_color(obj, lv_color_black(), LV_PART_MAIN | LV_STATE_FOCUSED);
  lv_obj_set_style_bg_color(obj, lv_color_make(0x00, 0xA0, 0x00), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_text_color(obj, lv_color_black(), LV_PART_MAIN | LV_STATE_PRESSED);
}

static void styleModelMenuHeader(Window* header)
{
  lv_obj_t* hdr = header->getLvObj();
  uint32_t cnt = lv_obj_get_child_cnt(hdr);
  for (uint32_t i = 0; i < cnt; i++) {
    auto child = lv_obj_get_child(hdr, i);
    if (lv_obj_check_type(child, &lv_canvas_class)) {
      lv_obj_add_flag(child, LV_OBJ_FLAG_HIDDEN);
      uint32_t ccnt = lv_obj_get_child_cnt(child);
      for (uint32_t j = 0; j < ccnt; j++) {
        lv_obj_add_flag(lv_obj_get_child(child, j), LV_OBJ_FLAG_HIDDEN);
      }
    }
  }

  auto leftBg = new StaticIcon(header, 0, 0, ICON_TOPLEFT_BG, COLOR_THEME_PRIMARY2_INDEX);
  lv_obj_set_style_img_recolor_opa(leftBg->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_img_recolor(leftBg->getLvObj(), lv_color_make(0x22, 0x22, 0x22), LV_PART_MAIN);
  leftBg->setTop((EdgeTxStyles::MENU_HEADER_HEIGHT - leftBg->height()) / 2);

  auto leftIco = new StaticIcon(leftBg, 0, 0, ICON_MODEL_SELECT, COLOR_THEME_PRIMARY2_INDEX);
  lv_obj_set_style_img_recolor_opa(leftIco->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_img_recolor(leftIco->getLvObj(), lv_color_make(0xFF, 0x8C, 0x00), LV_PART_MAIN);
  leftIco->center(leftBg->width() + PAD_MEDIUM, leftBg->height());

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

ModelWizard::ModelWizard(WizardType type) :
  Page(ICON_EDGETX, PAD_ZERO),
    wizardType(type)
{
  // Channel-order-aware defaults — matches Lua defaultChannel() calls
  // STICK_RUD=0, STICK_ELE=1, STICK_THR=2, STICK_AIL=3
  // Use inputMappingChannelOrder to map stick → input → channel
  auto chForStick = [](uint8_t stick) -> int {
    for (uint8_t i = 0; i < MAX_STICKS; i++) {
      if (inputMappingChannelOrder(i) == stick) return i;
    }
    return stick;
  };
  wizardData.motorChannel = chForStick(2);   // Thr
  wizardData.ailChA = chForStick(3);          // Ail
  wizardData.ailChB = chForStick(3) + 1;     // Ail+1
  wizardData.tailChA = chForStick(1);        // Ele
  wizardData.tailChB = chForStick(0);        // Rud
  wizardData.heliThrCh = chForStick(2);      // Thr
  wizardData.heliAilCh = chForStick(3);      // Ail
  wizardData.heliNickCh = chForStick(1);     // Ele
  wizardData.heliRudCh = chForStick(0);      // Rud
  wizardData.multiThrCh = chForStick(2);     // Thr
  wizardData.multiRollCh = chForStick(3);    // Ail
  wizardData.multiPitchCh = chForStick(1);   // Ele
  wizardData.multiYawCh = chForStick(0);     // Rud

  styleModelMenuHeader(header);
  header->setTitle(STR_MAIN_MENU_MANAGE_MODELS);

  body->setFlexLayout(LV_FLEX_FLOW_ROW, 0);
  body->setSize(LCD_W, LCD_H - EdgeTxStyles::MENU_HEADER_HEIGHT);
  lv_obj_set_style_pad_row(body->getLvObj(), 0, LV_PART_MAIN);
  lv_obj_set_style_pad_column(body->getLvObj(), 0, LV_PART_MAIN);
  lv_obj_set_style_bg_color(body->getLvObj(), lv_color_make(0x18, 0x18, 0x18), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(body->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);

  settingsArea = new Window(body, rect_t{});
  settingsArea->setFlexLayout(LV_FLEX_FLOW_COLUMN, PAD_SMALL);
  settingsArea->setSize(LCD_W * 60 / 100, LCD_H - EdgeTxStyles::MENU_HEADER_HEIGHT);
  settingsArea->padAll(PAD_TINY);
  settingsArea->padRight(PAD_SMALL);
  lv_obj_set_style_bg_color(settingsArea->getLvObj(), lv_color_make(0x18, 0x18, 0x18), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(settingsArea->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);

  imageArea = new Window(body, rect_t{});
  imageArea->setFlexLayout(LV_FLEX_FLOW_COLUMN, 0);
  imageArea->setSize(LCD_W * 40 / 100, LCD_H - EdgeTxStyles::MENU_HEADER_HEIGHT);
  imageArea->padAll(PAD_SMALL);
  lv_obj_set_style_bg_opa(imageArea->getLvObj(), LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_scrollbar_mode(imageArea->getLvObj(), LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_flex_align(imageArea->getLvObj(), LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  // Start with first page
  currentPage = 0;
  buildPage();
  createNavigationButtons();
  updateNavigationButtons();
}

void ModelWizard::onCancel()
{
  new ConfirmDialog(STR_WIZARD_EXIT_TITLE, STR_WIZARD_EXIT_MSG,
    [=]() {
      // Rebuild screens even when wizard is cancelled early —
      // createModel() deleted them and nothing else recreates them.
      rebuildMainView();
      deleteLater();
    });
}

void ModelWizard::buildPage()
{
  settingsArea->clear();
  imageArea->clear();

  const char* subtitle = "";
  void (ModelWizard::*builder)() = nullptr;

  switch (wizardType) {
    case WIZARD_TYPE_PLANE:
    case WIZARD_TYPE_GLIDER: {
      static const WizardPage motorPg = {STR_WIZARD_MOTOR, nullptr, &ModelWizard::buildMotorPage};
      static const WizardPage ailPg   = {STR_WIZARD_AIL, nullptr, &ModelWizard::buildAileronPage};
      static const WizardPage flapPg  = {STR_WIZARD_FLAPS, nullptr, &ModelWizard::buildFlapsPage};
      static const WizardPage tailPg  = {STR_WIZARD_TAIL, nullptr, &ModelWizard::buildTailPage};
      static const WizardPage gearPg  = {STR_WIZARD_GEAR, nullptr, &ModelWizard::buildGearPage};
      static const WizardPage addPg   = {STR_WIZARD_MORE, nullptr, &ModelWizard::buildAdditionalPage};
      static const WizardPage sumPg   = {STR_WIZARD_SUMMARY, nullptr, &ModelWizard::buildSummaryPage};
      static const WizardPage finPg   = {STR_WIZARD_FINISHED, nullptr, &ModelWizard::buildFinishedPage};
      static const WizardPage* fwPages[] = {&motorPg, &ailPg, &flapPg, &tailPg, &gearPg, &addPg, &sumPg, &finPg};
      int count = 8;
      if (currentPage < count) {
        subtitle = fwPages[currentPage]->title;
        builder = fwPages[currentPage]->buildFunc;
      }
      totalPages = count;
      break;
    }
    case WIZARD_TYPE_WING: {
      static const WizardPage motorPg = {STR_WIZARD_MOTOR, nullptr, &ModelWizard::buildMotorPage};
      static const WizardPage ailPg   = {STR_WIZARD_WING_AIL, nullptr, &ModelWizard::buildAileronPage};
      static const WizardPage addPg   = {STR_WIZARD_MORE, nullptr, &ModelWizard::buildAdditionalPage};
      static const WizardPage sumPg   = {STR_WIZARD_SUMMARY, nullptr, &ModelWizard::buildSummaryPage};
      static const WizardPage finPg   = {STR_WIZARD_FINISHED, nullptr, &ModelWizard::buildFinishedPage};
      static const WizardPage* wingPages[] = {&motorPg, &ailPg, &addPg, &sumPg, &finPg};
      int count = 5;
      if (currentPage < count) {
        subtitle = wingPages[currentPage]->title;
        builder = wingPages[currentPage]->buildFunc;
      }
      totalPages = count;
      break;
    }
    case WIZARD_TYPE_HELI: {
      static const WizardPage h0 = {STR_WIZARD_HELI_TYPE, nullptr, &ModelWizard::buildHeliTypePage};
      static const WizardPage h1 = {STR_WIZARD_HELI_STYLE, nullptr, &ModelWizard::buildHeliStylePage};
      static const WizardPage h2 = {STR_WIZARD_HELI_SWITCHES, nullptr, &ModelWizard::buildHeliSwitchPage};
      static const WizardPage h3 = {STR_WIZARD_HELI_THR, nullptr, &ModelWizard::buildHeliThrPage};
      static const WizardPage h4 = {STR_WIZARD_HELI_CURVES, nullptr, &ModelWizard::buildHeliCurvePage};
      static const WizardPage h5 = {STR_WIZARD_HELI_ROLL, nullptr, &ModelWizard::buildHeliAilerPage};
      static const WizardPage h6 = {STR_WIZARD_HELI_NICK, nullptr, &ModelWizard::buildHeliNickPage};
      static const WizardPage h7 = {STR_WIZARD_HELI_RUDD, nullptr, &ModelWizard::buildHeliRudPage};
      static const WizardPage h8 = {STR_WIZARD_SUMMARY, nullptr, &ModelWizard::buildSummaryPage};
      static const WizardPage h9 = {STR_WIZARD_FINISHED, nullptr, &ModelWizard::buildFinishedPage};
      static const WizardPage* heliPages[] = {&h0, &h1, &h2, &h3, &h4, &h5, &h6, &h7, &h8, &h9};
      int count = 10;
      if (currentPage < count) {
        subtitle = heliPages[currentPage]->title;
        builder = heliPages[currentPage]->buildFunc;
      }
      totalPages = count;
      break;
    }
    case WIZARD_TYPE_MULTIROTOR: {
      static const WizardPage m0 = {STR_WIZARD_MULTI_THR, nullptr, &ModelWizard::buildMultiThrottlePage};
      static const WizardPage m1 = {STR_WIZARD_MULTI_ROLL, nullptr, &ModelWizard::buildMultiRollPage};
      static const WizardPage m2 = {STR_WIZARD_MULTI_PITCH, nullptr, &ModelWizard::buildMultiPitchPage};
      static const WizardPage m3 = {STR_WIZARD_MULTI_YAW, nullptr, &ModelWizard::buildMultiYawPage};
      static const WizardPage m4 = {STR_WIZARD_MULTI_ARM, nullptr, &ModelWizard::buildMultiArmPage};
      static const WizardPage m5 = {STR_WIZARD_MULTI_BEEPER, nullptr, &ModelWizard::buildMultiBeeperPage};
      static const WizardPage m6 = {STR_WIZARD_MULTI_MODE, nullptr, &ModelWizard::buildMultiModePage};
      static const WizardPage m7 = {STR_WIZARD_SUMMARY, nullptr, &ModelWizard::buildSummaryPage};
      static const WizardPage m8 = {STR_WIZARD_FINISHED, nullptr, &ModelWizard::buildFinishedPage};
      static const WizardPage* multiPages[] = {&m0, &m1, &m2, &m3, &m4, &m5, &m6, &m7, &m8};
      int count = 9;
      if (currentPage < count) {
        subtitle = multiPages[currentPage]->title;
        builder = multiPages[currentPage]->buildFunc;
      }
      totalPages = count;
      break;
    }
  }

  header->setTitle2(subtitle);

  if (builder)
    (this->*builder)();

  updateNavigationButtons();
}

void ModelWizard::createNavigationButtons()
{
#if defined(HARDWARE_TOUCH)
  auto styleNavigationButton = [](IconButton* button) {
    auto obj = button->getLvObj();
    lv_obj_set_style_bg_color(obj, lv_color_make(0x28, 0x28, 0x28), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, lv_color_make(0x66, 0x66, 0x66), LV_PART_MAIN);
    lv_obj_set_style_bg_color(obj, lv_color_make(0xFF, 0x8C, 0x00),
                              LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(obj, 3, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(obj, lv_color_white(),
                                  LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(obj, lv_color_make(0x00, 0xA0, 0x00),
                              LV_PART_MAIN | LV_STATE_PRESSED);
  };

  prevBtn = new IconButton(this, ICON_BTN_PREV,
                          LCD_W - PageGroup::PAGE_GROUP_BACK_BTN_XO * 3,
                          PAD_MEDIUM, [=]() {
                            prevPage();
                            return 0;
                          });
  styleNavigationButton(prevBtn);
  nextBtn = new IconButton(this, ICON_BTN_NEXT,
                          LCD_W - PageGroup::PAGE_GROUP_BACK_BTN_XO * 2,
                          PAD_MEDIUM, [=]() {
                            if (currentPage < totalPages - 1) {
                              // Apply config when leaving Summary to Finished
                              if (currentPage == totalPages - 2)
                                applyModelConfig();
                              nextPage();
                            } else {
                              deleteLater();
                            }
                            return 0;
                          });
  styleNavigationButton(nextBtn);
#endif
}

void ModelWizard::updateNavigationButtons()
{
#if defined(HARDWARE_TOUCH)
  // Hide prev/next on Finished page (last page)
  bool isFinished = (currentPage == totalPages - 1);
  if (prevBtn) prevBtn->show(currentPage > 0 && !isFinished);
  if (nextBtn) nextBtn->show(totalPages > 0 && !isFinished);
#endif
}

void ModelWizard::nextPage()
{
  if (currentPage < totalPages - 1) {
    currentPage++;
    buildPage();
  }
}

void ModelWizard::prevPage()
{
  if (currentPage > 0) {
    currentPage--;
    buildPage();
  }
}

// ─── Helper: create settings rows matching Lua wizard-ui.lua ─────────────

static Window* makeSettingRow(Window* parent, const char* label)
{
  auto row = new Window(parent, rect_t{});
  row->setFlexLayout(LV_FLEX_FLOW_ROW, 0);
  row->setWidth(lv_pct(100));
  row->setHeight(EdgeTxStyles::UI_ELEMENT_HEIGHT);
  lv_obj_set_style_bg_color(row->getLvObj(), lv_color_make(0x28, 0x28, 0x28), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(row->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(row->getLvObj(), 0, LV_PART_MAIN);

  auto labelBox = new Window(row, rect_t{});
  labelBox->setWidth(lv_pct(60));
  labelBox->setHeight(EdgeTxStyles::UI_ELEMENT_HEIGHT);

  auto lbl = new StaticText(labelBox, rect_t{PAD_SMALL, PAD_TINY, lv_pct(100), EdgeTxStyles::STD_FONT_HEIGHT}, label);
  lv_obj_set_style_text_color(lbl->getLvObj(), lv_color_white(), LV_PART_MAIN);

  auto controlBox = new Window(row, rect_t{});
  controlBox->setFlexLayout(LV_FLEX_FLOW_ROW, PAD_TINY);
  controlBox->setWidth(lv_pct(40));
  controlBox->setHeight(EdgeTxStyles::UI_ELEMENT_HEIGHT);
  lv_obj_set_flex_align(controlBox->getLvObj(), LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  return controlBox;
}

static Window* makeRow(Window* parent, const char* label,
                       std::function<void(Choice*, std::shared_ptr<int>)> build)
{
  auto controlBox = makeSettingRow(parent, label);
  auto value = std::make_shared<int>(0);
  auto ch = new Choice(controlBox, rect_t{0, 0, LCD_W * 60 / 100 * 40 / 100 - PAD_SMALL, 0}, 0, 0,
                       [=]() { return *value; }, [=](int v) { *value = v; });
  styleModelMenuObject(ch->getLvObj());
  build(ch, value);
  return controlBox;
}

static Window* makeToggleRow(Window* parent, const char* label,
                             std::function<uint8_t()> getValue,
                             std::function<void(uint8_t)> setValue)
{
  auto controlBox = makeSettingRow(parent, label);
  auto toggle = new ToggleSwitch(controlBox, rect_t{}, std::move(getValue), std::move(setValue));
  lv_obj_set_style_outline_width(toggle->getLvObj(), 0, LV_PART_MAIN | LV_STATE_FOCUS_KEY);
  return controlBox;
}

// ─── Switch popup helpers (from timer_setup.cpp) ──────────────────────────

static constexpr coord_t POPUP_W_SMALL = LCD_W * 34 / 100;
static constexpr coord_t POPUP_MAX_H = LCD_H * 80 / 100;

struct PopupWindow : ModalWindow {
  using ModalWindow::ModalWindow;
  void onCancel() override { deleteLater(); }
};

static PopupWindow* createPopup(bool closeOnClickOutside)
{
  return new PopupWindow(closeOnClickOutside);
}

static std::pair<Window*, Window*> createPopupForm(PopupWindow* dlg,
                                                   const char* title,
                                                   coord_t width)
{
  auto form = new Window(dlg, rect_t{});
  form->padAll(PAD_ZERO);
  form->setFlexLayout(LV_FLEX_FLOW_COLUMN, PAD_ZERO, width, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(form->getLvObj(), lv_color_make(0x18, 0x18, 0x18), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(form->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_outline_width(form->getLvObj(), 1, LV_PART_MAIN);
  lv_obj_set_style_outline_color(form->getLvObj(), lv_color_make(0x44, 0x44, 0x44), LV_PART_MAIN);
  lv_obj_set_style_pad_all(form->getLvObj(), 0, LV_PART_MAIN);
  lv_obj_set_style_max_height(form->getLvObj(), POPUP_MAX_H, LV_PART_MAIN);
  lv_obj_center(form->getLvObj());

  lv_obj_add_event_cb(form->getLvObj(), [](lv_event_t* e) {
    auto* d = (PopupWindow*)lv_event_get_user_data(e);
    d->deleteLater();
  }, LV_EVENT_CANCEL, dlg);

  auto hdr = new StaticText(form, {0, 0, LV_PCT(100), 0}, title, COLOR_THEME_QM_FG_INDEX);
  lv_obj_set_style_bg_color(hdr->getLvObj(), lv_color_make(0x22, 0x22, 0x22), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(hdr->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
  hdr->padAll(PAD_SMALL);

  auto list = new Window(form, rect_t{});
  list->padAll(PAD_ZERO);
  list->setFlexLayout(LV_FLEX_FLOW_COLUMN, PAD_TINY, width, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(list->getLvObj(), PAD_SMALL, LV_PART_MAIN);
  return {form, list};
}

// ─── Switch popup with auto-detect (matches timer_setup.cpp) ──────────────

static void openSwitchPopup(std::function<void(int)> onSelect)
{
  auto dlg = createPopup(true);

  std::vector<std::string> items;
  std::vector<int> srcs;
  items.push_back("---");
  srcs.push_back(0);  // SWSRC_NONE equivalent as index
  for (int i = 0; i < switchGetMaxSwitches(); i++) {
    char s[8];
    getSwitchName(s, i);
    items.push_back(s);
    srcs.push_back(i);  // store switch index (0-7)
  }

  auto [form, list] = createPopupForm(dlg, STR_SWITCH, POPUP_W_SMALL);
  lv_obj_align(form->getLvObj(), LV_ALIGN_RIGHT_MID, -PAD_LARGE, 0);

  std::vector<lv_obj_t*> buttons;
  std::vector<int> buttonSrcs;

  for (size_t i = 0; i < items.size(); i++) {
    auto btn = new TextButton(list,
        {0, 0, LV_PCT(100), EdgeTxStyles::UI_ELEMENT_HEIGHT + PAD_SMALL},
        items[i], [=]() -> uint8_t {
          int sel = srcs[i];
          dlg->deleteLater();
          // Defer onSelect so buildPage() doesn't run inside button handler
          lv_timer_create([](lv_timer_t* t) {
            auto* fn = (std::function<void()>*)t->user_data;
            if (*fn) (*fn)();
            delete fn;
            lv_timer_del(t);
          }, 0, new std::function<void()>([=]() { if (onSelect) onSelect(sel); }));
          return 0;
        });
    styleModelMenuObject(btn->getLvObj());
    buttons.push_back(btn->getLvObj());
    buttonSrcs.push_back(srcs[i]);
  }

  struct SwitchCtx {
    std::vector<lv_obj_t*> buttons;
    std::vector<int> srcs;
    lv_timer_t* timer;
    swsrc_t lastSwtch = 0;
  };
  auto ctx = new SwitchCtx{buttons, srcs, nullptr, 0};
  lv_obj_set_user_data(form->getLvObj(), ctx);

  ctx->timer = lv_timer_create(
      [](lv_timer_t* t) {
        auto form = (Window*)t->user_data;
        auto ctx = (SwitchCtx*)lv_obj_get_user_data(form->getLvObj());
        if (!ctx) return;
        swsrc_t swtch = getMovedSwitch();
        if (swtch == 0 || swtch == ctx->lastSwtch) return;
        ctx->lastSwtch = swtch;
        int swIdx = (swtch - SWSRC_FIRST_SWITCH) / 3;
        for (size_t i = 0; i < ctx->srcs.size(); i++) {
          if (ctx->srcs[i] == swIdx) {
            lv_group_focus_obj(ctx->buttons[i]);
            lv_obj_scroll_to_view(ctx->buttons[i], LV_ANIM_OFF);
            return;
          }
        }
      },
      100, form);

  lv_obj_add_event_cb(
      form->getLvObj(),
      [](lv_event_t* e) {
        auto ctx = (SwitchCtx*)lv_obj_get_user_data(e->target);
        if (ctx) {
          if (ctx->timer) lv_timer_del(ctx->timer);
          delete ctx;
        }
      },
      LV_EVENT_DELETE, nullptr);
}

// ─── Switch row — button that opens auto-detect popup ─────────────────────

static Window* makeSwitchRow(Window* parent, const char* label,
                             std::function<int()> getValue,
                             std::function<void(int)> setValue,
                             std::function<void()> onChanged = nullptr)
{
  auto controlBox = makeSettingRow(parent, label);
  auto val = getValue();
  char name[8];
  getSwitchName(name, val);
  auto btn = new TextButton(controlBox,
      {0, 0, LCD_W * 60 / 100 * 40 / 100 - PAD_SMALL, 0}, name,
      [=]() -> uint8_t {
        openSwitchPopup([=](int sw) {
          setValue(sw);
          if (onChanged) onChanged();
        });
        return 0;
      });
  styleModelMenuObject(btn->getLvObj());
  return controlBox;
}

// ─── Fixed-wing pages ────────────────────────────────────────────────────

void ModelWizard::buildMotorPage()
{
  makeToggleRow(settingsArea, STR_WIZARD_MOTOR_LABEL,
    [=]() { return wizardData.hasMotor ? 1 : 0; },
    [=](uint8_t v) {
      wizardData.hasMotor = v != 0;
      buildPage();
    });

  if (wizardData.hasMotor) {
    makeRow(settingsArea, STR_WIZARD_MT_CH, [=](Choice* ch, std::shared_ptr<int> value) {
      ch->setValues({"CH1","CH2","CH3","CH4","CH5","CH6","CH7","CH8","CH9","CH10"});
      ch->setValue(wizardData.motorChannel);
      ch->setSetValueHandler([=](int v) { *value = v; wizardData.motorChannel = v; });
    });

    makeToggleRow(settingsArea, STR_WIZARD_SAFETY_SW,
      [=]() { return wizardData.hasArmSwitch ? 1 : 0; },
      [=](uint8_t v) {
        wizardData.hasArmSwitch = v != 0;
        buildPage();
      });

    if (wizardData.hasArmSwitch) {
      makeSwitchRow(settingsArea, STR_SWITCH,
        [=]() { return wizardData.armSwitch; },
        [=](int sw) { wizardData.armSwitch = sw; },
        [=]() { buildPage(); });
    }
  }

  showImage("prop.png");
}

void ModelWizard::buildAileronPage()
{
  if (wizardType == WIZARD_TYPE_WING) {
    makeRow(settingsArea, STR_WIZARD_ELEVON_A, [=](Choice* ch, std::shared_ptr<int> value) {
      ch->setValues({"CH1","CH2","CH3","CH4","CH5","CH6","CH7","CH8","CH9","CH10"});
      ch->setValue(wizardData.ailChA);
      ch->setSetValueHandler([=](int v) { *value = v; wizardData.ailChA = v; });
    });
    makeRow(settingsArea, STR_WIZARD_ELEVON_B, [=](Choice* ch, std::shared_ptr<int> value) {
      ch->setValues({"CH1","CH2","CH3","CH4","CH5","CH6","CH7","CH8","CH9","CH10"});
      ch->setValue(wizardData.ailChB);
      ch->setSetValueHandler([=](int v) { *value = v; wizardData.ailChB = v; });
    });
    showImage("plane-2a.png"); // Wing always has 2 elevons
  } else {
    makeRow(settingsArea, STR_WIZARD_AIL_COUNT, [=](Choice* ch, std::shared_ptr<int> value) {
      ch->setValues({STR_WIZARD_AIL_NONE, STR_WIZARD_AIL_ONE, STR_WIZARD_AIL_TWO});
      ch->setValue(wizardData.ailCount);
      ch->setSetValueHandler([=](int v) {
        *value = v;
        wizardData.ailCount = v;
        // Dynamic image update (matches Lua visibleFunc)
        imageArea->clear();
        if (v == 2) showImage("plane-2a.png");
        else if (v == 1) showImage("plane-1a.png");
        else showImage("plane.png");
      });
    });

    if (wizardData.ailCount >= 1) {
      makeRow(settingsArea, STR_WIZARD_AIL_RIGHT, [=](Choice* ch, std::shared_ptr<int> value) {
        ch->setValues({"CH1","CH2","CH3","CH4","CH5","CH6","CH7","CH8","CH9","CH10"});
        ch->setValue(wizardData.ailChA);
        ch->setSetValueHandler([=](int v) { *value = v; wizardData.ailChA = v; });
      });
    }
    if (wizardData.ailCount == 2) {
      makeRow(settingsArea, STR_WIZARD_AIL_LEFT, [=](Choice* ch, std::shared_ptr<int> value) {
        ch->setValues({"CH1","CH2","CH3","CH4","CH5","CH6","CH7","CH8","CH9","CH10"});
        ch->setValue(wizardData.ailChB);
        ch->setSetValueHandler([=](int v) { *value = v; wizardData.ailChB = v; });
      });
    }
    // Match Lua: plane-2a.png / plane-1a.png / plane.png
    if (wizardData.ailCount == 2)
      showImage("plane-2a.png");
    else if (wizardData.ailCount == 1)
      showImage("plane-1a.png");
    else
      showImage("plane.png");
  }
}

void ModelWizard::buildFlapsPage()
{
  if (wizardType == WIZARD_TYPE_WING) return;

  makeRow(settingsArea, STR_WIZARD_FLAPS_ASK, [=](Choice* ch, std::shared_ptr<int> value) {
    ch->setValues({STR_WIZARD_FLAP_NO, STR_WIZARD_FLAP_ONE, STR_WIZARD_FLAP_TWO});
    ch->setValue(wizardData.flapCount);
    ch->setSetValueHandler([=](int v) {
      *value = v;
      wizardData.flapCount = v;
      // Dynamic image update
      imageArea->clear();
      if (v == 2) showImage("plane-2f.png");
      else if (v == 1) showImage("plane-1f.png");
      else showImage("plane.png");
    });
  });

  if (wizardData.flapCount >= 1) {
    makeRow(settingsArea, STR_WIZARD_AIL_RIGHT, [=](Choice* ch, std::shared_ptr<int> value) {
      ch->setValues({"CH1","CH2","CH3","CH4","CH5","CH6","CH7","CH8","CH9","CH10"});
      ch->setValue(wizardData.flapChA);
      ch->setSetValueHandler([=](int v) { *value = v; wizardData.flapChA = v; });
    });
  }
  if (wizardData.flapCount == 2) {
    makeRow(settingsArea, STR_WIZARD_AIL_LEFT, [=](Choice* ch, std::shared_ptr<int> value) {
      ch->setValues({"CH1","CH2","CH3","CH4","CH5","CH6","CH7","CH8","CH9","CH10"});
      ch->setValue(wizardData.flapChB);
      ch->setSetValueHandler([=](int v) { *value = v; wizardData.flapChB = v; });
    });
  }

  // Match Lua: plane-2f.png / plane-1f.png / plane.png
  if (wizardData.flapCount == 2)
    showImage("plane-2f.png");
  else if (wizardData.flapCount == 1)
    showImage("plane-1f.png");
  else
    showImage("plane.png");
}

void ModelWizard::buildTailPage()
{
  if (wizardType == WIZARD_TYPE_WING) return;

  makeRow(settingsArea, STR_WIZARD_TAIL_CONFIG, [=](Choice* ch, std::shared_ptr<int> value) {
    ch->setValues({STR_WIZARD_TAIL_T0, STR_WIZARD_TAIL_T1,
                   STR_WIZARD_TAIL_T2, STR_WIZARD_TAIL_T3});
    ch->setValue(wizardData.tailType);
    ch->setSetValueHandler([=](int v) {
      *value = v;
      wizardData.tailType = v;
      // Dynamic image update
      imageArea->clear();
      char tname[32];
      snprintf(tname, sizeof(tname), "tail-%d.png", v + 1);
      showImage(tname);
    });
  });

  // Adjust tail defaults to avoid aileron channels
  bool used[10] = {false};
  if (wizardType == WIZARD_TYPE_WING) {
    used[wizardData.ailChA] = true;
    used[wizardData.ailChB] = true;
  } else if (wizardData.ailCount >= 1) {
    used[wizardData.ailChA] = true;
    if (wizardData.ailCount == 2) used[wizardData.ailChB] = true;
  }
  if (wizardData.hasMotor) used[wizardData.motorChannel] = true;
  if (used[wizardData.tailChA]) {
    for (int i = 0; i < 10; i++) if (!used[i]) { wizardData.tailChA = i; used[i] = true; break; }
  }
  if (used[wizardData.tailChB]) {
    for (int i = 0; i < 10; i++) if (!used[i]) { wizardData.tailChB = i; used[i] = true; break; }
  }

  makeRow(settingsArea, STR_WIZARD_TAIL_CH_A, [=](Choice* ch, std::shared_ptr<int> value) {
    ch->setValues({"CH1","CH2","CH3","CH4","CH5","CH6","CH7","CH8","CH9","CH10"});
    ch->setValue(wizardData.tailChA);
    ch->setSetValueHandler([=](int v) { *value = v; wizardData.tailChA = v; });
  });

  if (wizardData.tailType >= 1) {
    makeRow(settingsArea, STR_WIZARD_TAIL_CH_B, [=](Choice* ch, std::shared_ptr<int> value) {
      ch->setValues({"CH1","CH2","CH3","CH4","CH5","CH6","CH7","CH8","CH9","CH10"});
      ch->setValue(wizardData.tailChB);
      ch->setSetValueHandler([=](int v) { *value = v; wizardData.tailChB = v; });
    });
  }
  if (wizardData.tailType == 2) {
    makeRow(settingsArea, STR_WIZARD_TAIL_CH_C, [=](Choice* ch, std::shared_ptr<int> value) {
      ch->setValues({"CH1","CH2","CH3","CH4","CH5","CH6","CH7","CH8","CH9","CH10"});
      ch->setValue(wizardData.tailChC);
      ch->setSetValueHandler([=](int v) { *value = v; wizardData.tailChC = v; });
    });
  }

  // Match Lua: tail-1/2/3/4.png based on tailType
  char tname[32];
  snprintf(tname, sizeof(tname), "tail-%d.png", wizardData.tailType + 1);
  showImage(tname);
}

void ModelWizard::buildGearPage()
{
  if (wizardType == WIZARD_TYPE_WING) return;

  makeRow(settingsArea, STR_WIZARD_GEAR_ASK, [=](Choice* ch, std::shared_ptr<int> value) {
    ch->setValues({STR_WIZARD_NO, STR_WIZARD_YES});
    ch->setValue(wizardData.hasGear ? 1 : 0);
    ch->setSetValueHandler([=](int v) {
      *value = v;
      wizardData.hasGear = (v == 1);
      buildPage();
    });
  });

  if (wizardData.hasGear) {
    makeSwitchRow(settingsArea, STR_WIZARD_GEAR_SW_LABEL,
      [=]() { return wizardData.gearSwitch; },
      [=](int sw) { wizardData.gearSwitch = sw; },
      [=]() { buildPage(); });
    makeRow(settingsArea, STR_WIZARD_GEAR_CH_LABEL, [=](Choice* ch, std::shared_ptr<int> value) {
      ch->setValues({"CH1","CH2","CH3","CH4","CH5","CH6","CH7","CH8","CH9","CH10"});
      ch->setValue(wizardData.gearChannel);
      ch->setSetValueHandler([=](int v) { *value = v; wizardData.gearChannel = v; });
    });
  }

  showImage("plane.png");
}

void ModelWizard::buildAdditionalPage()
{
  auto expoCtrl = makeSettingRow(settingsArea, STR_WIZARD_EXPO_LABEL);
  auto expoEdit = new NumberEdit(expoCtrl, rect_t{0, 0, LCD_W * 60 / 100 * 40 / 100 - PAD_SMALL, 0}, 0, 100,
                                 [=]() { return wizardData.expoPercent; },
                                 [=](int v) { wizardData.expoPercent = v; });
  styleModelMenuObject(expoEdit->getLvObj());

  makeToggleRow(settingsArea, STR_WIZARD_DUAL_RATE_LABEL,
    [=]() { return wizardData.dualRate ? 1 : 0; },
    [=](uint8_t v) {
      wizardData.dualRate = v != 0;
      buildPage();
    });

  if (wizardData.dualRate) {
    makeSwitchRow(settingsArea, STR_SWITCH,
      [=]() { return wizardData.drSwitch; },
      [=](int sw) { wizardData.drSwitch = sw; },
      [=]() { buildPage(); });
  }
}

// ─── Heli pages ───────────────────────────────────────────────────────────

void ModelWizard::buildHeliTypePage()
{
  makeRow(settingsArea, STR_WIZARD_HELI_TYPE_ASK, [=](Choice* ch, std::shared_ptr<int> value) {
    ch->setValues({STR_WIZARD_HELI_FBL, STR_WIZARD_HELI_FB});
    ch->setValue(wizardData.heliType);
    ch->setSetValueHandler([=](int v) {
      *value = v;
      wizardData.heliType = v;
      buildPage();
    });
  });

  if (wizardData.heliType == 1) {
    makeRow(settingsArea, STR_WIZARD_SWASH_ASK, [=](Choice* ch, std::shared_ptr<int> value) {
      ch->setValues({STR_WIZARD_SWASH_120, STR_WIZARD_SWASH_120X, STR_WIZARD_SWASH_140, STR_WIZARD_SWASH_90});
      ch->setValue(wizardData.swashType);
      ch->setSetValueHandler([=](int v) { *value = v; wizardData.swashType = v; });
    });
  }

  showImage("type.png");
}

void ModelWizard::buildHeliStylePage()
{
  makeRow(settingsArea, STR_WIZARD_STYLE_ASK, [=](Choice* ch, std::shared_ptr<int> value) {
    ch->setValues({STR_WIZARD_STYLE_SPORT, STR_WIZARD_STYLE_3D_LIGHT, STR_WIZARD_STYLE_3D_FULL});
    ch->setValue(wizardData.flyingStyle);
    ch->setSetValueHandler([=](int v) { *value = v; wizardData.flyingStyle = v; });
  });
  showImage("style.png");
}

void ModelWizard::buildHeliSwitchPage()
{
  makeSwitchRow(settingsArea, STR_WIZARD_FM_LABEL,
    [=]() { return wizardData.fmSwitch; },
    [=](int sw) { wizardData.fmSwitch = sw; },
    [=]() { buildPage(); });
  makeSwitchRow(settingsArea, STR_WIZARD_TH_HOLD_LABEL,
    [=]() { return wizardData.throttleHoldSwitch; },
    [=](int sw) { wizardData.throttleHoldSwitch = sw; },
    [=]() { buildPage(); });
  if (wizardData.heliType == 1) {
    makeSwitchRow(settingsArea, STR_WIZARD_TAIL_GAIN_LABEL,
      [=]() { return wizardData.tailGainSwitch; },
      [=](int sw) { wizardData.tailGainSwitch = sw; },
      [=]() { buildPage(); });
  }
  showImage("switch.png");
}

void ModelWizard::buildHeliThrPage()
{
  makeRow(settingsArea, STR_WIZARD_THR_CH_LABEL, [=](Choice* ch, std::shared_ptr<int> value) {
    ch->setValues({"CH1","CH2","CH3","CH4","CH5","CH6","CH7","CH8"});
    ch->setValue(wizardData.heliThrCh);
    ch->setSetValueHandler([=](int v) { *value = v; wizardData.heliThrCh = v; });
  });
  showImage("throttle.png");
}

void ModelWizard::buildHeliCurvePage()
{
  makeRow(settingsArea, STR_WIZARD_CURVE_FM0_LABEL, [=](Choice* ch, std::shared_ptr<int> value) {
    ch->setValues({STR_WIZARD_CURVE_THRUP, STR_WIZARD_CURVE_V, STR_WIZARD_CURVE_FLAT});
    ch->setValue(wizardData.thrCurveFM0);
    ch->setSetValueHandler([=](int v) { *value = v; wizardData.thrCurveFM0 = v; });
  });
  makeRow(settingsArea, STR_WIZARD_CURVE_FM1_LABEL, [=](Choice* ch, std::shared_ptr<int> value) {
    ch->setValues({STR_WIZARD_CURVE_V, STR_WIZARD_CURVE_FLAT});
    ch->setValue(wizardData.thrCurveFM1);
    ch->setSetValueHandler([=](int v) { *value = v; wizardData.thrCurveFM1 = v; });
  });
  makeRow(settingsArea, STR_WIZARD_CURVE_FM2_LABEL, [=](Choice* ch, std::shared_ptr<int> value) {
    ch->setValues({STR_WIZARD_CURVE_V, STR_WIZARD_CURVE_FLAT});
    ch->setValue(wizardData.thrCurveFM2);
    ch->setSetValueHandler([=](int v) { *value = v; wizardData.thrCurveFM2 = v; });
  });
  showImage("curve.png");
}

void ModelWizard::buildHeliAilerPage()
{
  makeRow(settingsArea, STR_WIZARD_ROLL_CH_LABEL, [=](Choice* ch, std::shared_ptr<int> value) {
    ch->setValues({"CH1","CH2","CH3","CH4","CH5","CH6","CH7","CH8"});
    ch->setValue(wizardData.heliAilCh);
    ch->setSetValueHandler([=](int v) { *value = v; wizardData.heliAilCh = v; });
  });
  showImage("roll.png");
}

void ModelWizard::buildHeliNickPage()
{
  makeRow(settingsArea, STR_WIZARD_NICK_CH, [=](Choice* ch, std::shared_ptr<int> value) {
    ch->setValues({"CH1","CH2","CH3","CH4","CH5","CH6","CH7","CH8"});
    ch->setValue(wizardData.heliNickCh);
    ch->setSetValueHandler([=](int v) { *value = v; wizardData.heliNickCh = v; });
  });
  showImage("nick.png");
}

void ModelWizard::buildHeliRudPage()
{
  makeRow(settingsArea, STR_WIZARD_TAIL_RUD_CH, [=](Choice* ch, std::shared_ptr<int> value) {
    ch->setValues({"CH1","CH2","CH3","CH4","CH5","CH6","CH7","CH8"});
    ch->setValue(wizardData.heliRudCh);
    ch->setSetValueHandler([=](int v) { *value = v; wizardData.heliRudCh = v; });
  });
  showImage("tail.png");
}

// ─── Multirotor pages ────────────────────────────────────────────────────

void ModelWizard::buildMultiThrottlePage()
{
  makeRow(settingsArea, STR_WIZARD_ASSIGN_THR, [=](Choice* ch, std::shared_ptr<int> value) {
    ch->setValues({"CH1","CH2","CH3","CH4","CH5","CH6","CH7","CH8"});
    ch->setValue(wizardData.multiThrCh);
    ch->setSetValueHandler([=](int v) { *value = v; wizardData.multiThrCh = v; });
  });
  showImage("throttle.png");
}

void ModelWizard::buildMultiRollPage()
{
  makeRow(settingsArea, STR_WIZARD_ASSIGN_ROLL, [=](Choice* ch, std::shared_ptr<int> value) {
    ch->setValues({"CH1","CH2","CH3","CH4","CH5","CH6","CH7","CH8"});
    ch->setValue(wizardData.multiRollCh);
    ch->setSetValueHandler([=](int v) { *value = v; wizardData.multiRollCh = v; });
  });
  showImage("roll.png");
}

void ModelWizard::buildMultiPitchPage()
{
  makeRow(settingsArea, STR_WIZARD_ASSIGN_PITCH, [=](Choice* ch, std::shared_ptr<int> value) {
    ch->setValues({"CH1","CH2","CH3","CH4","CH5","CH6","CH7","CH8"});
    ch->setValue(wizardData.multiPitchCh);
    ch->setSetValueHandler([=](int v) { *value = v; wizardData.multiPitchCh = v; });
  });
  showImage("pitch.png");
}

void ModelWizard::buildMultiYawPage()
{
  makeRow(settingsArea, STR_WIZARD_ASSIGN_YAW, [=](Choice* ch, std::shared_ptr<int> value) {
    ch->setValues({"CH1","CH2","CH3","CH4","CH5","CH6","CH7","CH8"});
    ch->setValue(wizardData.multiYawCh);
    ch->setSetValueHandler([=](int v) { *value = v; wizardData.multiYawCh = v; });
  });
  showImage("yaw.png");
}

void ModelWizard::buildMultiArmPage()
{
  makeSwitchRow(settingsArea, STR_WIZARD_ASSIGN_ARM,
    [=]() { return wizardData.multiArmSwitch; },
    [=](int sw) { wizardData.multiArmSwitch = sw; },
    [=]() { buildPage(); });
  showImage("arm.png");
}

void ModelWizard::buildMultiBeeperPage()
{
  makeSwitchRow(settingsArea, STR_WIZARD_ASSIGN_BEEPER,
    [=]() { return wizardData.multiBeeperSwitch; },
    [=](int sw) { wizardData.multiBeeperSwitch = sw; },
    [=]() { buildPage(); });
  showImage("beeper.png");
}

void ModelWizard::buildMultiModePage()
{
  makeSwitchRow(settingsArea, STR_WIZARD_ASSIGN_MODE,
    [=]() { return wizardData.multiModeSwitch; },
    [=](int sw) { wizardData.multiModeSwitch = sw; },
    [=]() { buildPage(); });
  showImage("mode.png");
}

// ─── Summary & Finish pages ───────────────────────────────────────────────

static StaticText* makeSummaryRow(Window* parent, const char* label, const char* value)
{
  auto controlBox = makeSettingRow(parent, label);
  auto val = new StaticText(controlBox, rect_t{0, PAD_TINY, lv_pct(100), EdgeTxStyles::STD_FONT_HEIGHT}, value);
  return val;
}

void ModelWizard::buildSummaryPage()
{
  imageArea->clear();

  // Switch count is board dependent, so always resolve names via the HAL
  char swName[8];
  auto swStr = [&swName](int sw) -> const char* {
    getSwitchName(swName, sw);
    return swName;
  };

  // Review text on RIGHT side (matches Lua: children2)
  auto reviewLbl = lv_label_create(imageArea->getLvObj());
  lv_label_set_text(reviewLbl, STR_WIZARD_REVIEW);
  lv_obj_set_style_text_color(reviewLbl, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_width(reviewLbl, lv_pct(100));

  auto nextLbl = lv_label_create(imageArea->getLvObj());
  lv_label_set_text(nextLbl, STR_WIZARD_REVIEW_NEXT);
  lv_obj_set_style_text_color(nextLbl, lv_color_make(0x66, 0x66, 0x66), LV_PART_MAIN);
  lv_obj_set_width(nextLbl, lv_pct(100));

  // Per-type summary rows
  switch (wizardType) {
    case WIZARD_TYPE_PLANE:
    case WIZARD_TYPE_GLIDER:
    case WIZARD_TYPE_WING: {
      if (wizardData.hasMotor)
        makeSummaryRow(settingsArea, STR_WIZARD_MT_CH, channelNames[wizardData.motorChannel]);
      if (wizardType == WIZARD_TYPE_WING) {
        makeSummaryRow(settingsArea, STR_WIZARD_ELEVON_A, channelNames[wizardData.ailChA]);
        makeSummaryRow(settingsArea, STR_WIZARD_ELEVON_B, channelNames[wizardData.ailChB]);
      } else {
        if (wizardData.ailCount >= 1)
          makeSummaryRow(settingsArea, STR_WIZARD_AIL_R, channelNames[wizardData.ailChA]);
        if (wizardData.ailCount == 2)
          makeSummaryRow(settingsArea, STR_WIZARD_AIL_L, channelNames[wizardData.ailChB]);
        if (wizardData.flapCount >= 1)
          makeSummaryRow(settingsArea, STR_WIZARD_FLAP_R, channelNames[wizardData.flapChA]);
        if (wizardData.flapCount == 2)
          makeSummaryRow(settingsArea, STR_WIZARD_FLAP_L, channelNames[wizardData.flapChB]);
        if (wizardData.tailType == 0)
          makeSummaryRow(settingsArea, STR_WIZARD_ELEV_CH, channelNames[wizardData.tailChA]);
        else if (wizardData.tailType == 1) {
          makeSummaryRow(settingsArea, STR_WIZARD_ELEV_CH, channelNames[wizardData.tailChA]);
          makeSummaryRow(settingsArea, STR_WIZARD_RUDD_CH, channelNames[wizardData.tailChB]);
        } else if (wizardData.tailType == 2) {
          makeSummaryRow(settingsArea, STR_WIZARD_ELEV_R, channelNames[wizardData.tailChA]);
          makeSummaryRow(settingsArea, STR_WIZARD_RUDD_CH, channelNames[wizardData.tailChB]);
          makeSummaryRow(settingsArea, STR_WIZARD_ELEV_L, channelNames[wizardData.tailChC]);
        } else if (wizardData.tailType == 3) {
          makeSummaryRow(settingsArea, STR_WIZARD_VTAIL_R, channelNames[wizardData.tailChA]);
          makeSummaryRow(settingsArea, STR_WIZARD_VTAIL_L, channelNames[wizardData.tailChB]);
        }
        if (wizardData.hasGear) {
          makeSummaryRow(settingsArea, STR_WIZARD_GEAR_SW_LABEL, swStr(wizardData.gearSwitch));
          makeSummaryRow(settingsArea, STR_WIZARD_GEAR_CH_LABEL, channelNames[wizardData.gearChannel]);
        }
      }
      char expoStr[16];
      snprintf(expoStr, sizeof(expoStr), "%d%%", wizardData.expoPercent);
      makeSummaryRow(settingsArea, STR_WIZARD_EXPO_LABEL, expoStr);
      makeSummaryRow(settingsArea, STR_WIZARD_DUAL_RATE_LABEL, wizardData.dualRate ? STR_WIZARD_YES : STR_WIZARD_NO);
      if (wizardData.hasMotor && wizardData.hasArmSwitch)
        makeSummaryRow(settingsArea, STR_WIZARD_SAFETY_SW, swStr(wizardData.armSwitch));
      break;
    }
    case WIZARD_TYPE_HELI: {
      makeSummaryRow(settingsArea, STR_WIZARD_HELI_TYPE, wizardData.heliType == 0 ? STR_WIZARD_HELI_FBL : STR_WIZARD_HELI_FB);
      if (wizardData.heliType == 1) {
        const char* swashNames[] = {STR_WIZARD_SWASH_120, STR_WIZARD_SWASH_120X, STR_WIZARD_SWASH_140, STR_WIZARD_SWASH_90};
        makeSummaryRow(settingsArea, STR_WIZARD_HELI_SWASH, swashNames[wizardData.swashType]);
      }
      const char* styleNames[] = {STR_WIZARD_STYLE_SPORT, STR_WIZARD_STYLE_3D_LIGHT, STR_WIZARD_STYLE_3D_FULL};
      makeSummaryRow(settingsArea, STR_WIZARD_HELI_STYLE, styleNames[wizardData.flyingStyle]);
      makeSummaryRow(settingsArea, STR_WIZARD_FM_LABEL, swStr(wizardData.fmSwitch));
      makeSummaryRow(settingsArea, STR_WIZARD_TH_HOLD_LABEL, swStr(wizardData.throttleHoldSwitch));
      if (wizardData.heliType == 1)
        makeSummaryRow(settingsArea, STR_WIZARD_TAIL_GAIN_LABEL, swStr(wizardData.tailGainSwitch));
      makeSummaryRow(settingsArea, STR_WIZARD_THR_CH_LABEL, channelNames[wizardData.heliThrCh]);
      makeSummaryRow(settingsArea, STR_WIZARD_ROLL_CH_LABEL, channelNames[wizardData.heliAilCh]);
      makeSummaryRow(settingsArea, STR_WIZARD_NICK_CH, channelNames[wizardData.heliNickCh]);
      makeSummaryRow(settingsArea, STR_WIZARD_TAIL_RUD_CH, channelNames[wizardData.heliRudCh]);
      break;
    }
    case WIZARD_TYPE_MULTIROTOR: {
      makeSummaryRow(settingsArea, STR_WIZARD_ASSIGN_THR, channelNames[wizardData.multiThrCh]);
      makeSummaryRow(settingsArea, STR_WIZARD_ASSIGN_ROLL, channelNames[wizardData.multiRollCh]);
      makeSummaryRow(settingsArea, STR_WIZARD_ASSIGN_PITCH, channelNames[wizardData.multiPitchCh]);
      makeSummaryRow(settingsArea, STR_WIZARD_ASSIGN_YAW, channelNames[wizardData.multiYawCh]);
      makeSummaryRow(settingsArea, STR_WIZARD_ASSIGN_ARM, swStr(wizardData.multiArmSwitch));
      makeSummaryRow(settingsArea, STR_WIZARD_ASSIGN_BEEPER, swStr(wizardData.multiBeeperSwitch));
      makeSummaryRow(settingsArea, STR_WIZARD_ASSIGN_MODE, swStr(wizardData.multiModeSwitch));
      break;
    }
  }
}

void ModelWizard::buildFinishedPage()
{
  settingsArea->clear();
  imageArea->clear();

  auto createdLbl = lv_label_create(settingsArea->getLvObj());
  lv_label_set_text(createdLbl, STR_WIZARD_CREATED);
  lv_obj_set_style_text_color(createdLbl, lv_color_white(), LV_PART_MAIN);

  auto exitLbl = lv_label_create(settingsArea->getLvObj());
  lv_label_set_text(exitLbl, STR_WIZARD_HOLD_EXIT);
  lv_obj_set_style_text_color(exitLbl, lv_color_make(0x66, 0x66, 0x66), LV_PART_MAIN);

  // Model image — show bitmap if set, otherwise file picker with preview
  static constexpr lv_coord_t IMG_W = LCD_W * 40 / 100 - PAD_LARGE * 4;
  static constexpr lv_coord_t IMG_H = (LCD_H - EdgeTxStyles::MENU_HEADER_HEIGHT) - PAD_LARGE * 4;

  char bmp[LEN_BITMAP_NAME + 1];
  strAppend(bmp, g_model.header.bitmap, LEN_BITMAP_NAME);

  // Spacer: invisible first child — FileChoice cannot be first flex child
  auto* spacer = new Window(imageArea, {0, 0, 0, 0});
  lv_obj_add_flag(spacer->getLvObj(), LV_OBJ_FLAG_HIDDEN);

  // File picker (second child)
  auto preview = std::make_shared<FilePreview*>(nullptr);
  auto imageChoice = new FileChoice(imageArea,
      {0, 0, IMG_W, 0},
      BITMAPS_PATH, BITMAPS_EXT, LEN_BITMAP_NAME,
      []() { return std::string(g_model.header.bitmap, LEN_BITMAP_NAME); },
      [preview](std::string newValue) {
        strncpy(g_model.header.bitmap, newValue.c_str(), LEN_BITMAP_NAME);
        storageDirty(EE_MODEL);
        auto image = *preview;
        if (image && !image->deleted()) {
          watchdogSuspend(200);
          if (newValue.empty())
            image->setFile(nullptr);
          else {
            std::string path = std::string(BITMAPS_PATH) + PATH_SEPARATOR + newValue;
            image->setFile(path.c_str());
          }
        }
      },
        false, STR_WIZARD_SET_MODEL_IMAGE);
      imageChoice->enablePreview();

  // Preview image (third child)
  *preview = new FilePreview(imageArea, {0, 0, IMG_W, IMG_H - EdgeTxStyles::UI_ELEMENT_HEIGHT - PAD_SMALL});
  if (bmp[0]) {
    char path[128];
    snprintf(path, sizeof(path), "%s/%s", BITMAPS_PATH, bmp);
    (*preview)->setFile(path);
  }
}

void ModelWizard::showImage(const char* filename)
{
  const char* folder = "plane";
  switch (wizardType) {
    case WIZARD_TYPE_GLIDER:    folder = "glider"; break;
    case WIZARD_TYPE_WING:      folder = "wing"; break;
    case WIZARD_TYPE_HELI:      folder = "helicopter"; break;
    case WIZARD_TYPE_MULTIROTOR: folder = "multirotor"; break;
    default: break;
  }
  char path[128];
  snprintf(path, sizeof(path), "/TEMPLATES/1.Wizard/img/%s/%s", folder, filename);

  // Match Lua wizard image sizing exactly (wizard-ui.lua:261-280)
  // Lua: w = LCD_W*40/100 - PAD_LARGE*4, h = PAGE_BODY_HEIGHT - PAD_LARGE*4
  // Use fixed values (not layout-dependent) to guarantee match
  static constexpr lv_coord_t IMG_W = LCD_W * 40 / 100 - PAD_LARGE * 4;
  static constexpr lv_coord_t IMG_H = (LCD_H - EdgeTxStyles::MENU_HEADER_HEIGHT) - PAD_LARGE * 4;

  // Use StaticImage (same as Lua wizard) - SD card + auto zoom to fit
  auto img = new StaticImage(imageArea, {0, 0, IMG_W, IMG_H}, path);

  if (!img->hasImage()) {
    img->deleteLater();
    auto placeholder = lv_label_create(imageArea->getLvObj());
    lv_label_set_text(placeholder, STR_WIZARD_NO_IMAGE);
    lv_obj_set_style_text_color(placeholder, lv_color_make(0x66, 0x66, 0x66), LV_PART_MAIN);
    lv_obj_center(placeholder);
  }
}

// ─── Apply ────────────────────────────────────────────────────────────────

// Helper: set mix name
static void setMixName(uint8_t idx, const char* name)
{
  auto mix = mixAddress(idx);
  if (mix) strncpy(mix->name, name, sizeof(mix->name) - 1);
}

static int defaultInputForStick(uint8_t stick)
{
  for (uint8_t input = 0; input < MAX_STICKS; input++) {
    if (inputMappingChannelOrder(input) == stick) return input;
  }
  return stick;
}

static int16_t inputSourceForStick(uint8_t stick)
{
  return MIXSRC_FIRST_INPUT + defaultInputForStick(stick);
}

static int16_t switchSource(uint8_t sw)
{
  return MIXSRC_FIRST_SWITCH + sw;
}

// Remove every input line of a given input channel.
// Matches the Lua wizard's model.deleteInput(channel, 0) for the wing type,
// which drops the unused Rudder input so the model stays clean.
static void deleteInputLines(int chn)
{
  for (int i = 0; i < MAX_EXPOS; i++) {
    ExpoData* expo = expoAddress(i);
    if (expo->srcRaw && expo->chn == chn) {
      deleteExpo(i);
      i--;  // re-check the slot that shifted into place
    }
  }
}

// Add a mix line matching Lua model-config.lua addMix()
static void addWizardMix(uint8_t channel, int16_t source, const char* name,
                         int weight = 100, uint8_t mltpx = MLTPX_ADD)
{
  uint8_t idx = getMixCount();
  insertMix(idx, channel);
  auto mix = mixAddress(idx);
  if (!mix) return;

  mix->srcRaw = source;
  // weight is a SourceNumVal bitfield — a raw negative value would set isSource
  mix->weight = makeSourceNumVal(weight);
  mix->mltpx = mltpx;
  setMixName(idx, name);
}

// Find an unused custom function slot
static int findFreeCustomFn()
{
  for (int i = 0; i < MAX_SPECIAL_FUNCTIONS; i++) {
    if (!CFN_SWITCH(&g_model.customFn[i])) return i;
  }
  return 0; // fallback to slot 0
}

// Set up arm switch override — matches Lua wizard SF arm switch
static void setupArmSwitch(int armSwitch, int motorChannel)
{
  // CFN switch uses SWSRC range: switchIndex*3 + pos (0=up, 1=mid, 2=down)
  int swIdx = SWSRC_FIRST_SWITCH + armSwitch * 3 + 2; // CHAR_DOWN = position 2
  if (swIdx < 0 || swIdx > SWSRC_LAST_SWITCH) return;

  int slot = findFreeCustomFn();
  auto cfn = &g_model.customFn[slot];
  memclear(cfn, sizeof(CustomFunctionData));
  CFN_SWITCH(cfn) = swIdx;
  CFN_FUNC(cfn) = FUNC_OVERRIDE_CHANNEL;
  cfn->all.val = -100;
  cfn->all.mode = 0;
  cfn->all.param = motorChannel;
  CFN_ACTIVE(cfn) = 1;
  storageDirty(EE_MODEL);
}

void ModelWizard::applyModelConfig()
{
  // Delete all existing mixes
  while (getMixCount() > 0) {
    deleteMix(0);
  }

  switch (wizardType) {
    case WIZARD_TYPE_PLANE:
    case WIZARD_TYPE_GLIDER:
    case WIZARD_TYPE_WING:
      applyFixedWing();
      break;
    case WIZARD_TYPE_HELI:
      applyHelicopter();
      break;
    case WIZARD_TYPE_MULTIROTOR:
      applyMultirotor();
      break;
  }

  // Sort mixes by destCh — the mixer page requires ascending channel order.
  // A stable (adjacent-only) sort keeps the relative order of mixes on the
  // same channel, which matters for elevons: the first mix on a channel is
  // the REPL base, later ones use their multiplex flag. Reordering them would
  // make one of the two control surfaces override the other.
  uint8_t n = getMixCount();
  for (uint8_t i = 0; i < n; i++) {
    for (uint8_t j = 0; j + 1 < n - i; j++) {
      auto a = mixAddress(j);
      auto b = mixAddress(j + 1);
      if (a->destCh > b->destCh) {
        MixData tmp;
        memcpy(&tmp, a, sizeof(MixData));
        memcpy(a, b, sizeof(MixData));
        memcpy(b, &tmp, sizeof(MixData));
      }
    }
  }

  storageDirty(EE_MODEL);
  storageFlushCurrentModel();

  rebuildMainView();
}

void ModelWizard::rebuildMainView()
{
  // createModel() → applyDefaultTemplate() → deleteCustomScreens()
  // deleted all screens; recreate the default (FPV Dashboard preferred).
  g_model.view = 0;
  LayoutFactory::deleteCustomScreens();
  LayoutFactory::loadDefaultLayout();

  for (int i = 0; i < 20; i++)
    lv_obj_enable_style_refresh(true);
}

void ModelWizard::applyFixedWing()
{
  static constexpr uint8_t STICK_RUD = 0;
  static constexpr uint8_t STICK_ELE = 1;
  static constexpr uint8_t STICK_THR = 2;
  static constexpr uint8_t STICK_AIL = 3;

  // ── Input lines setup (matches Lua createModel) ──
  // Set expo on stick input lines; add dual rate lines if enabled
  int expoVal = wizardData.expoPercent;
  bool hasDualRate = wizardData.dualRate;
  int drSw = wizardData.drSwitch;

  // Helper: find first expo line index for a given input channel
  auto getFirstInput = [](int chn) -> int {
    for (int i = 0; i < MAX_EXPOS; i++) {
      ExpoData* expo = expoAddress(i);
      if (!expo->srcRaw || expo->chn >= chn) return i;
    }
    return 0;
  };

  // Helper: configure one input line
  auto setupInputLine = [](int idx, int chn, int expo, int weight, int16_t swtch = 0) {
    ExpoData* ed = expoAddress(idx);
    ed->curve.type = CURVE_REF_EXPO;
    ed->curve.value = expo;
    ed->weight = weight;
    ed->mode = 3;                         // both sides active
    ed->chn = chn;
    if (swtch) ed->swtch = swtch;
  };

  int ailCh = defaultInputForStick(STICK_AIL);
  int eleCh = defaultInputForStick(STICK_ELE);
  int rudCh = defaultInputForStick(STICK_RUD);

  // A wing has no rudder; remove the unused input line (matches the Lua
  // wizard's model.deleteInput(defaultChannel(STICK_NUMBER_RUD), 0)).
  if (wizardType == WIZARD_TYPE_WING && rudCh >= 0)
    deleteInputLines(rudCh);

  if (hasDualRate) {
    // Aileron: 3 lines (UP=100%, MID=75%, DOWN=50%)
    int ailFirst = getFirstInput(ailCh);
    setupInputLine(ailFirst, ailCh, expoVal, 100, SWSRC_FIRST_SWITCH + drSw * 3);      // UP
    insertExpo(ailFirst + 1, ailCh);
    setupInputLine(ailFirst + 1, ailCh, expoVal, 75, SWSRC_FIRST_SWITCH + drSw * 3 + 1); // MID
    insertExpo(ailFirst + 2, ailCh);
    setupInputLine(ailFirst + 2, ailCh, expoVal, 50, SWSRC_FIRST_SWITCH + drSw * 3 + 2); // DOWN

    // Elevator: 3 lines
    int eleFirst = getFirstInput(eleCh);
    setupInputLine(eleFirst, eleCh, expoVal, 100, SWSRC_FIRST_SWITCH + drSw * 3);
    insertExpo(eleFirst + 1, eleCh);
    setupInputLine(eleFirst + 1, eleCh, expoVal, 75, SWSRC_FIRST_SWITCH + drSw * 3 + 1);
    insertExpo(eleFirst + 2, eleCh);
    setupInputLine(eleFirst + 2, eleCh, expoVal, 50, SWSRC_FIRST_SWITCH + drSw * 3 + 2);

    // Rudder: just expo (no dual rate — matches Lua)
    if (wizardType != WIZARD_TYPE_WING) {
      int rudFirst = getFirstInput(rudCh);
      setupInputLine(rudFirst, rudCh, expoVal, 100);
    }
  } else {
    // No dual rate: set expo on all stick input lines
    int ailFirst = getFirstInput(ailCh);
    setupInputLine(ailFirst, ailCh, expoVal, 100);

    int eleFirst = getFirstInput(eleCh);
    setupInputLine(eleFirst, eleCh, expoVal, 100);

    if (wizardType != WIZARD_TYPE_WING) {
      int rudFirst = getFirstInput(rudCh);
      setupInputLine(rudFirst, rudCh, expoVal, 100);
    }
  }

  storageDirty(EE_MODEL);

  // Motor
  if (wizardData.hasMotor) {
    addWizardMix(wizardData.motorChannel, inputSourceForStick(STICK_THR), "Motor");
  }

  // Ailerons
  if (wizardType != WIZARD_TYPE_WING) {
    if (wizardData.ailCount >= 1) {
      addWizardMix(wizardData.ailChA, inputSourceForStick(STICK_AIL),
                   wizardData.ailCount == 1 ? "Ail" : "Ail-R");
      if (wizardData.ailCount == 2) {
        addWizardMix(wizardData.ailChB, inputSourceForStick(STICK_AIL), "Ail-L", -100);
      }
    }
  } else {
    // Wing delta mix
    addWizardMix(wizardData.ailChA, inputSourceForStick(STICK_ELE), "ele-R", 50);
    addWizardMix(wizardData.ailChA, inputSourceForStick(STICK_AIL), "ail-R", -50);
    addWizardMix(wizardData.ailChB, inputSourceForStick(STICK_ELE), "ele-L", 50);
    addWizardMix(wizardData.ailChB, inputSourceForStick(STICK_AIL), "ail-L", 50);
  }

  // Flaps
  if (wizardType != WIZARD_TYPE_WING) {
    if (wizardData.flapCount >= 1) {
      addWizardMix(wizardData.flapChA, MIXSRC_FIRST_SWITCH,
                   wizardData.flapCount == 1 ? "Flaps" : "FlapsR");
      if (wizardData.flapCount == 2) {
        addWizardMix(wizardData.flapChB, MIXSRC_FIRST_SWITCH, "FlapsL");
      }
    }
  }

  // Tail
  if (wizardType != WIZARD_TYPE_WING) {
    if (wizardData.tailType == 0) {
      addWizardMix(wizardData.tailChA, inputSourceForStick(STICK_ELE), "Elev");
    } else if (wizardData.tailType == 1) {
      addWizardMix(wizardData.tailChA, inputSourceForStick(STICK_ELE), "Elev");
      addWizardMix(wizardData.tailChB, inputSourceForStick(STICK_RUD), "Rudder");
    } else if (wizardData.tailType == 2) {
      addWizardMix(wizardData.tailChA, inputSourceForStick(STICK_ELE), "Elev-R");
      addWizardMix(wizardData.tailChB, inputSourceForStick(STICK_RUD), "Rudder");
      addWizardMix(wizardData.tailChC, inputSourceForStick(STICK_ELE), "Elev-L");
    } else if (wizardData.tailType == 3) {
      addWizardMix(wizardData.tailChA, inputSourceForStick(STICK_ELE), "V-EleR", 50);
      addWizardMix(wizardData.tailChA, inputSourceForStick(STICK_RUD), "V-RudR", 50);
      addWizardMix(wizardData.tailChB, inputSourceForStick(STICK_ELE), "V-EleL", 50);
      addWizardMix(wizardData.tailChB, inputSourceForStick(STICK_RUD), "V-RudL", -50);
    }
  }

  // Gear
  if (wizardType != WIZARD_TYPE_WING && wizardData.hasGear) {
    addWizardMix(wizardData.gearChannel, switchSource(wizardData.gearSwitch), "Gear");
  }

  // Arm switch special function — matches Lua: FUNC_OVERRIDE_CHANNEL
  if (wizardData.hasMotor && wizardData.hasArmSwitch) {
    setupArmSwitch(wizardData.armSwitch, wizardData.motorChannel);
  }
}

void ModelWizard::applyHelicopter()
{
  static constexpr uint8_t STICK_RUD = 0;
  static constexpr uint8_t STICK_ELE = 1;
  static constexpr uint8_t STICK_THR = 2;
  static constexpr uint8_t STICK_AIL = 3;

  int thrCh = wizardData.heliThrCh;
  int ailCh = wizardData.heliAilCh;
  int eleCh = wizardData.heliNickCh;
  int rudCh = wizardData.heliRudCh;
  int fmSw = wizardData.fmSwitch;
  int holdSw = wizardData.throttleHoldSwitch;
  int gyroSw = wizardData.tailGainSwitch;

  // Switch source helpers: SA↑=SWSRC_FIRST_SWITCH+0, SB↑=SWSRC_FIRST_SWITCH+3, etc.
  auto swUp = [](int sw) { return SWSRC_FIRST_SWITCH + sw * 3; };
  auto swMid = [](int sw) { return SWSRC_FIRST_SWITCH + sw * 3 + 1; };
  auto swDown = [](int sw) { return SWSRC_FIRST_SWITCH + sw * 3 + 2; };

  // ── Throttle Curves (TC0/TC1/TC2/THD) ──
  auto setStdCurve = [](int idx, const char* name,
                        std::initializer_list<int8_t> yVals) {
    if (idx >= MAX_CURVES) return;
    auto& ch = g_model.curves[idx];

    // Calculate memory shift needed
    int oldMemSize = (ch.type == CURVE_TYPE_STANDARD) ? (5 + ch.points) : (8 + 2 * ch.points);
    int newPoints = (int8_t)yVals.size();
    int newMemSize = 5 + newPoints;
    int shift = newMemSize - oldMemSize;

    // Move curve memory to make room
    moveCurve(idx, shift);

    // Write header
    ch.type = CURVE_TYPE_STANDARD;
    ch.smooth = true;
    ch.points = newPoints - 5;
    strncpy(ch.name, name, sizeof(ch.name) - 1);

    // Write Y points
    int8_t* pts = curveAddress(idx);
    for (auto v : yVals) {
      *pts++ = v;
    }
  };

  int style = wizardData.flyingStyle;  // 0=Sport, 1=Light3D, 2=Full3D
  int curve0 = wizardData.thrCurveFM0; // 0=ThrUp, 1=V, 2=Flat
  int curve1 = wizardData.thrCurveFM1;
  int curve2 = wizardData.thrCurveFM2;

  // FM0 (TC0)
  if (curve0 == 0) { // Thr Up
    if (style == 0)      setStdCurve(0, "TC0", {-100, 0, 20, 40, 40});
    else if (style == 1) setStdCurve(0, "TC0", {-100, 0, 35, 50, 50});
    else                 setStdCurve(0, "TC0", {-100, 0, 40, 80, 80});
  } else if (curve0 == 1) { // V Curve
    if (style == 0)      setStdCurve(0, "TC0", {50, 40, 50});
    else if (style == 1) setStdCurve(0, "TC0", {65, 55, 65});
    else                 setStdCurve(0, "TC0", {70, 60, 70});
  } else { // Flat
    if (style == 0)      setStdCurve(0, "TC0", {60, 60, 60});
    else if (style == 1) setStdCurve(0, "TC0", {65, 65, 65});
    else                 setStdCurve(0, "TC0", {70, 70, 70});
  }

  // FM1 (TC1)
  if (curve1 == 0) { // V Curve
    if (style == 0)      setStdCurve(1, "TC1", {60, 50, 60});
    else if (style == 1) setStdCurve(1, "TC1", {70, 60, 70});
    else                 setStdCurve(1, "TC1", {85, 75, 85});
  } else { // Flat
    if (style == 0)      setStdCurve(1, "TC1", {65, 65, 65});
    else if (style == 1) setStdCurve(1, "TC1", {70, 70, 70});
    else                 setStdCurve(1, "TC1", {85, 85, 85});
  }

  // FM2 (TC2)
  if (curve2 == 0) { // V Curve
    if (style == 0)      setStdCurve(2, "TC2", {70, 60, 70});
    else if (style == 1) setStdCurve(2, "TC2", {85, 70, 85});
    else                 setStdCurve(2, "TC2", {100, 90, 100});
  } else { // Flat
    if (style == 0)      setStdCurve(2, "TC2", {75, 75, 75});
    else if (style == 1) setStdCurve(2, "TC2", {85, 85, 85});
    else                 setStdCurve(2, "TC2", {95, 95, 95});
  }

  // TH Hold (TC3 = index 3)
  setStdCurve(3, "THD", {-100, -100, -100});

  // ── Throttle Mixes ──
  // CURVE_REF_CUSTOM values are 1-based: 1=TC0, 2=TC1, 3=TC2, 4=THD
  // Th0: always active, TC0
  addWizardMix(thrCh, inputSourceForStick(STICK_THR), "Th0");
  { auto m = mixAddress(getMixCount() - 1); if (m) { m->curve.type = CURVE_REF_CUSTOM; m->curve.value = makeSourceNumVal(1); } }

  // Th1: replaces when FM switch is MID, TC1
  addWizardMix(thrCh, inputSourceForStick(STICK_THR), "Th1", 100, MLTPX_REPL);
  { auto m = mixAddress(getMixCount() - 1); if (m) { m->curve.type = CURVE_REF_CUSTOM; m->curve.value = makeSourceNumVal(2); m->swtch = swMid(fmSw); } }

  // Th2: replaces when FM switch is UP, TC2
  addWizardMix(thrCh, inputSourceForStick(STICK_THR), "Th2", 100, MLTPX_REPL);
  { auto m = mixAddress(getMixCount() - 1); if (m) { m->curve.type = CURVE_REF_CUSTOM; m->curve.value = makeSourceNumVal(3); m->swtch = swUp(fmSw); } }

  // Hld: replaces when Hold switch is DOWN, offset -15, THD
  addWizardMix(thrCh, inputSourceForStick(STICK_THR), "Hld", 100, MLTPX_REPL);
  { auto m = mixAddress(getMixCount() - 1); if (m) { m->curve.type = CURVE_REF_CUSTOM; m->curve.value = makeSourceNumVal(4); m->swtch = swDown(holdSw); m->offset = makeSourceNumVal(-15); } }

  // Set throttle output name
  if (thrCh < MAX_OUTPUT_CHANNELS) {
    auto lim = limitAddress(thrCh);
    if (lim) { strncpy(lim->name, "Throt", sizeof(lim->name)); lim->name[sizeof(lim->name)-1] = '\0'; }
  }

  // ── Aileron ──
  if (wizardData.heliType == 0) { // FBL
    addWizardMix(ailCh, inputSourceForStick(STICK_AIL), "Ail");
  } else { // FB: use swash cyc2 source
    addWizardMix(ailCh, MIXSRC_FIRST_HELI + 1, "Ail");
  }
  if (ailCh < MAX_OUTPUT_CHANNELS) {
    auto lim = limitAddress(ailCh);
    if (lim) { strncpy(lim->name, "Ailer", sizeof(lim->name)); lim->name[sizeof(lim->name)-1] = '\0'; }
  }

  // ── Elevator ──
  if (wizardData.heliType == 0) { // FBL
    addWizardMix(eleCh, inputSourceForStick(STICK_ELE), "Ele");
  } else { // FB: use swash cyc1 source
    addWizardMix(eleCh, MIXSRC_FIRST_HELI, "Ele");
  }
  if (eleCh < MAX_OUTPUT_CHANNELS) {
    auto lim = limitAddress(eleCh);
    if (lim) { strncpy(lim->name, "Elev", sizeof(lim->name)); lim->name[sizeof(lim->name)-1] = '\0'; }
  }

  // ── Rudder ──
  addWizardMix(rudCh, inputSourceForStick(STICK_RUD), "Rud");
  if (rudCh < MAX_OUTPUT_CHANNELS) {
    auto lim = limitAddress(rudCh);
    if (lim) { strncpy(lim->name, "Rud", sizeof(lim->name)); lim->name[sizeof(lim->name)-1] = '\0'; }
  }

  // ── Gyro / Tail Gain on CH5 ──
  int gyroCh = 4;
  if (wizardData.heliType == 0) { // FBL: Tail Gain
    addWizardMix(gyroCh, MIXSRC_FIRST_POT + 5, "T.Gain", 25);
  } else { // FB: HHold + Rate switching
    addWizardMix(gyroCh, MIXSRC_FIRST_POT + 5, "HHold", 25);
    addWizardMix(gyroCh, MIXSRC_FIRST_POT + 5, "Rate", -25, MLTPX_REPL);
    { auto m = mixAddress(getMixCount() - 1); if (m) m->swtch = swDown(gyroSw); }
  }
  if (gyroCh < MAX_OUTPUT_CHANNELS) {
    auto lim = limitAddress(gyroCh);
    if (lim) { strncpy(lim->name, "T.Gain", sizeof(lim->name)); lim->name[sizeof(lim->name)-1] = '\0'; }
  }

  // ── Pitch on CH6 ──
  int pitchCh = 5;
  if (wizardData.heliType == 0) { // FBL: direct Thr stick
    addWizardMix(pitchCh, inputSourceForStick(STICK_THR), "Pch");
  } else { // FB: use swash cyc3 source
    addWizardMix(pitchCh, MIXSRC_FIRST_HELI + 2, "Pch");
  }
  if (pitchCh < MAX_OUTPUT_CHANNELS) {
    auto lim = limitAddress(pitchCh);
    if (lim) { strncpy(lim->name, "Pitch", sizeof(lim->name)); lim->name[sizeof(lim->name)-1] = '\0'; }
  }

  // ── Swash Ring (FB only) ──
  if (wizardData.heliType == 1) {
    g_model.swashR.type = wizardData.swashType + 1; // 0→1=120, 1→2=120X, 2→3=140, 3→4=90
    g_model.swashR.collectiveSource = MIXSRC_FIRST_STICK + 2; // Thr
    g_model.swashR.aileronSource = MIXSRC_FIRST_STICK + 3;    // Ail
    g_model.swashR.elevatorSource = MIXSRC_FIRST_STICK + 1;   // Ele
    if (wizardData.swashType <= 1) {
      g_model.swashR.collectiveWeight = 60;
      g_model.swashR.aileronWeight = 60;
      g_model.swashR.elevatorWeight = 60;
    } else if (wizardData.swashType == 2) {
      g_model.swashR.collectiveWeight = 40;
      g_model.swashR.aileronWeight = 40;
      g_model.swashR.elevatorWeight = 60;
    } else {
      g_model.swashR.collectiveWeight = 35;
      g_model.swashR.aileronWeight = 35;
      g_model.swashR.elevatorWeight = 60;
    }
  }

  storageDirty(EE_MODEL);
}

void ModelWizard::applyMultirotor()
{
  static constexpr uint8_t STICK_RUD = 0;
  static constexpr uint8_t STICK_ELE = 1;
  static constexpr uint8_t STICK_THR = 2;
  static constexpr uint8_t STICK_AIL = 3;

  addWizardMix(wizardData.multiThrCh, inputSourceForStick(STICK_THR), "Thr");
  addWizardMix(wizardData.multiRollCh, inputSourceForStick(STICK_AIL), "Roll");
  addWizardMix(wizardData.multiPitchCh, inputSourceForStick(STICK_ELE), "Pitch");
  addWizardMix(wizardData.multiYawCh, inputSourceForStick(STICK_RUD), "Yaw");

  // Aux channels: Arm/Beeper/Mode use first unused channels
  // Find next free channels after the 4 main channels
  bool used[8] = {false};
  used[wizardData.multiThrCh] = true;
  used[wizardData.multiRollCh] = true;
  used[wizardData.multiPitchCh] = true;
  used[wizardData.multiYawCh] = true;

  int auxCh[3] = {-1, -1, -1};
  int auxIdx = 0;
  for (int ch = 0; ch < 8 && auxIdx < 3; ch++) {
    if (!used[ch]) auxCh[auxIdx++] = ch;
  }
  if (auxIdx < 3) {
    for (int ch = 0; ch < 8 && auxIdx < 3; ch++) auxCh[auxIdx++] = ch;
  }

  addWizardMix(auxCh[0], switchSource(wizardData.multiArmSwitch), "Arm");
  addWizardMix(auxCh[1], switchSource(wizardData.multiBeeperSwitch), "Beeper");
  addWizardMix(auxCh[2], switchSource(wizardData.multiModeSwitch), "Mode");
}
