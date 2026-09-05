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

#include "model_setup.h"

#include <algorithm>
#include <cstring>

#include "button_matrix.h"
#include "dialog.h"
#include "edgetx.h"
#include "etx_lv_theme.h"
#include "filechoice.h"
#include "getset_helpers.h"
#include "hal/adc_driver.h"
#include "mainwindow.h"
#include "menu.h"
#include "model_heli.h"
#include "module_setup.h"
#include "preflight_checks.h"
#include "sourcechoice.h"
#include "storage/modelslist.h"
#include "textedit.h"
#include "timer_setup.h"
#include "toggleswitch.h"
#include "trainer_setup.h"

#if defined(FUNCTION_SWITCHES)
#include "function_switches.h"
#endif

#if defined(USBJ_EX)
#include "model_usbjoystick.h"
#endif

#define SET_DIRTY() storageDirty(EE_MODEL)

ModelSetupPage::ModelSetupPage(const PageDef& pageDef) :
    PageGroupItem(pageDef)
{
}

static void viewOption(Window* parent, coord_t x, coord_t y,
                std::function<uint8_t()> getValue,
                std::function<void(uint8_t)> setValue, bool globalState)
{
  auto lbl = new StaticText(parent, {x + ModelSetupPage::OPTS_W + PAD_MEDIUM, y + PAD_SMALL + 1, 0, 0},
                          STR_ADCFILTERVALUES[globalState ? 1 : 2], COLOR_THEME_SECONDARY1_INDEX);
  new Choice(parent, {x, y, ModelSetupPage::OPTS_W, 0}, STR_ADCFILTERVALUES, 0, 2, getValue,
              [=](int newValue) {
                setValue(newValue);
                lbl->show(newValue == 0);
              });
  lbl->show(getValue() == 0);
}

const static SetupLineDef viewOptionsPageSetupLines[] = {
  {
    STR_DEF(STR_RADIO_MENU_TABS), nullptr,
  },
  {
    STR_DEF(STR_MAIN_MENU_THEMES),
    [](Window* parent, coord_t x, coord_t y) {
      viewOption(parent, x, y,
                GET_SET_DEFAULT(g_model.radioThemesDisabled),
                g_eeGeneral.radioThemesDisabled);
    }
  },
  {
    STR_DEF(STR_MENUSPECIALFUNCS),
    [](Window* parent, coord_t x, coord_t y) {
      viewOption(parent, x, y,
                GET_SET_DEFAULT(g_model.radioGFDisabled),
                g_eeGeneral.radioGFDisabled);
    }
  },
  {
    STR_DEF(STR_MENUTRAINER),
    [](Window* parent, coord_t x, coord_t y) {
      viewOption(parent, x, y,
                GET_SET_DEFAULT(g_model.radioTrainerDisabled),
                g_eeGeneral.radioTrainerDisabled);
    }
  },
  {
    STR_DEF(STR_MODEL_MENU_TABS), nullptr,
  },
#if defined(HELI)
  {
    STR_DEF(STR_MENUHELISETUP),
    [](Window* parent, coord_t x, coord_t y) {
      viewOption(parent, x, y,
                GET_SET_DEFAULT(g_model.modelHeliDisabled),
                g_eeGeneral.modelHeliDisabled);
    }
  },
#endif
#if defined(FLIGHT_MODES)
  {
    STR_DEF(STR_MENUFLIGHTMODES),
    [](Window* parent, coord_t x, coord_t y) {
      viewOption(parent, x, y,
                GET_SET_DEFAULT(g_model.modelFMDisabled),
                g_eeGeneral.modelFMDisabled);
    }
  },
#endif
#if defined(GVARS)
  {
    STR_DEF(STR_MENU_GLOBAL_VARS),
    [](Window* parent, coord_t x, coord_t y) {
      viewOption(parent, x, y,
                GET_SET_DEFAULT(g_model.modelGVDisabled),
                g_eeGeneral.modelGVDisabled);
    }
  },
#endif
  {
    STR_DEF(STR_MENUCURVES),
    [](Window* parent, coord_t x, coord_t y) {
      viewOption(parent, x, y,
                GET_SET_DEFAULT(g_model.modelCurvesDisabled),
                g_eeGeneral.modelCurvesDisabled);
    }
  },
  {
    STR_DEF(STR_MENULOGICALSWITCHES),
    [](Window* parent, coord_t x, coord_t y) {
      viewOption(parent, x, y,
                GET_SET_DEFAULT(g_model.modelLSDisabled),
                g_eeGeneral.modelLSDisabled);
    }
  },
  {
    STR_DEF(STR_MENUCUSTOMFUNC),
    [](Window* parent, coord_t x, coord_t y) {
      viewOption(parent, x, y,
                GET_SET_DEFAULT(g_model.modelSFDisabled),
                g_eeGeneral.modelSFDisabled);
    }
  },
#if defined(LUA_MODEL_SCRIPTS)
  {
    STR_DEF(STR_MENUCUSTOMSCRIPTS),
    [](Window* parent, coord_t x, coord_t y) {
      viewOption(parent, x, y,
                GET_SET_DEFAULT(g_model.modelCustomScriptsDisabled),
                g_eeGeneral.modelCustomScriptsDisabled);
    }
  },
#endif
  {
    STR_DEF(STR_MENUTELEMETRY),
    [](Window* parent, coord_t x, coord_t y) {
      viewOption(parent, x, y,
                GET_SET_DEFAULT(g_model.modelTelemetryDisabled),
                g_eeGeneral.modelTelemetryDisabled);
    }
  },
  {nullptr, nullptr},
};

struct CenterBeepsMatrix : public ButtonMatrix {
  CenterBeepsMatrix(Window* parent, const rect_t& rect) :
    ButtonMatrix(parent, rect)
  {
    // Setup button layout & texts
    uint8_t btn_cnt = 0;

    auto max_sticks = adcGetMaxInputs(ADC_INPUT_MAIN);
    auto max_pots = adcGetMaxInputs(ADC_INPUT_FLEX);
    max_analogs = max_sticks + max_pots;

    for (uint8_t i = 0; i < max_analogs; i++) {
      // multipos cannot be centered
      if (i < max_sticks || (IS_POT_SLIDER_AVAILABLE(i - max_sticks) &&
                            !IS_POT_MULTIPOS(i - max_sticks))) {
        ana_idx[btn_cnt] = i;
        btn_cnt++;
      }
    }

    initBtnMap(min((int)btn_cnt, SW_BTNS), btn_cnt);

    uint8_t btn_id = 0;
    for (uint8_t i = 0; i < max_analogs; i++) {
      if (i < max_sticks || (IS_POT_SLIDER_AVAILABLE(i - max_sticks) &&
                            !IS_POT_MULTIPOS(i - max_sticks))) {
        setTextAndState(btn_id);
        btn_id++;
      }
    }

    update();

    setWidth(min((int)btn_cnt, SW_BTNS) * SW_BTN_W + PAD_SMALL);

    uint8_t rows = ((btn_cnt - 1) / SW_BTNS) + 1;
    setHeight((rows * (EdgeTxStyles::UI_ELEMENT_HEIGHT + PAD_SMALL)) + PAD_SMALL);

    lv_obj_set_style_pad_all(lvobj, PAD_SMALL, LV_PART_MAIN);

    lv_obj_set_style_pad_row(lvobj, PAD_SMALL, LV_PART_MAIN);
    lv_obj_set_style_pad_column(lvobj, PAD_SMALL, LV_PART_MAIN);
  }

  void onPress(uint8_t btn_id)
  {
    if (btn_id >= max_analogs) return;
    uint8_t i = ana_idx[btn_id];
    BFBIT_FLIP(g_model.beepANACenter, bfBit<BeepANACenter>(i));
    setTextAndState(btn_id);
    SET_DIRTY();
  }

  bool isActive(uint8_t btn_id)
  {
    if (btn_id >= max_analogs) return false;
    uint8_t i = ana_idx[btn_id];
    return bfSingleBitGet<BeepANACenter>(g_model.beepANACenter, i) != 0;
  }

  void setTextAndState(uint8_t btn_id)
  {
    auto max_sticks = adcGetMaxInputs(ADC_INPUT_MAIN);
    if (ana_idx[btn_id] < max_sticks)
      setText(btn_id, getAnalogShortLabel(ana_idx[btn_id]));
    else
      setText(btn_id,
              getAnalogLabel(ADC_INPUT_FLEX, ana_idx[btn_id] - max_sticks));
    setChecked(btn_id);
  }

  static LAYOUT_SIZE(SW_BTNS, 8, 4)
  static LAYOUT_SIZE_SCALED(SW_BTN_W, 56, 72)
  static LAYOUT_VAL_SCALED(SW_BTN_H, 36)

 private:
  uint8_t max_analogs;
  uint8_t ana_idx[MAX_ANALOG_INPUTS];
};

const static SetupLineDef otherPageSetupLines[] = {
  {
    STR_DEF(STR_JITTER_FILTER),
    [](Window* parent, coord_t x, coord_t y) {
      new Choice(parent, {x, y, 0, 0}, STR_ADCFILTERVALUES, 0, 2,
                GET_SET_DEFAULT(g_model.jitterFilter));
    }
  },
  {
    STR_DEF(STR_BEEPCTR), [](Window* parent, coord_t x, coord_t y) {}
  },
  {
    nullptr,
    [](Window* parent, coord_t x, coord_t y) {
      auto bm = new CenterBeepsMatrix(parent, {PAD_MEDIUM, y, 0, 0});
      parent->setHeight(bm->height() + PAD_SMALL);
    }
  },
  {nullptr, nullptr},
};

const static SetupLineDef throttleParamsSetupLines[] = {
  {
    // Throttle reversed
    STR_DEF(STR_THROTTLEREVERSE),
    [](Window* parent, coord_t x, coord_t y) {
      new ToggleSwitch(parent, {x, y, 0, 0}, GET_SET_DEFAULT(g_model.throttleReversed));
    }
  },
  {
    // Throttle source
    STR_DEF(STR_TTRACE),
    [](Window* parent, coord_t x, coord_t y) {
      auto sc = new SourceChoice(parent, {x, y, 0, 0}, 0, MIXSRC_LAST_CH,
                                []() {return throttleSource2Source(g_model.thrTraceSrc); },
                                [](int16_t src) {
                                  int16_t val = source2ThrottleSource(src);
                                  if (val >= 0) {
                                    g_model.thrTraceSrc = val;
                                    SET_DIRTY();
                                  }
                                });
      sc->setAvailableHandler(isThrottleSourceAvailable);
    }
  },
  {
    // Throttle trim
    STR_DEF(STR_TTRIM),
    [](Window* parent, coord_t x, coord_t y) {
      new ToggleSwitch(parent, {x, y, 0, 0}, GET_SET_DEFAULT(g_model.thrTrim));
    }
  },
  {
    // Throttle trim source
    STR_DEF(STR_TTRIM_SW),
    [](Window* parent, coord_t x, coord_t y) {
      new SourceChoice(
          parent, {x, y, 0, 0}, MIXSRC_FIRST_TRIM, MIXSRC_LAST_TRIM,
          []() { return g_model.getThrottleStickTrimSource(); },
          [](int16_t src) {
            g_model.setThrottleStickTrimSource(src);
            SET_DIRTY();
          });
    }
  },
  {nullptr, nullptr},
};

#if defined(USE_HATS_AS_KEYS)
static LAYOUT_VAL_SCALED(HATSMODE_W, 120)
#endif

const static SetupLineDef trimsSetupLines[] = {
  {
    // Reset trims
    nullptr,
    [](Window* parent, coord_t x, coord_t y) {
      new TextButton(parent, {PAD_TINY, y, LCD_W - PAD_MEDIUM * 2, 0}, STR_RESET_BTN, []() -> uint8_t {
        for (auto &fm : g_model.flightModeData) memclear(&fm.trim, sizeof(fm.trim));
        SET_DIRTY();
        AUDIO_WARNING1();
        return 0;
      });
    }
  },
#if defined(USE_HATS_AS_KEYS)
  {
    // Hats mode for NV14/EL18
    STR_DEF(STR_HATSMODE),
    [](Window* parent, coord_t x, coord_t y) {
      new Choice(parent, {x, y, HATSMODE_W, 0}, STR_HATSOPT, HATSMODE_TRIMS_ONLY, HATSMODE_GLOBAL,
                GET_SET_DEFAULT(g_model.hatsMode));
      new TextButton(parent, {x + HATSMODE_W + PAD_SMALL, y, 0, 0}, "?", [=]() {
        new MessageDialog(STR_HATSMODE_KEYS, STR_HATSMODE_KEYS_HELP, "",
                          LEFT);
        return 0;
      });
    }
  },
#endif
  {
    // Trim step
    STR_DEF(STR_TRIMINC),
    [](Window* parent, coord_t x, coord_t y) {
      new Choice(parent, {x, y, 0, 0}, STR_VTRIMINC, -2, 2,
                GET_SET_DEFAULT(g_model.trimInc));
    }
  },
  {
    // Extended trims
    STR_DEF(STR_ETRIMS),
    [](Window* parent, coord_t x, coord_t y) {
      new ToggleSwitch(parent, {x, y, 0, 0}, GET_SET_DEFAULT(g_model.extendedTrims));
    }
  },
  {
    // Display trims
    // TODO: move to "Screen setup" ?
    STR_DEF(STR_DISPLAY_TRIMS),
    [](Window* parent, coord_t x, coord_t y) {
      new Choice(parent, {x, y, 0, 0}, STR_VDISPLAYTRIMS, 0, 2,
                GET_SET_DEFAULT(g_model.displayTrims));
    }
  },
  {nullptr, nullptr},
};

const static SetupLineDef setupLines[] = {
  {
    // Model name
    STR_DEF(STR_MODELNAME),
    [](Window* parent, coord_t x, coord_t y) {
      new ModelTextEdit(parent, {x, y, ModelSetupPage::NAM_W, 0},
                        g_model.header.name, sizeof(g_model.header.name),
                        [=]() {
                          auto model = modelslist.getCurrentModel();
                          if (model) {
                            model->setModelName(g_model.header.name);
                          }
                          SET_DIRTY();
                        });
    }
  },
  {
    // Model labels
    STR_DEF(STR_LABELS),
    [](Window* parent, coord_t x, coord_t y) {
      auto curmod = modelslist.getCurrentModel();
      TextButton* btn = new TextButton(parent, {x, y, 0, 0}, modelslabels.getBulletLabelString(curmod, STR_UNLABELEDMODEL));
      btn->setPressHandler([=]() {
            Menu *menu = new Menu(true);
            menu->setTitle(STR_LABELS);
            for (auto &label : modelslabels.getLabels()) {
              menu->addLineBuffered(
                  label,
                  [=]() {
                    if (!modelslabels.isLabelSelected(label, curmod))
                      modelslabels.addLabelToModel(label, curmod);
                    else
                      modelslabels.removeLabelFromModel(label, curmod);
                    btn->setText(modelslabels.getBulletLabelString(
                        curmod, STR_UNLABELEDMODEL));
                    strncpy(g_model.header.labels,
                            ModelMap::toCSV(modelslabels.getLabelsByModel(curmod))
                                .c_str(),
                            sizeof(g_model.header.labels));
                    g_model.header.labels[sizeof(g_model.header.labels) - 1] = '\0';
                    SET_DIRTY();
                  },
                  [=]() { return modelslabels.isLabelSelected(label, curmod); });
            }
            menu->updateLines();
            return 0;
          });
    }
  },
  {
    // Model bitmap
    STR_DEF(STR_BITMAP),
    [](Window* parent, coord_t x, coord_t y) {
      new FileChoice(parent, {x, y, 0, 0}, BITMAPS_PATH, BITMAPS_EXT, LEN_BITMAP_NAME,
                     [=]() {
                       return std::string(g_model.header.bitmap, LEN_BITMAP_NAME);
                     },
                     [=](std::string newValue) {
                       strncpy(g_model.header.bitmap, newValue.c_str(), LEN_BITMAP_NAME);
                       auto model = modelslist.getCurrentModel();
                       if (model) {
                         strncpy(model->modelBitmap, newValue.c_str(), LEN_BITMAP_NAME);
                         model->modelBitmap[LEN_BITMAP_NAME] = '\0';
                       }
                       SET_DIRTY();
                     }, false, STR_BITMAP);
    }
  },
  {nullptr, nullptr},
};

const static PageButtonDef modelSetupButtons[] = {
  // Modules
  {STR_DEF(STR_INTERNALRF), []() { new ModulePage(INTERNAL_MODULE); }, []() { return g_model.moduleData[INTERNAL_MODULE].type > 0; }},
  {STR_DEF(STR_EXTERNALRF), []() { new ModulePage(EXTERNAL_MODULE); }, []() { return g_model.moduleData[EXTERNAL_MODULE].type > 0; }},
  {STR_DEF(STR_TRAINER), []() { auto p = new TrainerPage(); p->setDarkHeader(ICON_MODEL_SETUP); p->setDarkBody(); }, []() { return g_model.trainerData.mode > 0; }},
  // Timer buttons
  {STR_DEF(STR_TIMER_1), []() { TimerWindow::open(0); }, []() { return g_model.timers[0].mode > 0; }},
  {STR_DEF(STR_TIMER_2), []() { TimerWindow::open(1); }, []() { return g_model.timers[1].mode > 0; }},
  {STR_DEF(STR_TIMER_3), []() { TimerWindow::open(2); }, []() { return g_model.timers[2].mode > 0; }},

  {STR_DEF(STR_PREFLIGHT), []() { auto p = new PreflightChecks(); p->setDarkHeader(ICON_MODEL_SETUP); p->setDarkBody(); }},
  {STR_DEF(STR_TRIMS), []() { auto p = new SubPage(ICON_MODEL_SETUP, STR_MAIN_MODEL_SETTINGS, STR_TRIMS, trimsSetupLines); p->setDarkHeader(ICON_MODEL_SETUP); p->setDarkBody(); }},
  {STR_DEF(STR_THROTTLE_LABEL), []() { auto p = new SubPage(ICON_MODEL_SETUP, STR_MAIN_MODEL_SETTINGS, STR_THROTTLE_LABEL, throttleParamsSetupLines); p->setDarkHeader(ICON_MODEL_SETUP); p->setDarkBody(); }},
  {STR_DEF(STR_ENABLED_FEATURES), []() { auto p = new SubPage(ICON_MODEL_SETUP, STR_MAIN_MODEL_SETTINGS, STR_ENABLED_FEATURES, viewOptionsPageSetupLines); p->setDarkHeader(ICON_MODEL_SETUP); p->setDarkBody(); }},
#if defined(USBJ_EX)
  {STR_DEF(STR_USBJOYSTICK_LABEL), []() { auto p = new ModelUSBJoystickPage(); p->setDarkHeader(ICON_MODEL_USB); p->setDarkBody(); }},
#endif
#if defined(FUNCTION_SWITCHES)
  {STR_DEF(STR_FUNCTION_SWITCHES), []() { auto p = new ModelFunctionSwitches(); p->setDarkHeader(ICON_MODEL_SETUP); p->setDarkBody(); }},
#endif
  {STR_DEF(STR_MENU_OTHER), []() { auto p = new SubPage(ICON_MODEL_SETUP, STR_MAIN_MODEL_SETTINGS, STR_MENU_OTHER, otherPageSetupLines); p->setDarkHeader(ICON_MODEL_SETUP); p->setDarkBody(); }},
#if defined(HELI)
  {STR_DEF(STR_MENUHELISETUP), []() { auto p = new ModelHeliPage(); p->setDarkHeader(ICON_MODEL_HELI); p->setDarkBody(); }, nullptr, modelHeliEnabled},
#endif
  {nullptr},
};

void ModelSetupPage::build(Window * window)
{
  // Match ModelSelect FPV header: dark bg shape + orange icon
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
    // Left: dark bg shape + orange model icon
    auto leftBg = new StaticIcon(hdrWin, 0, 0, ICON_TOPLEFT_BG, COLOR_THEME_PRIMARY2_INDEX);
    lv_obj_set_style_img_recolor_opa(leftBg->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_img_recolor(leftBg->getLvObj(), lv_color_make(0x22, 0x22, 0x22), LV_PART_MAIN);
    leftBg->setTop((EdgeTxStyles::MENU_HEADER_HEIGHT - leftBg->height()) / 2);
    auto leftIco = new StaticIcon(leftBg, 0, 0, ICON_MODEL_SETUP, COLOR_THEME_PRIMARY2_INDEX);
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

  lv_obj_t* win = window->getLvObj();
  window->setWindowFlag(OPAQUE);
  lv_obj_set_style_bg_color(win, lv_color_make(0x18, 0x18, 0x18), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(win, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_grad_dir(win, LV_GRAD_DIR_NONE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(pg->getLvObj(), lv_color_make(0x18, 0x18, 0x18), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(pg->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_grad_dir(pg->getLvObj(), LV_GRAD_DIR_NONE, LV_PART_MAIN);
  lv_obj_set_style_text_color(win, lv_color_white(), LV_PART_MAIN);

  // Advanced mode toggle
  static bool s_advanced = false;
  auto* advLine = new Window(window, {0, 0, LCD_W - padding * 2, (coord_t)EdgeTxStyles::UI_ELEMENT_HEIGHT});
  lv_obj_set_style_text_color(advLine->getLvObj(), lv_color_white(), LV_PART_MAIN);
  new StaticText(advLine, {PAD_TINY, PAD_LARGE, LCD_W / 2, (coord_t)EdgeTxStyles::STD_FONT_HEIGHT},
                 "高级模式", COLOR_THEME_PRIMARY2_INDEX);
  new ToggleSwitch(advLine, {SubPage::EDT_X, PAD_TINY, 0, 0},
                   []() -> int { return s_advanced; },
                   [this, window](int v) {
                     s_advanced = v;
                     window->clear();
                     this->build(window);
                   });
  coord_t y = advLine->height() + padding;

  if (s_advanced) {
    y += SetupLine::showLines(window, y, SubPage::EDT_X, padding, setupLines);
  } else {
    // Simplified: only model name and bitmap
    const SetupLineDef basicLines[] = {
      { STR_DEF(STR_MODELNAME), setupLines[0].createEdit },
      { STR_DEF(STR_BITMAP),    setupLines[2].createEdit },
      { nullptr, nullptr },
    };
    y += SetupLine::showLines(window, y, SubPage::EDT_X, padding, basicLines);
  }

  // Override setup line styles from blue-white to dark FPV theme
  for (uint32_t ci = 0; ci < lv_obj_get_child_cnt(window->getLvObj()); ci++) {
    lv_obj_t* setupLine = lv_obj_get_child(window->getLvObj(), ci);
    // Fix title label (first label in each SetupLine) + restyle Choice controls
    bool titleFixed = false;
    for (uint32_t si = 0; si < lv_obj_get_child_cnt(setupLine); si++) {
      lv_obj_t* sc = lv_obj_get_child(setupLine, si);
      if (lv_obj_check_type(sc, &lv_label_class)) {
        lv_obj_set_style_text_color(sc, lv_color_white(), LV_PART_MAIN);
        if (!titleFixed) titleFixed = true;  // first label = title
      }
      // Darken textarea (ModelTextEdit, TextEdit)
      if (lv_obj_check_type(sc, &lv_textarea_class)) {
        lv_obj_set_style_bg_color(sc, lv_color_make(0x28, 0x28, 0x28), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(sc, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_text_color(sc, lv_color_white(), LV_PART_MAIN);
      }
      // Find Choice/FileChoice controls via their dropdown/folder icon image
      for (uint32_t gi = 0; gi < lv_obj_get_child_cnt(sc); gi++) {
        lv_obj_t* gc = lv_obj_get_child(sc, gi);
        if (lv_obj_check_type(gc, &lv_img_class)) {
          const void* src = lv_img_get_src(gc);
          if (lv_img_src_get_type(src) == LV_IMG_SRC_SYMBOL) {
            const char* sym = (const char*)src;
            if (strcmp(sym, LV_SYMBOL_DOWN) == 0 || strcmp(sym, LV_SYMBOL_DIRECTORY) == 0) {
            // Style the Choice/FileChoice control container
            lv_obj_set_style_bg_color(sc, lv_color_make(0x28, 0x28, 0x28), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(sc, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_text_color(sc, lv_color_white(), LV_PART_MAIN);
            // Focus: orange bg, black text
            lv_obj_set_style_bg_color(sc, lv_color_make(0xFF, 0x8C, 0x00), LV_PART_MAIN | LV_STATE_FOCUSED);
            lv_obj_set_style_text_color(sc, lv_color_black(), LV_PART_MAIN | LV_STATE_FOCUSED);
            // Style the dropdown/folder icon to white
            lv_obj_set_style_img_recolor(gc, lv_color_white(), LV_PART_MAIN);
            lv_obj_set_style_img_recolor_opa(gc, LV_OPA_COVER, LV_PART_MAIN);
            // Also recolor icon when control is focused
            lv_obj_set_style_img_recolor(gc, lv_color_black(), LV_PART_MAIN | LV_STATE_FOCUSED);
            lv_obj_set_style_img_recolor_opa(gc, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_FOCUSED);
            // Style all labels inside the choice to white text
            for (uint32_t ki = 0; ki < lv_obj_get_child_cnt(sc); ki++) {
              lv_obj_t* kc = lv_obj_get_child(sc, ki);
              if (lv_obj_check_type(kc, &lv_label_class)) {
                lv_obj_set_style_text_color(kc, lv_color_white(), LV_PART_MAIN);
                lv_obj_set_style_text_color(kc, lv_color_black(), LV_PART_MAIN | LV_STATE_FOCUSED);
              }
            }
            break;  // Found the Choice, stop looking for images in this child
            }
          }
        }
      }
    }
  }

  // Basic buttons (always shown)
  const PageButtonDef basicButtons[] = {
    {STR_DEF(STR_INTERNALRF), []() { new ModulePage(INTERNAL_MODULE); }, []() { return g_model.moduleData[INTERNAL_MODULE].type > 0; }},
    {STR_DEF(STR_EXTERNALRF), []() { new ModulePage(EXTERNAL_MODULE); }, []() { return g_model.moduleData[EXTERNAL_MODULE].type > 0; }},
    {STR_DEF(STR_TRAINER), []() { auto p = new TrainerPage(); p->setDarkHeader(ICON_MODEL_SETUP); p->setDarkBody(); }, []() { return g_model.trainerData.mode > 0; }},
    {STR_DEF(STR_TIMER_1), []() { TimerWindow::open(0); }, []() { return g_model.timers[0].mode > 0; }},
    {STR_DEF(STR_TIMER_2), []() { TimerWindow::open(1); }, []() { return g_model.timers[1].mode > 0; }},
    {STR_DEF(STR_TIMER_3), []() { TimerWindow::open(2); }, []() { return g_model.timers[2].mode > 0; }},
    {nullptr},
  };

  auto* btns = new SetupButtonGroup(window, {0, y, LCD_W - padding * 2, 0},
                                     BTN_COLS,
                                     s_advanced ? modelSetupButtons : basicButtons, BTN_H);

  // Style buttons for FPV dark theme
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(btns->getLvObj()); i++) {
    lv_obj_t* btn = lv_obj_get_child(btns->getLvObj(), i);
    lv_obj_set_style_bg_color(btn, lv_color_make(0x28, 0x28, 0x28), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(btn, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
    // Pressed: green highlight
    lv_obj_set_style_bg_color(btn, lv_color_make(0x00, 0xA0, 0x00), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(btn, lv_color_black(), LV_PART_MAIN | LV_STATE_PRESSED);
    // Focused: orange highlight
    lv_obj_set_style_bg_color(btn, lv_color_make(0xFF, 0x8C, 0x00), LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_text_color(btn, lv_color_black(), LV_PART_MAIN | LV_STATE_FOCUSED);
  }

  // Left-align last button(s) when they don't fill a full row
  int totalBtns = 0;
  for (auto* p = s_advanced ? modelSetupButtons : basicButtons; p->title; p++) totalBtns++;
  int lastRowStart = (totalBtns / BTN_COLS) * BTN_COLS;
  if (totalBtns % BTN_COLS != 0) {
    coord_t xw = (btns->width() - PAD_SMALL * (BTN_COLS + 1) - PAD_TINY * 2) / BTN_COLS + PAD_SMALL;
    coord_t leftX = (btns->width() - (BTN_COLS * xw - PAD_SMALL)) / 2;
    for (int i = lastRowStart; i < totalBtns; i++) {
      lv_obj_t* b = lv_obj_get_child(btns->getLvObj(), i);
      lv_obj_set_x(b, leftX + (i - lastRowStart) * xw);
    }
  }
}

//-----------------------------------------------------------------------------
// ModelMenuPage - standalone grid of all model page icons
//-----------------------------------------------------------------------------

#include "static.h"

ModelMenuPage::ModelMenuPage() :
    NavWindow(MainWindow::instance(),
              {0, EdgeTxStyles::MENU_HEADER_HEIGHT, LCD_W, LCD_H - EdgeTxStyles::MENU_HEADER_HEIGHT})
{
  // Match the main page's dark background
  lv_obj_set_style_bg_color(lvobj, lv_color_make(0x18, 0x18, 0x18), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(lvobj, LV_OPA_COVER, LV_PART_MAIN);

  pushLayer();

  lv_obj_add_flag(lvobj, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(lvobj, [](lv_event_t* e) {
    auto* page = (ModelMenuPage*)lv_event_get_user_data(e);
    page->onCancel();
  }, LV_EVENT_CLICKED, this);

  body = new Window(this, {0, 0, LCD_W, LCD_H - EdgeTxStyles::MENU_HEADER_HEIGHT});
  lv_obj_add_flag(body->getLvObj(), LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(body->getLvObj(), [](lv_event_t* e) {
    auto* page = (ModelMenuPage*)lv_event_get_user_data(e);
    if (page) page->onCancel();
  }, LV_EVENT_CLICKED, this);
  body->setWindowFlag(NO_FOCUS | OPAQUE);
  lv_obj_set_style_bg_color(body->getLvObj(), lv_color_make(0x18, 0x18, 0x18), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(body->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_grad_dir(body->getLvObj(), LV_GRAD_DIR_NONE, LV_PART_MAIN);
  body->padAll(PAD_MEDIUM);

  int visIdx[16], n = 0;
  EdgeTxIcon icons[16];
  for (int i = 0; modelMenuItems[i].icon != EDGETX_ICONS_COUNT; i++) {
    if (!modelMenuItems[i].enabled || modelMenuItems[i].enabled()) {
      icons[n] = modelMenuItems[i].icon;
      visIdx[n++] = i;
    }
  }

  int cols = ModelSetupPage::BTN_COLS;
  coord_t contentW = body->width() - PAD_MEDIUM * 2;
  coord_t btnW = (contentW - PAD_SMALL * (cols + 1) - PAD_TINY * 2) / cols;
  coord_t btnH = ModelSetupPage::BTN_H;
  coord_t gap = PAD_SMALL;
  coord_t gridW = cols * btnW + (cols - 1) * gap;

  int rows = (n + cols - 1) / cols;
  coord_t contentH = body->height() - PAD_MEDIUM * 2;
  coord_t gridH = rows * btnH + (rows - 1) * gap;
  coord_t yo = std::min((contentH - gridH) / 2, (coord_t)0);
  coord_t xo = (contentW - gridW) / 2;

  for (int j = 0; j < n; j++) {
    int idx = visIdx[j];
    coord_t x = xo + (j % cols) * (btnW + gap);
    coord_t y = yo + (j / cols) * (btnH + gap);

    auto btn = new TextButton(body, {x, y, btnW, btnH},
                              std::string(STR_VAL(modelMenuItems[idx].title)),
                              [idx]() -> uint8_t {
                                auto pg = new PageGroup(ICON_MODEL, "Model", modelMenuItems);
                                pg->setCurrentTab(idx);
                                return 0;
                              });

    // FPV dark theme
    lv_obj_set_style_bg_color(btn->getLvObj(), lv_color_make(0x28, 0x28, 0x28), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(btn->getLvObj(), lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_width(btn->getLvObj(), 0, LV_PART_MAIN);
    lv_obj_set_style_outline_width(btn->getLvObj(), 0, LV_PART_MAIN | LV_STATE_FOCUS_KEY);
    lv_obj_set_style_bg_color(btn->getLvObj(), lv_color_make(0x00, 0xA0, 0x00), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(btn->getLvObj(), lv_color_black(), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(btn->getLvObj(), lv_color_make(0xFF, 0x8C, 0x00), LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_text_color(btn->getLvObj(), lv_color_black(), LV_PART_MAIN | LV_STATE_FOCUSED);

    // Use flex row layout so icon and text sit side by side with consistent gap
    lv_obj_set_flex_flow(btn->getLvObj(), LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn->getLvObj(), LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btn->getLvObj(), PAD_SMALL, LV_PART_MAIN);
    lv_obj_set_style_pad_left(btn->getLvObj(), PAD_MEDIUM, LV_PART_MAIN);
    lv_obj_set_style_pad_right(btn->getLvObj(), PAD_SMALL, LV_PART_MAIN);

    // Icon: move to first child so it sits left of the label
    auto icon = new StaticIcon(btn, 0, 0,
                               icons[j], COLOR_THEME_PRIMARY2_INDEX);
    lv_obj_clear_flag(icon->getLvObj(), LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_to_index(icon->getLvObj(), 0);
  }
}

void ModelMenuPage::onCancel()
{
  deleteLater();
}

//-----------------------------------------------------------------------------
// RadioMenuPage - standalone grid of radio page icons
//-----------------------------------------------------------------------------

RadioMenuPage::RadioMenuPage() :
    NavWindow(MainWindow::instance(),
              {0, EdgeTxStyles::MENU_HEADER_HEIGHT, LCD_W, LCD_H - EdgeTxStyles::MENU_HEADER_HEIGHT})
{
  lv_obj_set_style_bg_color(lvobj, lv_color_make(0x18, 0x18, 0x18), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(lvobj, LV_OPA_COVER, LV_PART_MAIN);

  pushLayer();

  lv_obj_add_flag(lvobj, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(lvobj, [](lv_event_t* e) {
    auto* page = (RadioMenuPage*)lv_event_get_user_data(e);
    page->onCancel();
  }, LV_EVENT_CLICKED, this);

  body = new Window(this, {0, 0, LCD_W, LCD_H - EdgeTxStyles::MENU_HEADER_HEIGHT});
  lv_obj_add_flag(body->getLvObj(), LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(body->getLvObj(), [](lv_event_t* e) {
    auto* page = (RadioMenuPage*)lv_event_get_user_data(e);
    if (page) page->onCancel();
  }, LV_EVENT_CLICKED, this);
  body->setWindowFlag(NO_FOCUS | OPAQUE);
  lv_obj_set_style_bg_color(body->getLvObj(), lv_color_make(0x18, 0x18, 0x18), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(body->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_grad_dir(body->getLvObj(), LV_GRAD_DIR_NONE, LV_PART_MAIN);
  body->padAll(PAD_MEDIUM);

  // Filter: only the 5 core radio items
  int visIdx[16], n = 0;
  EdgeTxIcon icons[16];
  for (int i = 0; radioMenuItems[i].icon != EDGETX_ICONS_COUNT; i++) {
    if (!radioMenuItems[i].enabled || radioMenuItems[i].enabled()) {
      if (radioMenuItems[i].qmPage == QM_RADIO_SETUP ||
          radioMenuItems[i].qmPage == QM_RADIO_GF ||
          radioMenuItems[i].qmPage == QM_RADIO_TRAINER ||
          radioMenuItems[i].qmPage == QM_RADIO_HARDWARE ||
          radioMenuItems[i].qmPage == QM_RADIO_VERSION) {
        icons[n] = radioMenuItems[i].icon;
        visIdx[n++] = i;
      }
    }
  }

  int cols = ModelSetupPage::BTN_COLS;
  coord_t contentW = body->width() - PAD_MEDIUM * 2;
  coord_t btnW = (contentW - PAD_SMALL * (cols + 1) - PAD_TINY * 2) / cols;
  coord_t btnH = ModelSetupPage::BTN_H;
  coord_t gap = PAD_SMALL;
  coord_t gridW = cols * btnW + (cols - 1) * gap;

  int rows = (n + cols - 1) / cols;
  coord_t contentH = body->height() - PAD_MEDIUM * 2;
  coord_t gridH = rows * btnH + (rows - 1) * gap;
  coord_t yo = std::min((contentH - gridH) / 2, (coord_t)0);
  coord_t xo = (contentW - gridW) / 2;

  for (int j = 0; j < n; j++) {
    int idx = visIdx[j];
    coord_t x = xo + (j % cols) * (btnW + gap);
    coord_t y = yo + (j / cols) * (btnH + gap);

    auto btn = new TextButton(body, {x, y, btnW, btnH},
                              std::string(STR_VAL(radioMenuItems[idx].title)),
                              [idx]() -> uint8_t {
                                auto pg = new PageGroup(ICON_RADIO, "System", radioMenuItems);
                                pg->setCurrentTab(idx);
                                return 0;
                              });

    // FPV dark theme
    lv_obj_set_style_bg_color(btn->getLvObj(), lv_color_make(0x28, 0x28, 0x28), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(btn->getLvObj(), lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_width(btn->getLvObj(), 0, LV_PART_MAIN);
    lv_obj_set_style_outline_width(btn->getLvObj(), 0, LV_PART_MAIN | LV_STATE_FOCUS_KEY);
    lv_obj_set_style_bg_color(btn->getLvObj(), lv_color_make(0x00, 0xA0, 0x00), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(btn->getLvObj(), lv_color_black(), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(btn->getLvObj(), lv_color_make(0xFF, 0x8C, 0x00), LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_text_color(btn->getLvObj(), lv_color_black(), LV_PART_MAIN | LV_STATE_FOCUSED);

    // Use flex row layout so icon and text sit side by side with consistent gap
    lv_obj_set_flex_flow(btn->getLvObj(), LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn->getLvObj(), LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btn->getLvObj(), PAD_SMALL, LV_PART_MAIN);
    lv_obj_set_style_pad_left(btn->getLvObj(), PAD_MEDIUM, LV_PART_MAIN);
    lv_obj_set_style_pad_right(btn->getLvObj(), PAD_SMALL, LV_PART_MAIN);

    // Icon: move to first child so it sits left of the label
    auto icon = new StaticIcon(btn, 0, 0,
                               icons[j], COLOR_THEME_PRIMARY2_INDEX);
    lv_obj_clear_flag(icon->getLvObj(), LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_to_index(icon->getLvObj(), 0);
  }
}

void RadioMenuPage::onCancel()
{
  deleteLater();
}

//-----------------------------------------------------------------------------
// ToolsMenuPage - hosts RadioToolsPage with main topbar
//-----------------------------------------------------------------------------

ToolsMenuPage::ToolsMenuPage() :
    NavWindow(MainWindow::instance(),
              {0, EdgeTxStyles::MENU_HEADER_HEIGHT, LCD_W, LCD_H - EdgeTxStyles::MENU_HEADER_HEIGHT})
{
  lv_obj_set_style_bg_color(lvobj, lv_color_make(0x18, 0x18, 0x18), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(lvobj, LV_OPA_COVER, LV_PART_MAIN);

  pushLayer();

  lv_obj_add_flag(lvobj, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(lvobj, [](lv_event_t* e) {
    auto* page = (ToolsMenuPage*)lv_event_get_user_data(e);
    page->onCancel();
  }, LV_EVENT_CLICKED, this);

  body = new Window(this, {0, 0, LCD_W, LCD_H - EdgeTxStyles::MENU_HEADER_HEIGHT});
  lv_obj_add_flag(body->getLvObj(), LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(body->getLvObj(), [](lv_event_t* e) {
    auto* page = (ToolsMenuPage*)lv_event_get_user_data(e);
    if (page) page->onCancel();
  }, LV_EVENT_CLICKED, this);
  body->setWindowFlag(NO_FOCUS | OPAQUE);
  lv_obj_set_style_bg_color(body->getLvObj(), lv_color_make(0x18, 0x18, 0x18), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(body->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
  body->padAll(PAD_SMALL);

  toolsMenuItems[0].create(toolsMenuItems[0])->build(body);
}

void ToolsMenuPage::onCancel()
{
  deleteLater();
}
