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

#pragma once

#include "edgetx.h"
#include "page.h"

// Model Wizard - C++ replacement for Lua-based wizard scripts
// Supports: Plane, Glider, Wing, Helicopter, Multirotor

enum WizardType {
  WIZARD_TYPE_PLANE,
  WIZARD_TYPE_GLIDER,
  WIZARD_TYPE_WING,
  WIZARD_TYPE_HELI,
  WIZARD_TYPE_MULTIROTOR,
};

class IconButton;

class ModelWizard : public Page
{
 public:
  ModelWizard(WizardType type);

  void onCancel() override;

#if defined(DEBUG_WINDOWS)
  std::string getName() const override { return "ModelWizard"; }
#endif

 protected:
  WizardType wizardType;
  int currentPage = 0;
  int totalPages = 0;

  Window* settingsArea = nullptr;
  Window* imageArea = nullptr;
  lv_obj_t* titleLabel = nullptr;
  lv_obj_t* pageLabel = nullptr;

  // Shared settings — defaults set in constructor via channel order mapping
  struct {
    bool hasMotor = true;
    int motorChannel = 0;      // set in constructor
    bool hasArmSwitch = true;
    int armSwitch = 5;         // SF

    int ailCount = 2;          // 0=None, 1=One, 2=Two
    int ailChA = 0;            // set in constructor
    int ailChB = 0;            // set in constructor

    int flapCount = 0;         // 0=No, 1=One, 2=Two
    int flapChA = 7;
    int flapChB = 8;

    int tailType = 1;          // 0-3
    int tailChA = 0;           // set in constructor
    int tailChB = 0;           // set in constructor
    int tailChC = 5;           // Ele2

    bool hasGear = false;
    int gearSwitch = 3;        // SD
    int gearChannel = 6;

    int expoPercent = 30;
    bool dualRate = true;
    int drSwitch = 2;          // SC

    // Heli
    int heliType = 0;          // 0=FBL, 1=FB
    int swashType = 0;         // 0=120, 1=120X, 2=140, 3=90
    int flyingStyle = 0;       // 0=Sport, 1=Light 3D, 2=Full 3D
    int fmSwitch = 1;          // SB
    int throttleHoldSwitch = 5;// SF
    int tailGainSwitch = 0;    // SA
    int heliThrCh = 0;         // set in constructor
    int heliAilCh = 0;         // set in constructor
    int heliNickCh = 0;        // set in constructor
    int heliRudCh = 0;         // set in constructor
    int thrCurveFM0 = 0;
    int thrCurveFM1 = 0;
    int thrCurveFM2 = 1;

    // Multi
    int multiThrCh = 0;        // set in constructor
    int multiRollCh = 0;       // set in constructor
    int multiPitchCh = 0;      // set in constructor
    int multiYawCh = 0;        // set in constructor
    int multiArmSwitch = 5;
    int multiBeeperSwitch = 3;
    int multiModeSwitch = 0;
  } wizardData;

  // Channel name list
  static constexpr int MAX_CHANNELS = 10;
  const char* channelNames[MAX_CHANNELS] = {
    "CH1", "CH2", "CH3", "CH4", "CH5",
    "CH6", "CH7", "CH8", "CH9", "CH10"
  };
  const char* switchNames[8] = {
    "SA", "SB", "SC", "SD", "SE", "SF", "SG", "SH"
  };

  // Page definitions
  struct WizardPage {
    const char* title;
    const char* subtitle = nullptr;
    void (ModelWizard::*buildFunc)();
  };

  void buildPage();
  void nextPage();
  void prevPage();
  void createNavigationButtons();
  void updateNavigationButtons();

  void showImage(const char* filename);

  IconButton* prevBtn = nullptr;
  IconButton* nextBtn = nullptr;

  // Page builders
  void buildMotorPage();
  void buildAileronPage();
  void buildFlapsPage();
  void buildTailPage();
  void buildGearPage();
  void buildAdditionalPage();

  // Heli pages
  void buildHeliTypePage();
  void buildHeliStylePage();
  void buildHeliSwitchPage();
  void buildHeliThrPage();
  void buildHeliCurvePage();
  void buildHeliAilerPage();
  void buildHeliNickPage();
  void buildHeliRudPage();

  // Multi pages
  void buildMultiThrottlePage();
  void buildMultiRollPage();
  void buildMultiPitchPage();
  void buildMultiYawPage();
  void buildMultiArmPage();
  void buildMultiBeeperPage();
  void buildMultiModePage();

  // Summary & Finish
  void buildSummaryPage();
  void buildFinishedPage();

  // Apply
  void rebuildMainView();
  void applyModelConfig();
  void applyFixedWing();
  void applyHelicopter();
  void applyMultirotor();
};
