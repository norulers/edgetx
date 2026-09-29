/*
 * Yaapu-style telemetry dashboard for EdgeTX C++ (screen content only).
 *
 * Layout, design space 480x320, uniformly scaled to the actual LCD:
 *
 *   y   0.. 56  [sat] SATELLITE / SAT: n      BATTERY / n.nV [batt]
 *   y  57       tan rule, interrupted by the dial
 *   y  18..224  circular horizon dial, centre (240,121), r=103: roll scale,
 *               pitch ladder, aircraft symbol and a compass rose band across
 *               the lower half (canvas y 2..240)
 *   y  58..182  altitude tape (left) / ground-speed tape (right)
 *   y 184..222  readouts: 312 m (left)  GND SPD / 40 km/h (right)
 *   y 222..318  chamfered 3x2 panel: VSpd RxV Curr / Bat% RSSI FRv
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
  // Vertical value tape: 5 scale numbers, 6 tick marks and a centre box
  // showing the current value. The box outline is a pentagon whose tip points
  // at the dial (lv_line keeps a pointer to the points, so they live here).
  // The geometry is filled in by buildTape() and reused by updateTape().
  struct Tape {
    bool        left = true;
    coord_t     x = 0, y = 0, w = 0, h = 0;
    coord_t     numX = 0, numW = 0, tickX = 0, centerY = 0, stepPx = 0;
    StaticText* num[5] = {};
    lv_obj_t*   tick[6] = {};
    lv_obj_t*   box = nullptr;
    lv_obj_t*   boxOutline = nullptr;
    lv_point_t  boxPts[6] = {};
    StaticText* boxValue = nullptr;
  };

  void buildUI();
  void buildTopBar();
  void buildDial();
  void buildSkirt();
  void buildTape(Tape& t, coord_t designX, bool left);
  void buildReadouts();
  void buildGrid();

  void updateValues();
  void updateTape(Tape& t, float value, float step, bool valid);
  void drawDial();
  void drawSkirt();
  uint32_t dialPixel(int dx, int dy) const;
  bool bandPixel(int dx, int dy, uint32_t& col) const;

  // attitude factors cached by drawDial() and used by dialPixel()
  float dashCosR = 1.0f;
  float dashSinR = 0.0f;
  float dashPitchOff = 0.0f;

  // telemetry helpers
  int   findSensor(const char* name) const;
  int   findMappedSensor(const char* primary, const char* secondary) const;
  int   findForController(const char* apName, const char* inavName) const;
  float getSensorValue(int idx) const;
  float getSensorValueIn(int idx, uint8_t dstUnit, uint8_t dstPrec) const;

  static TelemetryController controllerType;
  bool        menuActive = false;
  StaticText* menuText = nullptr;

  // top bar
  lv_obj_t*   satIcon = nullptr;
  uint8_t*    satIconBuf = nullptr;
  lv_obj_t*   battIcon = nullptr;
  uint8_t*    battIconBuf = nullptr;
  StaticText* satValue = nullptr;
  StaticText* battValue = nullptr;

  // centre dial
  lv_obj_t*   dialCanvas = nullptr;
  uint8_t*    dialBuf = nullptr;
  lv_obj_t*   skirtCanvas = nullptr;   // dial rim that overlaps the panel
  uint8_t*    skirtBuf = nullptr;

  // tapes / readouts
  Tape        altTape;
  Tape        spdTape;
  StaticText* altReadout = nullptr;
  StaticText* spdTitle = nullptr;
  StaticText* spdReadout = nullptr;

  // bottom grid (3 columns x 2 rows)
  lv_obj_t*   gridFrame = nullptr;
  lv_obj_t*   gridOutline = nullptr;
  lv_point_t  gridPts[9] = {};
  StaticText* cellLabel[6] = {};
  StaticText* cellValue[6] = {};

  // cached values
  int   lastSats = -1;
  float lastBattV = -1;
  float lastAlt = -100000;
  float lastSpd = -100000;
  int   lastHdg = -1;
  int   lastPitch = 0;
  int   lastRoll = 0;
  int   lastHomeAngle = -1;
  int   lastCells[6] = {-100000, -100000, -100000, -100000, -100000, -100000};
};
