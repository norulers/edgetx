/*
 * Yaapu-style telemetry dashboard for EdgeTX C++
 * Supports ArduPilot (yaapu) and INAV (OpenTX-Telemetry-Widget) controllers
 * 480x320 layout matching yaapu layout_def.lua + right_def.lua + left_wp_def.lua
 *
 * Layout (480x320), dark theme (~RGB 20,20,20):
 *   Top bar (0-18, black):       Clock HH:MM:SS | RSSI | TX voltage
 *   Left panel (0,18):           GALT/RNG value | HOME-TRAVEL (dist + total)
 *   Center HUD (120,18):         240x150 artificial horizon (roll+pitch) + compass + speed
 *   Right panel (360,18):        CELL(V) | BATT(V) | CURR(A) | Batt% bar | Capa
 *   Status bar (194-320, dark):  GPS(lat lon) | Timer | FM | Sats+HDOP
 *
 * Font mapping: yaapu DBLSIZE→FONT(BOLD), MIDSIZE→FONT(STD), SMLSIZE→FONT(XS)
 */
#pragma once

#include "window.h"
#include "static.h"

enum TelemetryController {
  CONTROLLER_ARDUPILOT = 0,
  CONTROLLER_INAV = 1,
};

class TelemetryDashViewMenu : public NavWindow
{
 public:
  TelemetryDashViewMenu();
  ~TelemetryDashViewMenu() override;
  void checkEvents() override;
  void onCancel() override;
  void onEvent(event_t event) override;
  void onClicked() override;
#if defined(HARDWARE_KEYS)
  void onLongPressRTN() override;
#endif

 protected:
  void buildUI();
  void buildTopBar();
  void buildCenterHUD();
  void buildLeftPanel();
  void buildRightPanel();
  void buildStatusBar();
  void updateValues();

  int findSensor(const char* name) const;
  float getSensorValue(int idx) const;

  // Sensor name mapping: returns controller-appropriate sensor name
  struct SensorMap { const char* ardupilot; const char* inav; };
  int findMappedSensor(const SensorMap& map) const;

  // Current controller type (set externally or via menu)
  static TelemetryController controllerType;
  bool menuActive = false;
  StaticText* menuText = nullptr;

  // Top bar
  StaticText* clockLabel = nullptr;
  StaticText* txVoltLabel = nullptr;
  StaticText* rssiLabel = nullptr;

  // Center HUD
  lv_obj_t* hudCanvas = nullptr;
  uint8_t* hudBuf = nullptr;
  static constexpr coord_t HUD_W = 240;
  static constexpr coord_t HUD_H = 150;
  static constexpr coord_t HUD_X = 120;
  static constexpr coord_t HUD_Y = 18;
  StaticText* hudSpeedLabel = nullptr;

  // Left panel (left_wp_def.lua: x=0, y=18)
  StaticText* galtRngLabel = nullptr;
  StaticText* galtRngValue = nullptr;
  StaticText* homeDistLabel = nullptr;
  StaticText* totalDistLabel = nullptr;

  // Right panel (right_def.lua: x=360, y=18)
  StaticText* cellVoltLabel = nullptr;
  StaticText* battVoltLabel = nullptr;
  StaticText* currLabel = nullptr;
  lv_obj_t* battBarBg = nullptr;
  lv_obj_t* battBarFill = nullptr;
  StaticText* capaLabel = nullptr;

  // Status bar (y=194-320)
  StaticText* fmLabel = nullptr;
  StaticText* timerLabel = nullptr;
  StaticText* gpsLabel = nullptr;
  StaticText* satsLabel = nullptr;

  // HUD drawing
  void drawHUD();
  void drawArtificialHorizon(lv_color_t* buf, int pitch, int roll);
  void drawCompassRibbon(lv_color_t* buf, int yaw);
  void drawHomeArrow(lv_color_t* buf, int yaw, int homeAngle);

  // Last-value cache
  int lastRssi = -999;
  float lastTxV = -1;
  int lastHdg = -1;
  int lastPitch = 0;
  int lastRoll = 0;
  float lastGAlt = -9999;
  float lastHomeDist = -1;
  float lastTotalDist = -1;
  float lastCell = -1;
  float lastBattV = -1;
  float lastCurr = -1;
  int lastBattPct = -1;
  float lastCapa = -1;
  float lastGSpd = -1;
  int lastSats = -1;
  int lastTimerSec = -1;
  int lastHomeAngle = -1;
  float lastHdop = -1;
  char lastFM[16] = {};
  float lastLat = 0;
  float lastLon = 0;
};
