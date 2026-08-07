/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#pragma once

#include "layout.h"
#include "static.h"
#include "telemetry/telemetry.h"

class FileChoice;
class StaticIcon;

// FPV Dashboard layout — a fixed-content screen inspired by dedicated FPV
// ground-station displays.  It does NOT use the generic zone/widget system;
// instead it creates a fixed set of LVGL objects directly (battery bar, RSSI,
// LQ, model bitmap, arming state, timer, lap counter).
//
// The layout still derives from Layout so it integrates with the existing
// screen-selection and persistence infrastructure (topbar option, etc.).
class BattSetupDialog;
class FpvDashLayout : public Layout
{
 public:
  FpvDashLayout(Window* parent, const LayoutFactory* factory, int screenNum,
                uint8_t zoneCount, uint8_t* zoneMap);
  ~FpvDashLayout();

#if defined(DEBUG_WINDOWS)
  std::string getName() const override { return "FpvDashLayout"; }
#endif

  // Disable topbar, sliders and flight mode — trims are kept for stick feedback
  bool hasTopbar()    const override { return false; }
  bool hasSliders()   const override { return false; }
  bool hasTrims()     const override { return true; }
  bool hasFlightMode() const override { return false; }

  void onClicked() override;  // swallow touch — prevent ViewMain::onClicked → QuickMenu

  friend class BattSetupDialog;

  // Called by battery bar click to start RxBt sensor discovery
  void startSensorDiscovery();

  // Apply/clear focus highlight on all three battery bar elements
  void setBattFocusHighlight(bool focused);

  // Show battery setup popup (cell count + scan button)
  void showBattSetupPopup();

  // ELRS arming support
  int  _elrsArmIdx = -1;
  int  getElrsModuleIdx();
  void showArmSetupPopup();

 protected:
  // ---- Battery bar (left) ------------------------------------------------
  lv_obj_t* battOutline  = nullptr;
  lv_obj_t* battFill     = nullptr;
  lv_obj_t* battPctLabel = nullptr;

  // ---- Model bitmap (centre) ---------------------------------------------
  StaticBitmap* modelBitmap   = nullptr;
  FileChoice*   m_bitmapPicker = nullptr;

  // ---- Arming state + model name (centre-bottom) -------------------------
  lv_obj_t* armLabel     = nullptr;
  lv_obj_t* modelName    = nullptr;

  // ---- Timer (right-centre) ----------------------------------------------
  lv_obj_t* timerLabel   = nullptr;

  // ---- Header bar info (right side) -------------------------------------
  StaticIcon* hdrVolIcon[5]   = {};      // ICON_TOPMENU_VOLUME_0..4
  StaticIcon* hdrBattIcon     = nullptr; // ICON_TOPMENU_TXBATT outline
  lv_obj_t*   hdrBattFill     = nullptr; // fill rect inside battery icon
  lv_obj_t*   hdrVoltText     = nullptr; // "x.xV" text label
  lv_obj_t*   hdrRssiBars[5]  = {};      // 5-bar RSSI indicator
  lv_obj_t*   hdrElrsLabel    = nullptr; // ELRS rate + power (replaces EdgeTX title)
  uint8_t     lastHdrVol      = 255;
  int16_t     lastHdrBatt     = -1;
  char        lastElrsRateStr[16] = {};
  uint32_t    lastElrsTxPower = UINT32_MAX;

  // ---- Timer (secondary, above primary) ---------------------------------
  lv_obj_t* timer2Label  = nullptr;

  // ---- RxBt telemetry sensor -------------------------------------------
  int      rxbtSensorIdx   = -2;       // -2 = not searched yet, -1 = not found
  int32_t  lastRxbtValue   = 0;
  bool     lastRxbtAvail   = false;
  bool     lastScanning    = false;
  tmr10ms_t _rxbtSearchTimer = 0;
  bool     _scanning         = false;
  tmr10ms_t _scanStopTimer   = 0;
  uint8_t  _battCells        = 0;      // 0 = auto-detect, 1-8 = manual cell count
  bool     _battHV           = false;  // high-voltage LiPo (4.35V/cell)

  // ---- Last values to avoid redundant label updates ----------------------
  int16_t  lastBattPct   = -1;
  uint8_t  lastRssi      = 255;
  uint32_t lastTimerVal  = UINT32_MAX;
  uint8_t  lastTimerState = 255;
  char     lastBitmap[LEN_BITMAP_NAME + 1] = {};
  uint32_t lastTimer2Val = UINT32_MAX;
  uint8_t  lastTimer2State = 255;
  char     lastModelName[LEN_MODEL_NAME + 1] = {};
  tmr10ms_t _lastElrsPing = 0;  // debounce ELRS discovery PINGs

  // ---- ELRS cache (populated by ElrsBgLoader via triggerCacheLoad) ----
  bool                _lastArmed     = false;

  void delayedInit() override;
  void checkEvents() override;

  void updateBattery();
  void updateTelemetry();
  void updateModelInfo();
  void updateTimer();
  void updateTopbarInfo();
  void updateElrsHeader();
  void showElrsPowerPopup();
};
