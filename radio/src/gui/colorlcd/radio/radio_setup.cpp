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

#define LANGUAGE_PACKS_DEFINITION

#include "radio_setup.h"

#include <cstring>

#include "choice.h"
#include "dialog.h"
#include "edgetx.h"
#include "getset_helpers.h"
#include "hal/adc_driver.h"
#include "hal/audio_driver.h"
#include "hal/usb_driver.h"
#include "input_mapping.h"
#include "key_shortcuts.h"
#include "numberedit.h"
#include "page.h"
#include "quick_menu_favorites.h"
#include "slider.h"
#include "storage/modelslist.h"
#include "sourcechoice.h"
#include "tasks/mixer_task.h"
#include "textedit.h"
#include "toggleswitch.h"
#include "storage/sdcard_common.h"
#include "menu.h"
#include "dialog.h"

#define SET_DIRTY() storageDirty(EE_GENERAL)

static const lv_coord_t col_two_dsc[] = {LV_GRID_FR(19), LV_GRID_FR(21),
                                         LV_GRID_TEMPLATE_LAST};
static const lv_coord_t row_dsc[] = {LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};

class DateNumberEdit : public NumberEdit
{
 public:
  DateNumberEdit(Window* parent, coord_t x, coord_t y, int vmin, int vmax, bool leading0,
                  std::function<int()> getValue,
                  std::function<void(int)> setValue,
                  coord_t w = DT_EDT_W) :
      NumberEdit(parent, {x, y, w, 0}, vmin, vmax,
                  getValue,
                  [=](int32_t newValue) {
                    setValue(newValue);
                    SET_DIRTY();
                  })
  {
    lastValue = this->getValue();
    if (leading0)
      setDisplayHandler([](int32_t value) { return formatNumberAsString(value, LEADING0, 2); });
}

  static LAYOUT_ORIENTATION(DT_EDT_W, EdgeTxStyles::EDIT_FLD_WIDTH_NARROW, LAYOUT_SCALE(52))
  static LAYOUT_ORIENTATION(DT_EDT_W_YEAR, EdgeTxStyles::EDIT_FLD_WIDTH_NARROW, LAYOUT_SCALE(70))

 protected:
  int32_t lastValue;

  void checkEvents() override
  {
    if (lastValue != getValue())
      update();
  }
};

class DateTimeWindow : public Window
{
 public:
  DateTimeWindow(Window* parent, const rect_t& rect) :
      Window(parent, rect)
  {
    setWindowFlag(OPAQUE);
    lv_obj_set_style_bg_color(lvobj, lv_color_make(0x18, 0x18, 0x18), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(lvobj, LV_OPA_COVER, LV_PART_MAIN);
    padAll(PAD_ZERO);
    build();
  }

  void checkEvents() override
  {
    Window::checkEvents();

    if (get_tmr10ms() - lastRefresh >= 10) {
      lastRefresh = get_tmr10ms();
      gettime(&m_tm);
    }
  }

  static constexpr coord_t DT_Y2 = PAD_TINY + EdgeTxStyles::UI_ELEMENT_HEIGHT + PAD_MEDIUM;

 protected:
  bool init = false;
  struct gtm m_tm;
  tmr10ms_t lastRefresh = 0;
  NumberEdit* day = nullptr;

  int8_t daysInMonth()
  {
    static const int8_t dmon[] = {31, 28, 31, 30, 31, 30,
                                  31, 31, 30, 31, 30, 31};
    int16_t year = TM_YEAR_BASE + m_tm.tm_year;
    int8_t days_in_month = dmon[m_tm.tm_mon];
    if ((m_tm.tm_mon == 1) &&
        (((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0)))
      days_in_month += 1;
    return days_in_month;
  }

  void setDaysInMonth()
  {
    if (day) {
      day->setMax(daysInMonth());
      if (m_tm.tm_mday > day->getMax()) {
        // Update stored day value if > actual days in month
        // Will be written to RTC via SET_LOAD_DATETIME call after returning
        // UI will update on next iteration of checkEvents
        m_tm.tm_mday = day->getMax();
      }
    }
  }

  void build()
  {
    gettime(&m_tm);

    // Date
    new StaticText(this, rect_t{PAD_TINY, PAD_TINY + PAD_MEDIUM, SubPage::EDT_X - PAD_TINY - PAD_SMALL, EdgeTxStyles::STD_FONT_HEIGHT}, STR_DATE, COLOR_THEME_PRIMARY2_INDEX);
    new DateNumberEdit(this, SubPage::EDT_X, PAD_TINY, 2023, 2037, false,
        [=]() -> int32_t { return TM_YEAR_BASE + m_tm.tm_year; },
        [=](int32_t newValue) {
          m_tm.tm_year = newValue - TM_YEAR_BASE;
          setDaysInMonth();
          SET_LOAD_DATETIME(&m_tm);
        }, DateNumberEdit::DT_EDT_W_YEAR);

    new DateNumberEdit(this, SubPage::EDT_X + DateNumberEdit::DT_EDT_W_YEAR + PAD_TINY, PAD_TINY, 1, 12, false,
        [=]() -> int32_t { return 1 + m_tm.tm_mon; },
        [=](int32_t newValue) {
          m_tm.tm_mon = newValue - 1;
          setDaysInMonth();
          SET_LOAD_DATETIME(&m_tm);
        });

    day = new DateNumberEdit(this, SubPage::EDT_X + DateNumberEdit::DT_EDT_W_YEAR + DateNumberEdit::DT_EDT_W + PAD_SMALL, PAD_TINY, 1, daysInMonth(), true,
        [=]() -> int32_t { return m_tm.tm_mday; },
        [=](int32_t newValue) {
          m_tm.tm_mday = newValue;
          SET_LOAD_DATETIME(&m_tm);
        });

    // Time
    new StaticText(this, rect_t{PAD_TINY, DT_Y2 + PAD_MEDIUM, SubPage::EDT_X - PAD_TINY - PAD_SMALL, EdgeTxStyles::STD_FONT_HEIGHT}, STR_TIME, COLOR_THEME_PRIMARY2_INDEX);
    new DateNumberEdit(this, SubPage::EDT_X, DT_Y2, 0, 23, true,
        [=]() -> int32_t { return m_tm.tm_hour; },
        [=](int32_t newValue) {
          m_tm.tm_hour = newValue;
          SET_LOAD_DATETIME(&m_tm);
        });

    new DateNumberEdit(this, SubPage::EDT_X + DateNumberEdit::DT_EDT_W + PAD_TINY, DT_Y2, 0, 59, true,
        [=]() -> int32_t { return m_tm.tm_min; },
        [=](int32_t newValue) {
          m_tm.tm_min = newValue;
          SET_LOAD_DATETIME(&m_tm);
        });

    new DateNumberEdit(this, SubPage::EDT_X + DateNumberEdit::DT_EDT_W * 2 + PAD_SMALL, DT_Y2, 0, 59, true,
        [=]() -> int32_t { return m_tm.tm_sec; },
        [=](int32_t newValue) {
          m_tm.tm_sec = newValue;
          SET_LOAD_DATETIME(&m_tm);
        });
  }
};

class ControlTextOverride : public StaticText
{
 public:
  ControlTextOverride(Window* parent, coord_t x, coord_t y, FunctionsActive func) :
        StaticText(parent, {x + XO, y + PAD_MEDIUM, 0, 0}, STR_SF_OVERRIDDEN, COLOR_THEME_WARNING_INDEX, FONT_SZ), func(func)
  {
    hide();
  }

  void checkEvents() override
  {
    show(isFunctionActive(func));
  }

  static LAYOUT_SIZE(FONT_SZ, FONT(STD), FONT(XS))
  static LAYOUT_ORIENTATION(XO, PAD_LARGE * 12, PAD_LARGE * 8)

 protected:
  FunctionsActive func;
};

#if defined(AUDIO)
const static SetupLineDef soundPageSetupLines[] = {
  {
    // Beeps mode
    STR_DEF(STR_MODE),
    [](Window* parent, coord_t x, coord_t y) {
      new Choice(parent, {x, y, 0, 0}, STR_VBEEPMODE, -2, 1,
                 GET_SET_DEFAULT(g_eeGeneral.beepMode));
    }
  },
  {
    // Main volume
    STR_DEF(STR_VOLUME),
    [](Window* parent, coord_t x, coord_t y) {
      (new Slider(parent, lv_pct(50), -VOLUME_LEVEL_DEF,
                  VOLUME_LEVEL_MAX - VOLUME_LEVEL_DEF,
                  GET_SET_DEFAULT(g_eeGeneral.speakerVolume)))->setPos(x, y);
    }
  },
  {
    // Beeps volume
    STR_DEF(STR_BEEP_VOLUME),
    [](Window* parent, coord_t x, coord_t y) {
      (new Slider(parent, lv_pct(50), -2, +2,
                  GET_SET_DEFAULT(g_eeGeneral.beepVolume)))->setPos(x, y);
    }
  },
  {
    // Beeps length
    STR_DEF(STR_BEEP_LENGTH),
    [](Window* parent, coord_t x, coord_t y) {
      (new Slider(parent, lv_pct(50), -2, +2,
                  GET_SET_DEFAULT(g_eeGeneral.beepLength)))->setPos(x, y);
    }
  },
  {
    // Beeps pitch
    STR_DEF(STR_BEEP_PITCH),
    [](Window* parent, coord_t x, coord_t y) {
      auto edit = new NumberEdit(parent, rect_t{x, y, EdgeTxStyles::EDIT_FLD_WIDTH_NARROW, 0}, 0, 300,
                                GET_DEFAULT(15 * g_eeGeneral.speakerPitch),
                                [=](int32_t newValue) {
                                  g_eeGeneral.speakerPitch = newValue / 15;
                                  SET_DIRTY();
                                });
      edit->setStep(15);
      edit->setPrefix("+");
      edit->setSuffix("Hz");
    }
  },
  {
    // Wav volume
    STR_DEF(STR_WAV_VOLUME),
    [](Window* parent, coord_t x, coord_t y) {
      (new Slider(parent, lv_pct(50), -2, +2,
                 GET_SET_DEFAULT(g_eeGeneral.wavVolume)))->setPos(x, y);
    }
  },
  {
    // Background volume
    STR_DEF(STR_BG_VOLUME),
    [](Window* parent, coord_t x, coord_t y) {
      (new Slider(parent, lv_pct(50), -2, +2,
                  GET_SET_DEFAULT(g_eeGeneral.backgroundVolume)))->setPos(x, y);
    }
  },
  {
    // Volume source
    STR_DEF(STR_CONTROL),
    [](Window* parent, coord_t x, coord_t y) {
      auto choice = new SourceChoice(parent, {x, y, 0, 0}, MIXSRC_NONE, MIXSRC_LAST_SWITCH,
              GET_SET_DEFAULT(g_eeGeneral.volumeSrc), true);
      choice->setAvailableHandler(isSourceAvailableForBacklightOrVolume);
      new ControlTextOverride(parent, x, y, FUNCTION_VOLUME);
      }
  },
#if defined(KCX_BTAUDIO)
  {
    STR_DEF(STR_BTAUDIO),
    [](Window* parent, coord_t x, coord_t y) {
      new TextButton(parent, {x, y, 0, 0}, STR_PAIRING, [=]() {
        btAudioConnect();
        return 0;
      });
    }
  },
#endif
  {nullptr, nullptr},
};
#endif

#if defined(VARIO)
const static SetupLineDef varioPageSetupLines[] = {
  {
    // Vario volume
    STR_DEF(STR_VOLUME),
    [](Window* parent, coord_t x, coord_t y) {
      (new Slider(parent, lv_pct(50), -2, +2,
                  GET_SET_DEFAULT(g_eeGeneral.varioVolume)))->setPos(x, y);
    }
  },
  {
    STR_DEF(STR_PITCH_AT_ZERO),
    [](Window* parent, coord_t x, coord_t y) {
      auto edit = new NumberEdit(
          parent, {x, y, EdgeTxStyles::EDIT_FLD_WIDTH_NARROW, 0}, VARIO_FREQUENCY_ZERO - 400, VARIO_FREQUENCY_ZERO + 400,
          GET_DEFAULT(VARIO_FREQUENCY_ZERO + (g_eeGeneral.varioPitch * 10)),
          SET_VALUE(g_eeGeneral.varioPitch,
                    (newValue - VARIO_FREQUENCY_ZERO) / 10));
      edit->setStep(10);
      edit->setSuffix("Hz");
    }
  },
  {
    STR_DEF(STR_PITCH_AT_MAX),
    [](Window* parent, coord_t x, coord_t y) {
      auto edit = new NumberEdit(
          parent, {x, y, EdgeTxStyles::EDIT_FLD_WIDTH_NARROW, 0}, 900, 2500,
          GET_DEFAULT(VARIO_FREQUENCY_ZERO + (g_eeGeneral.varioPitch * 10) +
                      VARIO_FREQUENCY_RANGE + (g_eeGeneral.varioRange * 10)),
          SET_VALUE(
              g_eeGeneral.varioRange,
              (newValue - VARIO_FREQUENCY_ZERO - VARIO_FREQUENCY_RANGE) / 10 -
                  g_eeGeneral.varioPitch));
      edit->setStep(10);
      edit->setSuffix("Hz");
    }
  },
  {
    STR_DEF(STR_REPEAT_AT_ZERO),
    [](Window* parent, coord_t x, coord_t y) {
      auto edit = new NumberEdit(
          parent, {x, y, EdgeTxStyles::EDIT_FLD_WIDTH_NARROW, 0}, 200, 1000,
          GET_DEFAULT(VARIO_REPEAT_ZERO + (g_eeGeneral.varioRepeat * 10)),
          SET_VALUE(g_eeGeneral.varioRepeat,
                    (newValue - VARIO_REPEAT_ZERO) / 10));
      edit->setStep(10);
      edit->setSuffix("ms");
    }
  },
  {nullptr, nullptr},
};
#endif

#if defined(HAPTIC)
const static SetupLineDef hapticPageSetupLines[] = {
  {
    // Haptic mode
    STR_DEF(STR_MODE),
    [](Window* parent, coord_t x, coord_t y) {
      new Choice(parent, {x, y, 0, 0}, STR_VBEEPMODE, -2, 1,
                 GET_SET_DEFAULT(g_eeGeneral.hapticMode));
    }
  },
  {
    // Haptic duration
    STR_DEF(STR_LENGTH),
    [](Window* parent, coord_t x, coord_t y) {
      (new Slider(parent, lv_pct(50), -2, +2,
                  GET_SET_DEFAULT(g_eeGeneral.hapticLength)))->setPos(x, y);
    }
  },
  {
    // Haptic strength
    STR_DEF(STR_STRENGTH),
    [](Window* parent, coord_t x, coord_t y) {
      (new Slider(parent, lv_pct(50), -2, +2,
                  GET_SET_DEFAULT(g_eeGeneral.hapticStrength)))->setPos(x, y);
    }
  },
  {nullptr, nullptr},
};
#endif

const static SetupLineDef alarmsPageSetupLines[] = {
  {
    // Battery warning
    STR_DEF(STR_BATTERYWARNING),
    [](Window* parent, coord_t x, coord_t y) {
      auto edit = new NumberEdit(parent, {x, y, EdgeTxStyles::EDIT_FLD_WIDTH_NARROW, 0}, 30, 120,
                                GET_SET_DEFAULT(g_eeGeneral.vBatWarn), PREC1);
      edit->setSuffix("V");
    }
  },
  {
    // Inactivity alarm
    STR_DEF(STR_INACTIVITYALARM),
    [](Window* parent, coord_t x, coord_t y) {
      auto edit = new NumberEdit(parent, {x, y, EdgeTxStyles::EDIT_FLD_WIDTH_NARROW * 3 / 2, 0}, 0, 250,
                                 GET_SET_DEFAULT(g_eeGeneral.inactivityTimer));

      edit->setDisplayHandler([=](int value) -> std::string {
        std::string suffix(STR_MINUTE_PLURAL2);
        if (value == 1) {
          suffix = std::string(STR_MINUTE_SINGULAR);
        } else if (value < g_use_plural2) {
          const int secondDecimal = (value / 10) % 10;
          if (secondDecimal != 1) {
            const int firstDecimal = value % 10;
            if (firstDecimal) {
              if (firstDecimal < g_min_plural2 &&
                  firstDecimal == g_use_singular_in_plural) {
                suffix = std::string(STR_MINUTE_SINGULAR);
              } else if (firstDecimal <= g_max_plural2 &&
                        firstDecimal != g_use_plural2_special_case) {
                suffix = std::string(STR_MINUTE_PLURAL1);
              }
            }
          }
        }
        suffix = " " + suffix;
        return formatNumberAsString(value, 0, 0, nullptr, suffix.c_str());
      });
    }
  },
  {
    // Alarms warning
    STR_DEF(STR_ALARMWARNING),
    [](Window* parent, coord_t x, coord_t y) {
      new ToggleSwitch(parent, {x, y, 0, 0},
                       GET_SET_INVERTED(g_eeGeneral.disableAlarmWarning));
    }
  },
  {
    // RSSI shutdown alarm
    STR_DEF(STR_RSSI_SHUTDOWN_ALARM),
    [](Window* parent, coord_t x, coord_t y) {
      new ToggleSwitch(parent, {x, y, 0, 0},
                       GET_SET_INVERTED(g_eeGeneral.disableRssiPoweroffAlarm));
    }
  },
  {
    // Trainer shutdown alarm
    STR_DEF(STR_TRAINER_SHUTDOWN_ALARM),
    [](Window* parent, coord_t x, coord_t y) {
      new ToggleSwitch(parent, {x, y, 0, 0},
                       GET_SET_INVERTED(g_eeGeneral.disableTrainerPoweroffAlarm));
    }
  },
  {nullptr, nullptr},
};

class BacklightSlider : public Slider
{
 public:
  BacklightSlider(Window* parent, coord_t x, coord_t y,
                  std::function<int()> getValue,
                  std::function<void(int)> setValue) :
          Slider(parent, LV_PCT(50), BACKLIGHT_LEVEL_MIN, BACKLIGHT_LEVEL_MAX,
                 getValue, setValue)
  {
    setPos(x, y);
    updateMsg.subscribe(Messaging::REFRESH, [=](uint32_t param) { update(); });
  }

 protected:
  Messaging updateMsg;
};

const static SetupLineDef backlightSetupLines[] = {
  {
    // Backlight mode
    STR_DEF(STR_MODE),
    [](Window* parent, coord_t x, coord_t y) {
      auto blMode = new Choice(
          parent, {x, y, 0, 0}, STR_VBLMODE, e_backlight_mode_off, e_backlight_mode_on,
          GET_DEFAULT(g_eeGeneral.backlightMode), [=](int32_t newValue) {
            g_eeGeneral.backlightMode = newValue;
            Messaging::send(Messaging::REFRESH);
            SET_DIRTY();
          });

      blMode->setAvailableHandler(
          [=](int newValue) { return newValue != e_backlight_mode_off; });
    }
  },
  {
    // Delay
    STR_DEF(STR_BACKLIGHT_TIMER),
    [](SetupLine* parent, coord_t x, coord_t y) {
      parent->setupMsg.subscribe(Messaging::REFRESH, [=](uint32_t param) {
        parent->show(g_eeGeneral.backlightMode != e_backlight_mode_on);
        resetBacklightTimeout();
      });
      auto edit =
          new NumberEdit(parent, {x, y, EdgeTxStyles::EDIT_FLD_WIDTH_NARROW, 0}, 5, 600,
                        GET_DEFAULT(g_eeGeneral.lightAutoOff * 5),
                        SET_VALUE(g_eeGeneral.lightAutoOff, newValue / 5));
      edit->setStep(5);
      edit->setSuffix("s");
      parent->show(g_eeGeneral.backlightMode != e_backlight_mode_on);
    }
  },
  {
    // Backlight ON bright
    STR_DEF(STR_BLONBRIGHTNESS),
    [](Window* parent, coord_t x, coord_t y) {
      new BacklightSlider(
          parent, x, y,
          [=]() -> int32_t {
            return BACKLIGHT_LEVEL_MAX - g_eeGeneral.backlightBright;
          },
          [=](int32_t newValue) {
            if (newValue >= g_eeGeneral.blOffBright ||
                g_eeGeneral.backlightMode == e_backlight_mode_on) {
              g_eeGeneral.backlightBright = BACKLIGHT_LEVEL_MAX - newValue;
            } else {
              g_eeGeneral.backlightBright =
                  BACKLIGHT_LEVEL_MAX - g_eeGeneral.blOffBright;
              Messaging::send(Messaging::REFRESH);
            }
            SET_DIRTY();
          });
}
  },
  {
    // Backlight OFF bright
    STR_DEF(STR_BLOFFBRIGHTNESS),
    [](Window* parent, coord_t x, coord_t y) {
      new BacklightSlider(
          parent, x, y,
          GET_DEFAULT(g_eeGeneral.blOffBright),
          [=](int32_t newValue) {
            int32_t onBright = BACKLIGHT_LEVEL_MAX - g_eeGeneral.backlightBright;
            if (newValue <= onBright ||
                g_eeGeneral.backlightMode == e_backlight_mode_off) {
              g_eeGeneral.blOffBright = newValue;
            } else {
              g_eeGeneral.blOffBright = onBright;
              Messaging::send(Messaging::REFRESH);
            }
            SET_DIRTY();
          });
}
  },
#if defined(KEYS_BACKLIGHT_GPIO)
  {
    // Keys backlight
    STR_DEF(STR_KEYS_BACKLIGHT),
    [](Window* parent, coord_t x, coord_t y) {
      new ToggleSwitch(parent, {x, y, 0, 0},
                       GET_SET_DEFAULT(g_eeGeneral.keysBacklight));
    }
  },
#endif
  {
    // Backlight/Brightness source
    STR_DEF(STR_CONTROL),
    [](Window* parent, coord_t x, coord_t y) {
      auto choice = new SourceChoice(parent, {x, y, 0, 0}, MIXSRC_NONE, MIXSRC_LAST_SWITCH,
              GET_SET_DEFAULT(g_eeGeneral.backlightSrc), true);
      choice->setAvailableHandler(isSourceAvailableForBacklightOrVolume);
      new ControlTextOverride(parent, x, y, FUNCTION_BACKLIGHT);
    }
  },
  {
    // Flash beep
    STR_DEF(STR_ALARM),
    [](Window* parent, coord_t x, coord_t y) {
      new ToggleSwitch(parent, {x, y, 0, 0}, GET_SET_DEFAULT(g_eeGeneral.alarmsFlash));
    }
  },
  {nullptr, nullptr},
};

const static SetupLineDef gpsPageSetupLines[] = {
  {
    // Timezone
    STR_DEF(STR_TIMEZONE),
    [](Window* parent, coord_t x, coord_t y) {
      auto tz = new NumberEdit(parent, {x, y, EdgeTxStyles::EDIT_FLD_WIDTH_NARROW, 0}, minTimezone(), maxTimezone(),
                              []() {
                                return timezoneIndex(g_eeGeneral.timezone, g_eeGeneral.timezoneMinutes);
                              },
                              [](int newTz) {
                                g_eeGeneral.timezone = timezoneHour(newTz);
                                g_eeGeneral.timezoneMinutes =
                                    timezoneMinute(newTz);
                                SET_DIRTY();
                              });
      tz->setDisplayHandler([](int32_t tz) { return timezoneDisplay(tz); });
    }
  },
  {
    // Adjust RTC (from telemetry)
    STR_DEF(STR_ADJUST_RTC),
    [](Window* parent, coord_t x, coord_t y) {
      new ToggleSwitch(parent, {x, y, 0, 0}, GET_SET_DEFAULT(g_eeGeneral.adjustRTC));
    }
  },
  {
    // GPS format
    STR_DEF(STR_GPS_COORDS_FORMAT),
    [](Window* parent, coord_t x, coord_t y) {
      new Choice(parent, {x, y, 0, 0}, STR_GPSFORMAT, 0, 1,
                 GET_SET_DEFAULT(g_eeGeneral.gpsFormat));
    }
  },
  {nullptr, nullptr},
};

static void viewOption(Window* parent, coord_t x, coord_t y,
                std::function<uint8_t()> getValue,
                std::function<void(uint8_t)> setValue, uint8_t modelOption)
{
  new ToggleSwitch(parent, {x, y, 0, 0}, getValue, setValue);
  if (modelOption != OVERRIDE_GLOBAL) {
    std::string s(STR_MODEL);
    s += " - ";
    s += STR_ADCFILTERVALUES[modelOption];
    new StaticText(parent, {x + ToggleSwitch::TOGGLE_W + PAD_MEDIUM, y + PAD_SMALL + 1, 0, 0}, s.c_str(), COLOR_THEME_SECONDARY1_INDEX);
  }
}

const static SetupLineDef viewOptionsPageSetupLines[] = {
  {
    STR_DEF(STR_RADIO_MENU_TABS), nullptr,
  },
  {
    STR_DEF(STR_MAIN_MENU_THEMES),
    [](Window* parent, coord_t x, coord_t y) {
      viewOption(parent, x, y,
                GET_SET_INVERTED(g_eeGeneral.radioThemesDisabled),
                g_model.radioThemesDisabled);
    }
  },
  {
    STR_DEF(STR_MENUSPECIALFUNCS),
    [](Window* parent, coord_t x, coord_t y) {
      viewOption(parent, x, y,
                GET_SET_INVERTED(g_eeGeneral.radioGFDisabled),
                g_model.radioGFDisabled);
    }
  },
  {
    STR_DEF(STR_MENUTRAINER),
    [](Window* parent, coord_t x, coord_t y) {
      viewOption(parent, x, y,
                GET_SET_INVERTED(g_eeGeneral.radioTrainerDisabled),
                g_model.radioTrainerDisabled);
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
                GET_SET_INVERTED(g_eeGeneral.modelHeliDisabled),
                g_model.modelHeliDisabled);
    }
  },
#endif
#if defined(FLIGHT_MODES)
  {
    STR_DEF(STR_MENUFLIGHTMODES),
    [](Window* parent, coord_t x, coord_t y) {
      viewOption(parent, x, y,
                GET_SET_INVERTED(g_eeGeneral.modelFMDisabled),
                g_model.modelFMDisabled);
    }
  },
#endif
#if defined(GVARS)
  {
    STR_DEF(STR_MENU_GLOBAL_VARS),
    [](Window* parent, coord_t x, coord_t y) {
      viewOption(parent, x, y,
                GET_SET_INVERTED(g_eeGeneral.modelGVDisabled),
                g_model.modelGVDisabled);
    }
  },
#endif
  {
    STR_DEF(STR_MENUCURVES),
    [](Window* parent, coord_t x, coord_t y) {
      viewOption(parent, x, y,
                GET_SET_INVERTED(g_eeGeneral.modelCurvesDisabled),
                g_model.modelCurvesDisabled);
    }
  },
  {
    STR_DEF(STR_MENULOGICALSWITCHES),
    [](Window* parent, coord_t x, coord_t y) {
      viewOption(parent, x, y,
                GET_SET_INVERTED(g_eeGeneral.modelLSDisabled),
                g_model.modelLSDisabled);
    }
  },
  {
    STR_DEF(STR_MENUCUSTOMFUNC),
    [](Window* parent, coord_t x, coord_t y) {
      viewOption(parent, x, y,
                GET_SET_INVERTED(g_eeGeneral.modelSFDisabled),
                g_model.modelSFDisabled);
    }
  },
#if defined(LUA_MODEL_SCRIPTS)
  {
    STR_DEF(STR_MENUCUSTOMSCRIPTS),
    [](Window* parent, coord_t x, coord_t y) {
      viewOption(parent, x, y,
                GET_SET_INVERTED(g_eeGeneral.modelCustomScriptsDisabled),
                g_model.modelCustomScriptsDisabled);
    }
  },
#endif
  {
    STR_DEF(STR_MENUTELEMETRY),
    [](Window* parent, coord_t x, coord_t y) {
      viewOption(parent, x, y,
                GET_SET_INVERTED(g_eeGeneral.modelTelemetryDisabled),
                g_model.modelTelemetryDisabled);
    }
  },
  {nullptr, nullptr},
};

const static SetupLineDef manageModelsSetupLines[] = {
  {
    // Model quick select
    STR_DEF(STR_MODEL_QUICK_SELECT),
    [](Window* parent, coord_t x, coord_t y) {
      new ToggleSwitch(parent, {x, y, 0, 0},
                      GET_SET_DEFAULT(g_eeGeneral.modelQuickSelect));
    }
  },
  {
    // Label single/multi select
    STR_DEF(STR_LABELS_SELECT),
    [](Window* parent, coord_t x, coord_t y) {
      new Choice(parent, {x, y, 0, 0}, STR_LABELS_SELECT_MODE, 0, 1,
                GET_DEFAULT(g_eeGeneral.labelSingleSelect),
                [=](int newValue) {
                  g_eeGeneral.labelSingleSelect = newValue;
                  modelslabels.clearFilter();
                  Messaging::send(Messaging::REFRESH);
                  SET_DIRTY();
                });
    }
  },
  {
    // Label multi select matching mode
    STR_DEF(STR_LABELS_MATCH),
    [](SetupLine* parent, coord_t x, coord_t y) {
      parent->setupMsg.subscribe(Messaging::REFRESH, [=](uint32_t param) {
        parent->show(!g_eeGeneral.labelSingleSelect);
      });
      new Choice(parent, {x, y, 0, 0}, STR_LABELS_MATCH_MODE, 0, 1,
                GET_DEFAULT(g_eeGeneral.labelMultiMode),
                [=](int newValue) {
                  g_eeGeneral.labelMultiMode = newValue;
                  Messaging::send(Messaging::REFRESH);
                  SET_DIRTY();
                });
      parent->show(!g_eeGeneral.labelSingleSelect);
    }
  },
  {
    // Favorites multi select matching mode
    STR_DEF(STR_FAV_MATCH),
    [](SetupLine* parent, coord_t x, coord_t y) {
      parent->setupMsg.subscribe(Messaging::REFRESH, [=](uint32_t param) {
        parent->show(!g_eeGeneral.labelSingleSelect && (g_eeGeneral.labelMultiMode != 0));
      });
      new Choice(parent, {x, y, 0, 0}, STR_FAV_MATCH_MODE, 0, 1,
                GET_SET_DEFAULT(g_eeGeneral.favMultiMode));
      parent->show(!g_eeGeneral.labelSingleSelect && (g_eeGeneral.labelMultiMode != 0));
    }
  },
  {nullptr, nullptr},
};

const static SetupLineDef setupLines[] = {
  {
    // Have only one log per day
    STR_DEF(STR_ONE_LOG_PER_DAY),
    [](Window* parent, coord_t x, coord_t y) {
      new ToggleSwitch(parent, {x, y, 0, 0}, GET_SET_DEFAULT(g_eeGeneral.oneLogPerDay));
    }
  },
  {
    // Splash screen
    STR_DEF(STR_SPLASHSCREEN),
    [](Window* parent, coord_t x, coord_t y) {
      new Choice(
          parent, {x, y, 0, 0}, STR_SPLASHSCREEN_DELAYS, 0, 7,
          [=]() -> int32_t { return 3 - g_eeGeneral.splashMode; },
          [=](int32_t newValue) {
            g_eeGeneral.splashMode = 3 - newValue;
            SET_DIRTY();
          });
    }
  },
  {
    // Play startup sound
    STR_DEF(STR_PLAY_HELLO),
    [](Window* parent, coord_t x, coord_t y) {
      new ToggleSwitch(parent, {x, y, 0, 0}, GET_SET_INVERTED(g_eeGeneral.dontPlayHello));
    }
  },
#if defined(PWR_BUTTON_PRESS) && !defined(PWR_BUTTON_MANAGED)
  {
    // Pwr Off Delay
    STR_DEF(STR_PWR_OFF_DELAY),
    [](Window* parent, coord_t x, coord_t y) {
      new Choice(
          parent, {x, y, 0, 0}, STR_PWR_OFF_DELAYS, 0, 4,
          [=]() -> int32_t { return pwrDelayFromYaml(g_eeGeneral.pwrOffSpeed); },
          [=](int32_t newValue) {
            g_eeGeneral.pwrOffSpeed = pwrDelayToYaml(newValue);
            SET_DIRTY();
          });
    }
  },
#endif
#if defined(PWR_BUTTON_PRESS)
  // Pwr Off If Inactive
  {
    STR_DEF(STR_PWR_AUTO_OFF),
     [](Window* parent, coord_t x, coord_t y) {
       auto edit = new NumberEdit(parent,{x, y, EdgeTxStyles::EDIT_FLD_WIDTH_NARROW, EdgeTxStyles::UI_ELEMENT_HEIGHT}, 0,
           255, GET_SET_DEFAULT(g_eeGeneral.pwrOffIfInactive));
       edit->setSuffix(" min");
     }
  },
#endif
#if defined(HAPTIC)
  {
    // Power on/off haptic alarm
      STR_DEF(STR_PWR_ON_OFF_HAPTIC),
      [](Window* parent, coord_t x, coord_t y) {
        new ToggleSwitch(parent, {x, y, 0, 0}, GET_SET_INVERTED(g_eeGeneral.disablePwrOnOffHaptic));
      }
  },
#endif
#if defined(PXX2)
  {
    // Owner ID
    STR_DEF(STR_OWNER_ID),
    [](Window* parent, coord_t x, coord_t y) {
      new RadioTextEdit(parent, {x, y, 0, 0}, g_eeGeneral.ownerRegistrationID,
                        PXX2_LEN_REGISTRATION_ID);
    }
  },
#endif
  {
    // Country code
    STR_DEF(STR_COUNTRY_CODE),
    [](Window* parent, coord_t x, coord_t y) {
      new Choice(parent, {x, y, 0, 0}, STR_COUNTRY_CODES, 0, 2,
                GET_SET_DEFAULT(g_eeGeneral.countryCode));
    }
  },
  {
    // Audio language
    STR_DEF(STR_VOICE_LANGUAGE),
    [](Window* parent, coord_t x, coord_t y) {
      auto choice =
          new Choice(parent, {x, y, 0, 0}, 0, DIM(languagePacks) - 2,
                    GET_VALUE(currentLanguagePackIdx), [](uint8_t newValue) {
                      currentLanguagePackIdx = newValue;
                      currentLanguagePack = languagePacks[currentLanguagePackIdx];
                      strncpy(g_eeGeneral.ttsLanguage, currentLanguagePack->id, 2);
                      SET_DIRTY();
                    });
#if !defined(ALL_LANGS)
      choice->setTextHandler(
          [](uint8_t value) { return languagePacks[value]->name; });
#else
      choice->setTextHandler(
          [](uint8_t value) {
            // TODO: language name should always be in the language of the name, not
            //       the current UI language. Needs translation characters to be
            //       always available for all language names in the base font.
            //       temp solution - prepend language id to name.
            std::string s(languagePacks[value]->id);
            s += " - ";
            s += languagePacks[value]->name();
            return s;
          });
#endif
    }
  },
#if defined(ALL_LANGS)
  {
    // UI language
    STR_DEF(STR_TEXT_LANGUAGE),
    [](Window* parent, coord_t x, coord_t y) {
      auto choice =
          new Choice(parent, {x, y, 0, 0}, 0, DIM(languagePacks) - 2,
                    GET_VALUE(getLanguageId(g_eeGeneral.uiLanguage)),
                    [](uint8_t newValue) {
                      strncpy(g_eeGeneral.uiLanguage, languagePacks[newValue]->id, 2);
                      currentLangStrings = langStrings[newValue];
                      extern void setLanguageFont(int idx);
                      setLanguageFont(newValue);
                      PageGroup* pg = Window::pageGroup();
                      coord_t y = pg->getScrollY();
                      pg->onCancel();
                      QuickMenu::openPage(QM_RADIO_SETUP);
                      pg = Window::pageGroup();
                      pg->setScrollY(y);
                      // Force QM rebuild for language change
                      QuickMenu::shutdownQuickMenu();
                      SET_DIRTY();
                    });
      choice->setAvailableHandler([=](int n) { return isTextLangAvail(n); });
      choice->setTextHandler(
          [](uint8_t value) {
            // TODO: language name should always be in the language of the name, not
            //       the current UI language. Needs translation characters to be
            //       always available for all language names in the base font.
            //       temp solution - prepend language id to name.
            std::string s(languagePacks[value]->id);
            s += " - ";
            s += languagePacks[value]->name();
            return s;
          });
    }
  },
#endif
  {
    // Imperial units
    STR_DEF(STR_UNITS_SYSTEM),
    [](Window* parent, coord_t x, coord_t y) {
      new Choice(parent, {x, y, 0, 0}, STR_VUNITSSYSTEM, 0, 1,
                GET_SET_DEFAULT(g_eeGeneral.imperial));
    }
  },
  {
    // PPM units
    STR_DEF(STR_UNITS_PPM),
    [](Window* parent, coord_t x, coord_t y) {
      new Choice(parent, {x, y, 0, 0}, STR_PPMUNIT, PPM_PERCENT_PREC0, PPM_US,
                GET_SET_DEFAULT(g_eeGeneral.ppmunit));
    }
  },
  {
    // Switches delay
    STR_DEF(STR_SWITCHES_DELAY),
    [](Window* parent, coord_t x, coord_t y) {
      auto edit =
          new NumberEdit(parent, {x, y, EdgeTxStyles::EDIT_FLD_WIDTH_NARROW, 0}, 0, 100,
                        GET_SET_VALUE_WITH_OFFSET(g_eeGeneral.switchesDelay, 15));
      edit->setDisplayHandler([](int32_t value) {
        return formatNumberAsString(value * 10, 0, 0, nullptr, STR_MS);
      });
    }
  },
  {
    // USB mode
    STR_DEF(STR_USBMODE),
    [](Window* parent, coord_t x, coord_t y) {
      new Choice(parent, {x, y, 0, 0}, STR_USBMODES, USB_UNSELECTED_MODE, USB_MAX_MODE,
                GET_SET_DEFAULT(g_eeGeneral.USBMode));
    }
  },
#if defined(USB_CHARGE_CONTROL)
  {
    // Charge while radio on
    STR_DEF(STR_USB_CHARGE),
    [](Window* parent, coord_t x, coord_t y) {
      new ToggleSwitch(parent, {x, y, 0, 0},
                       GET_INVERTED(g_eeGeneral.usbChargeDisabled),
                       [](uint8_t newValue) {
                         g_eeGeneral.usbChargeDisabled = !newValue;
                         SET_DIRTY();
                         // take effect now, not only on the next plug
                         if (usbPlugged())
                           usbChargerEnableCharge(!g_eeGeneral.usbChargeDisabled);
                       });
    }
  },
#endif
#if defined(ROTARY_ENCODER_NAVIGATION) && !defined(USE_HATS_AS_KEYS)
  {
    STR_DEF(STR_ROTARY_ENC_MODE),
    [](Window* parent, coord_t x, coord_t y) {
      new Choice(parent, {x, y, 0, 0}, STR_ROTARY_ENC_OPT, ROTARY_ENCODER_MODE_NORMAL,
                ROTARY_ENCODER_MODE_INVERT_BOTH,
                GET_SET_DEFAULT(g_eeGeneral.rotEncMode));
    }
  },
#endif
#if defined(USE_HATS_AS_KEYS)
  {
    STR_DEF(STR_HATSMODE),
    [](Window* parent, coord_t x, coord_t y) {
      new Choice(parent, {x, y, RadioSetupPage::HATS_MODE_W, 0}, STR_HATSOPT, HATSMODE_TRIMS_ONLY,
                HATSMODE_SWITCHABLE, GET_SET_DEFAULT(g_eeGeneral.hatsMode));
      new TextButton(parent, {x + RadioSetupPage::HATS_MODE_W + PAD_MEDIUM, y, 0, 0}, "?", [=]() {
        new MessageDialog(STR_HATSMODE_KEYS, STR_HATSMODE_KEYS_HELP, "",
                          LEFT);
        return 0;
      });
    }
  },
#endif
  {
    // RX channel order
    STR_DEF(STR_DEF_CHAN_ORD),
    [](Window* parent, coord_t x, coord_t y) {
      uint8_t mains = adcGetMaxInputs(ADC_INPUT_MAIN);
      auto max_order = inputMappingGetMaxChannelOrder() - 1;
      auto choice = new Choice(parent, {x, y, 0, 0}, 0, max_order,
                          GET_SET_DEFAULT(g_eeGeneral.templateSetup));

      choice->setTextHandler([=](uint8_t value) {
        std::string s;
        for (uint8_t i = 0; i < mains; i++) {
          s += getAnalogShortLabel(inputMappingChannelOrder(value, i));
        }
        return s;
      });
    }
  },
  {
    // Stick mode
    STR_DEF(STR_MODE),
    [](Window* parent, coord_t x, coord_t y) {
      auto choice = new Choice(parent, {x, y, 0, 0}, 0, 3, GET_DEFAULT(g_eeGeneral.stickMode),
                          [=](uint8_t newValue) {
                            mixerTaskStop();
                            g_eeGeneral.stickMode = newValue;
                            SET_DIRTY();
                            checkThrottleStick();
                            mixerTaskStart();
                          });
      choice->setTextHandler([](uint8_t value) {
        auto stick0 = inputMappingConvertMode(value, 0);
        auto stick1 = inputMappingConvertMode(value, 1);
        return std::to_string(1 + value) + ": " + STR_LEFT_STICK + " = " +
              std::string(getMainControlLabel(stick0)) + "+" +
              std::string(getMainControlLabel(stick1));
      });
    }
  },
  {nullptr, nullptr},
};

RadioSetupPage::RadioSetupPage(const PageDef& pageDef) : PageGroupItem(pageDef, PAD_TINY) {}

#if VERSION_MAJOR > 2
static bool hasShortcutKeys()
{
#if defined(USE_HATS_AS_KEYS)
  return true;
#else
  return keysGetSupported() & ((1 << KEY_MODEL) | (1 << KEY_SYS) | (1 << KEY_TELE));
#endif
}
#endif

const static PageButtonDef radioSetupButtons[] = {
#if defined(AUDIO)
  {STR_DEF(STR_SOUND_LABEL), []() { auto p = new SubPage(ICON_RADIO_SETUP, STR_MAIN_RADIO_SETTINGS, STR_SOUND_LABEL, soundPageSetupLines); p->setDarkHeader(ICON_RADIO_SETUP); p->setDarkBody(); }},
#endif
#if defined(VARIO)
  {STR_DEF(STR_VARIO), []() { auto p = new SubPage(ICON_RADIO_SETUP, STR_MAIN_RADIO_SETTINGS, STR_VARIO, varioPageSetupLines); p->setDarkHeader(ICON_RADIO_SETUP); p->setDarkBody(); }},
#endif
#if defined(HAPTIC)
  {STR_DEF(STR_HAPTIC_LABEL), []() { auto p = new SubPage(ICON_RADIO_SETUP, STR_MAIN_RADIO_SETTINGS, STR_HAPTIC_LABEL, hapticPageSetupLines); p->setDarkHeader(ICON_RADIO_SETUP); p->setDarkBody(); }},
#endif
  {STR_DEF(STR_ALARMS_LABEL), []() { auto p = new SubPage(ICON_RADIO_SETUP, STR_MAIN_RADIO_SETTINGS, STR_ALARMS_LABEL, alarmsPageSetupLines); p->setDarkHeader(ICON_RADIO_SETUP); p->setDarkBody(); }},
  {STR_DEF(STR_BACKLIGHT_LABEL), []() { auto p = new SubPage(ICON_RADIO_SETUP, STR_MAIN_RADIO_SETTINGS, STR_BACKLIGHT_LABEL, backlightSetupLines); p->setDarkHeader(ICON_RADIO_SETUP); p->setDarkBody(); p->useFlexLayout(); }},
  {STR_DEF(STR_GPS), []() { auto p = new SubPage(ICON_RADIO_SETUP, STR_MAIN_RADIO_SETTINGS, STR_GPS, gpsPageSetupLines); p->setDarkHeader(ICON_RADIO_SETUP); p->setDarkBody(); }},
  {STR_DEF(STR_ENABLED_FEATURES), []() { auto p = new SubPage(ICON_RADIO_SETUP, STR_MAIN_RADIO_SETTINGS, STR_ENABLED_FEATURES, viewOptionsPageSetupLines); p->setDarkHeader(ICON_RADIO_SETUP); p->setDarkBody(); }},
  {STR_DEF(STR_MAIN_MENU_MANAGE_MODELS), []() { auto p = new SubPage(ICON_RADIO_SETUP, STR_MAIN_RADIO_SETTINGS, STR_MAIN_MENU_MANAGE_MODELS, manageModelsSetupLines); p->setDarkHeader(ICON_RADIO_SETUP); p->setDarkBody(); }},
#if VERSION_MAJOR > 2
  {STR_DEF(STR_KEY_SHORTCUTS), []() { auto p = new QMKeyShortcutsPage(); p->setDarkHeader(ICON_RADIO_SETUP); p->setDarkBody(); p->enableRefresh(); }, nullptr, []() { return hasShortcutKeys(); }},
  {STR_DEF(STR_QUICK_MENU_FAVORITES), []() { auto p = new QMFavoritesPage(); p->setDarkHeader(ICON_RADIO_SETUP); p->setDarkBody(); p->enableRefresh(); }, nullptr},
#endif
  {STR_DEF(STR_FACTORY_RESET), []() {
    auto menu = new Menu();
    menu->addLine(STR_FACTORY_RESET_CLEAR_SETTINGS, []() {
      new ConfirmDialog(STR_FACTORY_RESET_CLEAR_SETTINGS, STR_CONFIRM_CLEAR_SETTINGS, []() {
        // Preserve stick/pot calibration data
        CalibData savedCalib[MAX_CALIB_ANALOG_INPUTS];
        for (int i = 0; i < MAX_CALIB_ANALOG_INPUTS; i++) {
          savedCalib[i] = g_eeGeneral.calib[i];
        }
        generalDefault();
        // Restore calibration
        for (int i = 0; i < MAX_CALIB_ANALOG_INPUTS; i++) {
          g_eeGeneral.calib[i] = savedCalib[i];
        }
        storageDirty(EE_GENERAL);
        storageCheck(true);
      });
    });
    menu->addLine(STR_FACTORY_RESET_CLEAR_MODELS, []() {
      new ConfirmDialog(STR_FACTORY_RESET_CLEAR_MODELS, STR_CONFIRM_CLEAR_MODELS, []() {
        // Delete all model files
        while (modelslist.size() > 0) {
          modelslist.removeModel(modelslist[0]);
        }
        modelslist.save();
        storageCheck(true);
        // Reboot to fully reset all UI state
        NVIC_SystemReset();
      });
    });
  }},
  {nullptr},
};

void RadioSetupPage::build(Window* window)
{
  window->setFlexLayout(LV_FLEX_FLOW_COLUMN, padding);
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
    // Left: dark bg shape + orange radio icon
    auto leftBg = new StaticIcon(hdrWin, 0, 0, ICON_TOPLEFT_BG, COLOR_THEME_PRIMARY2_INDEX);
    lv_obj_set_style_img_recolor_opa(leftBg->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_img_recolor(leftBg->getLvObj(), lv_color_make(0x22, 0x22, 0x22), LV_PART_MAIN);
    leftBg->setTop((EdgeTxStyles::MENU_HEADER_HEIGHT - leftBg->height()) / 2);
    auto leftIco = new StaticIcon(leftBg, 0, 0, ICON_RADIO_SETUP, COLOR_THEME_PRIMARY2_INDEX);
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

  // Date & time picker including labels
  new DateTimeWindow(window, {0, 0, LCD_W - padding * 2, EdgeTxStyles::UI_ELEMENT_HEIGHT * 2 + PAD_TINY * 2 + PAD_MEDIUM});

  // Basic buttons (always shown)
  const PageButtonDef basicButtons[] = {
#if defined(AUDIO)
    {STR_DEF(STR_SOUND_LABEL), []() { auto p = new SubPage(ICON_RADIO_SETUP, STR_MAIN_MENU_RADIO_SETTINGS, STR_SOUND_LABEL, soundPageSetupLines); p->setDarkHeader(ICON_RADIO_SETUP); p->setDarkBody(); }},
#endif
    {STR_DEF(STR_ALARMS_LABEL), []() { auto p = new SubPage(ICON_RADIO_SETUP, STR_MAIN_MENU_RADIO_SETTINGS, STR_ALARMS_LABEL, alarmsPageSetupLines); p->setDarkHeader(ICON_RADIO_SETUP); p->setDarkBody(); }},
    {STR_DEF(STR_BACKLIGHT_LABEL), []() { auto p = new SubPage(ICON_RADIO_SETUP, STR_MAIN_MENU_RADIO_SETTINGS, STR_BACKLIGHT_LABEL, backlightSetupLines); p->setDarkHeader(ICON_RADIO_SETUP); p->setDarkBody(); p->useFlexLayout(); }},
    {nullptr},
  };

  auto* w = new SetupButtonGroup(window, {0, 0, LCD_W - padding * 2, 0}, BTN_COLS,
                            s_advanced ? radioSetupButtons : basicButtons, BTN_H);
  // FPV dark button style
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(w->getLvObj()); i++) {
    lv_obj_t* btn = lv_obj_get_child(w->getLvObj(), i);
    lv_obj_set_style_bg_color(btn, lv_color_make(0x28, 0x28, 0x28), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(btn, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_make(0x00, 0xA0, 0x00), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(btn, lv_color_black(), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(btn, lv_color_make(0xFF, 0x8C, 0x00), LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_text_color(btn, lv_color_black(), LV_PART_MAIN | LV_STATE_FOCUSED);
  }

  SetupLine::showLines(window, 0, SubPage::EDT_X, padding, setupLines);

  // Override setup line styles from blue-white to dark FPV theme
  for (uint32_t ci = 0; ci < lv_obj_get_child_cnt(window->getLvObj()); ci++) {
    lv_obj_t* setupLine = lv_obj_get_child(window->getLvObj(), ci);
    bool titleFixed = false;
    for (uint32_t si = 0; si < lv_obj_get_child_cnt(setupLine); si++) {
      lv_obj_t* sc = lv_obj_get_child(setupLine, si);
      if (lv_obj_check_type(sc, &lv_label_class)) {
        lv_obj_set_style_text_color(sc, lv_color_white(), LV_PART_MAIN);
        if (!titleFixed) titleFixed = true;
      }
      if (lv_obj_check_type(sc, &lv_textarea_class)) {
        lv_obj_set_style_bg_color(sc, lv_color_make(0x28, 0x28, 0x28), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(sc, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_text_color(sc, lv_color_white(), LV_PART_MAIN);
      }
      for (uint32_t gi = 0; gi < lv_obj_get_child_cnt(sc); gi++) {
        lv_obj_t* gc = lv_obj_get_child(sc, gi);
        if (lv_obj_check_type(gc, &lv_img_class)) {
          const void* src = lv_img_get_src(gc);
          if (lv_img_src_get_type(src) == LV_IMG_SRC_SYMBOL) {
            const char* sym = (const char*)src;
            if (strcmp(sym, LV_SYMBOL_DOWN) == 0 || strcmp(sym, LV_SYMBOL_DIRECTORY) == 0) {
              lv_obj_set_style_bg_color(sc, lv_color_make(0x28, 0x28, 0x28), LV_PART_MAIN);
              lv_obj_set_style_bg_opa(sc, LV_OPA_COVER, LV_PART_MAIN);
              lv_obj_set_style_text_color(sc, lv_color_white(), LV_PART_MAIN);
              lv_obj_set_style_bg_color(sc, lv_color_make(0xFF, 0x8C, 0x00), LV_PART_MAIN | LV_STATE_FOCUSED);
              lv_obj_set_style_text_color(sc, lv_color_black(), LV_PART_MAIN | LV_STATE_FOCUSED);
              lv_obj_set_style_img_recolor(gc, lv_color_white(), LV_PART_MAIN);
              lv_obj_set_style_img_recolor_opa(gc, LV_OPA_COVER, LV_PART_MAIN);
              lv_obj_set_style_img_recolor(gc, lv_color_black(), LV_PART_MAIN | LV_STATE_FOCUSED);
              lv_obj_set_style_img_recolor_opa(gc, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_FOCUSED);
              for (uint32_t ki = 0; ki < lv_obj_get_child_cnt(sc); ki++) {
                lv_obj_t* kc = lv_obj_get_child(sc, ki);
                if (lv_obj_check_type(kc, &lv_label_class)) {
                  lv_obj_set_style_text_color(kc, lv_color_white(), LV_PART_MAIN);
                  lv_obj_set_style_text_color(kc, lv_color_black(), LV_PART_MAIN | LV_STATE_FOCUSED);
                }
              }
              break;
            }
          }
        }
      }
    }
  }
}
