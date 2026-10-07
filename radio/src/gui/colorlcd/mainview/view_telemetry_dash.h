/*
 * Yaapu-style telemetry dashboard for EdgeTX C++ (screen content only).
 *
 * Layout, design space 480x320, uniformly scaled to the actual LCD:
 *
 *   y  12.. 56  [sat] flight mode / SAT: n        [lq bars] n% / [batt] txV
 *   y  57       tan rules that bend down into the roll scale
 *   y  12..236  circular horizon dial: roll scale, pitch ladder, aircraft
 *               symbol and a half compass rose
 *   y  61..191  speed tape (left) / altitude-above-home tape (right), flanked
 *               by decorative mesh "wings"
 *   y 178..211  readouts: speed (left), Home Alt: (right)
 *   y 218..310  chamfered 3x2 panel: SPD Dist BATT / RPM1 RPM2 CURR
 *
 * Telemetry sensors are matched by label, using the same names as the yaapu
 * script:
 *   top bar - FM: flight mode text, Sats: satellite count ("15+" at the 4 bit
 *           passthrough limit), RQly: uplink link quality (bars + %) and the
 *           transmitter battery voltage, its icon filled and coloured by the
 *           level (GET_TXBATT_BARS, as the FPV dashboard header)
 *   dial  - Ptch/Roll/Yaw (CRSF radians) converted to degrees and glided
 *           between samples; Hdg is yaapu's non-CRSF yaw alias, the home
 *           marker comes from the ArduPilot passthrough home frame
 *   tapes - ASpd (left, GSpd while the vehicle has no airspeed sensor) and the
 *           altitude above home from the ArduPilot passthrough home frame
 *           (right, m), both in km/h and m per 10 unit scale steps
 *   grid  - SPD (horizontal speed, ASpd when the vehicle has an airspeed
 *           sensor, GSpd otherwise, in km/h), Dist (distance to home from the
 *           passthrough home frame, m up to 999 then km), BATT (aircraft
 *           battery voltage, RxBt/VFAS), RPM/RPM2 (RPM1/RPM2 in the panel, from
 *           the ArduPilot passthrough 0x500A frame) and CURR (aircraft current);
 *           BATT and CURR carry their unit, SPD/BATT/CURR drop to integers
 *           from 10 up, as yaapu shows them
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
  // Vertical value tape: numbers every step, ticks every half step and a
  // centre box (own canvas) with the current value.
  struct Tape {
    bool        left = true;
    coord_t     x = 0, y = 0, w = 0, h = 0;
    coord_t     numX = 0, numW = 0, tickX = 0, centerY = 0, stepPx = 0;
    StaticText* num[5] = {};
    lv_obj_t*   tick[9] = {};
    StaticText* boxValue = nullptr;
  };

  static constexpr int MAX_BUFS = 8;

  // The last two samples of one attitude angle, used to glide the drawn value
  // between the few frames per second the sensors send.
  struct AngleGlide {
    float    prev = 0.0f, cur = 0.0f;
    uint32_t prevMs = 0, curMs = 0;
    bool     seen = false;
  };

  void buildUI();
  void buildTopBar();
  void buildWings();
  void buildDial();
  void buildTape(Tape& t, bool left, bool altitude);
  void buildReadouts();
  void buildGrid();

  lv_obj_t* makeCanvas(coord_t x, coord_t y, coord_t w, coord_t h, bool alpha,
                       uint8_t*& buf);
  StaticText* makeText(coord_t x, coord_t capCenterY, coord_t w,
                       const char* txt, LcdFlags font, uint32_t color,
                       lv_text_align_t align);
  lv_obj_t* makeRect(coord_t x, coord_t y, coord_t w, coord_t h,
                     uint32_t color);
  lv_obj_t* makeLine(lv_point_t* pts, uint16_t n, coord_t width,
                     uint32_t color);

  void updateValues();
  void updateTape(Tape& t, float value, float step, bool valid, int dec = 0);
  void setBoxValue(Tape& t, const char* txt);
  void drawDial();
  void drawBatteryIcon(uint32_t color, int pct);

  // telemetry helpers
  int   findSensor(const char* name) const;
  int   findMappedSensor(const char* primary, const char* secondary) const;
  int   findForController(const char* apName, const char* inavName) const;
  float getSensorValue(int idx) const;
  float getSensorValueIn(int idx, uint8_t dstUnit, uint8_t dstPrec) const;
  float getAngleDegrees(int idx) const;
  float glideAngle(AngleGlide& g, float sample, uint32_t nowMs, bool heading);

  static TelemetryController controllerType;
  bool        menuActive = false;
  StaticText* menuText = nullptr;

  // canvas pixel buffers, released in the destructor
  uint8_t*    bufs[MAX_BUFS] = {};
  int         bufCount = 0;

  // top bar
  StaticText* satValue = nullptr;
  StaticText* flightModeText = nullptr;
  lv_obj_t*   lqBar[5] = {};  // link quality signal bars (LQ_BARS_*)
  StaticText* lqText = nullptr;
  StaticText* battValue = nullptr;
  lv_obj_t*   battIconCanvas = nullptr;
  uint8_t*    battIconBuf = nullptr;
  int         battIconPct = -1;
  lv_point_t  rulePts[2][3] = {};

  // centre dial
  lv_obj_t*   dialCanvas = nullptr;
  uint8_t*    dialBuf = nullptr;

  // tapes / readouts
  Tape        spdTape;
  Tape        homeAltTape;
  StaticText* spdReadout = nullptr;
  StaticText* homeAltReadout = nullptr;

  // bottom grid (3 columns x 2 rows)
  lv_point_t  gridPts[9] = {};
  StaticText* cellValue[6] = {};

  // cached values; the lastXxx angles hold the values actually drawn
  uint32_t lastEaseMs = 0;  // 0 = first update, snap to the sensor values
  AngleGlide glideHdg, glidePitch, glideRoll, glideHome;
  int   lastSats = -1;
  int   lastLq = -1;
  int   lastVbat = -1;
  float lastSpd = -100000;
  float lastHomeAlt = -100000;
  float lastHdg = -1;
  float lastPitch = 0;
  float lastRoll = 0;
  float lastHomeAngle = -1;
  int   lastCells[6] = {-100000, -100000, -100000, -100000, -100000, -100000};
  uint8_t lastCellPrec[6] = {255, 255, 255, 255, 255, 255};
};
