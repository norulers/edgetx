/*
 * Yaapu-style telemetry dashboard for EdgeTX C++
 * Supports ArduPilot (yaapu) and INAV (OpenTX-Telemetry-Widget) controllers
 */

#include "view_telemetry_dash.h"
#include "edgetx.h"
#include "etx_lv_theme.h"
#include "mainwindow.h"
#include "static.h"
#include "telemetry/telemetry.h"
#include "timers.h"
#include "rtc.h"
#include "hal/adc_driver.h"
#include <cmath>
#include <cstring>

// Default to ArduPilot; change via menu or config
TelemetryController TelemetryDashViewMenu::controllerType = CONTROLLER_ARDUPILOT;

// Sensor name mapping: tries ArduPilot name first, then INAV fallback
int TelemetryDashViewMenu::findMappedSensor(const SensorMap& map) const
{
  int idx = findSensor(map.ardupilot);
  if (idx >= 0) return idx;
  if (map.inav) {
    idx = findSensor(map.inav);
    if (idx >= 0) return idx;
  }
  return -1;
}

int TelemetryDashViewMenu::findSensor(const char* name) const
{
  for (int i = 0; i < MAX_TELEMETRY_SENSORS; i++) {
    if (!telemetryItems[i].isFresh()) continue;
    if (strncmp(g_model.telemetrySensors[i].label, name, TELEM_LABEL_LEN) == 0)
      return i;
  }
  return -1;
}

float TelemetryDashViewMenu::getSensorValue(int idx) const
{
  if (idx < 0) return -9999;
  auto& sc = g_model.telemetrySensors[idx];
  int32_t raw = telemetryItems[idx].value;
  float scale = 1.0f;
  for (int p = 0; p < sc.prec; p++) scale *= 0.1f;
  return raw * scale;
}

TelemetryDashViewMenu::TelemetryDashViewMenu() :
    NavWindow(MainWindow::instance(), {0, 0, LCD_W, LCD_H})
{
  pushLayer();
  // yaapu dark theme background (~RGB 20,20,20)
  lv_color_t bgColor = lv_color_make(0x14, 0x14, 0x14);
  lv_obj_set_style_bg_color(lvobj, bgColor, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(lvobj, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(lvobj, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(lvobj, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(lvobj, 0, LV_PART_MAIN);
  setWindowFlag(NO_FOCUS);
  setWindowFlag(NO_SCROLL);
  lv_obj_clear_flag(lvobj, LV_OBJ_FLAG_SCROLLABLE);

  // Add to default group to receive encoder/scroll events
  lv_group_add_obj(lv_group_get_default(), lvobj);
  lv_group_set_editing(lv_group_get_default(), true);
  // Handle encoder scroll events
  lv_obj_add_event_cb(lvobj, [](lv_event_t* e) {
    auto* self = (TelemetryDashViewMenu*)lv_event_get_user_data(e);
    uint32_t key = lv_event_get_key(e);
    if (self->menuActive && (key == LV_KEY_LEFT || key == LV_KEY_RIGHT)) {
      self->controllerType = (self->controllerType == CONTROLLER_ARDUPILOT)
                           ? CONTROLLER_INAV : CONTROLLER_ARDUPILOT;
    }
  }, LV_EVENT_KEY, this);

  buildUI();
}

TelemetryDashViewMenu::~TelemetryDashViewMenu()
{
  if (hudBuf) { free(hudBuf); hudBuf = nullptr; }
}

void TelemetryDashViewMenu::buildUI()
{
  buildTopBar();
  buildCenterHUD();
  buildLeftPanel();
  buildRightPanel();
  buildStatusBar();

  // Controller selection menu overlay (hidden by default)
  menuText = new StaticText(this, {60, 255, LCD_W - 120, 50}, "",
                            COLOR_THEME_QM_FG_INDEX, FONT(BOLD));
  lv_obj_set_style_text_align(menuText->getLvObj(), LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_color(menuText->getLvObj(), lv_color_white(), LV_PART_MAIN);
  lv_obj_add_flag(menuText->getLvObj(), LV_OBJ_FLAG_HIDDEN);
}

void TelemetryDashViewMenu::buildTopBar()
{
  // matches yaapu layoutlib.drawTopBar(): black bar (0,0,480,18)
  // yaapu shows: clock | RSSI | TX voltage
  constexpr coord_t H = 18;
  lv_obj_t* bar = lv_obj_create(lvobj);
  lv_obj_set_pos(bar, 0, 0);
  lv_obj_set_size(bar, LCD_W, H);
  lv_obj_set_style_radius(bar, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar, lv_color_make(0, 0, 0), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);

  // Clock at far right (yaapu: LCD_W right-aligned)
  clockLabel = new StaticText(this, {LCD_W - 68, 0, 64, H},
                              "00:00:00", COLOR_THEME_QM_FG_INDEX, FONT(XS));
  lv_obj_set_style_text_align(clockLabel->getLvObj(), LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
  lv_obj_set_style_text_color(clockLabel->getLvObj(), lv_color_white(), LV_PART_MAIN);

  // TX voltage at x=391 (yaapu: lcd.drawText(391,0,...))
  txVoltLabel = new StaticText(this, {LCD_W - 178, 0, 105, H},
                               "TX ---V", COLOR_THEME_QM_FG_INDEX, FONT(XS));
  lv_obj_set_style_text_align(txVoltLabel->getLvObj(), LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
  lv_obj_set_style_text_color(txVoltLabel->getLvObj(), lv_color_white(), LV_PART_MAIN);

  // RSSI (yaapu: drawRssi around x=300)
  rssiLabel = new StaticText(this, {LCD_W - 305, 0, 120, H},
                             "RSSI ---", COLOR_THEME_QM_FG_INDEX, FONT(XS));
  lv_obj_set_style_text_align(rssiLabel->getLvObj(), LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
  lv_obj_set_style_text_color(rssiLabel->getLvObj(), lv_color_white(), LV_PART_MAIN);
}

void TelemetryDashViewMenu::buildCenterHUD()
{
  hudCanvas = lv_canvas_create(lvobj);
  lv_obj_set_pos(hudCanvas, HUD_X, HUD_Y);
  lv_obj_set_size(hudCanvas, HUD_W, HUD_H);
  hudBuf = (uint8_t*)malloc(HUD_W * HUD_H * sizeof(lv_color_t));
  if (!hudBuf) return;
  memset(hudBuf, 0, HUD_W * HUD_H * sizeof(lv_color_t));
  lv_canvas_set_buffer(hudCanvas, hudBuf, HUD_W, HUD_H, LV_IMG_CF_TRUE_COLOR);

  hudSpeedLabel = new StaticText(this, {HUD_X + 40, HUD_Y + 115, 160, 32},
                                 "---", COLOR_THEME_QM_FG_INDEX, FONT(BOLD));
  lv_obj_set_style_text_align(hudSpeedLabel->getLvObj(), LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_color(hudSpeedLabel->getLvObj(),
                              lv_color_make(0x00, 0xFF, 0x80), LV_PART_MAIN);
  drawHUD();
}

void TelemetryDashViewMenu::drawHUD()
{
  if (!hudBuf) return;
  auto* buf = (lv_color_t*)hudBuf;
  for (int i = 0; i < HUD_W * HUD_H; i++) buf[i] = lv_color_black();
  drawArtificialHorizon(buf, lastPitch, lastRoll);
  drawCompassRibbon(buf, lastHdg >= 0 ? lastHdg : 0);
  if (lastHomeAngle >= 0)
    drawHomeArrow(buf, lastHdg >= 0 ? lastHdg : 0, lastHomeAngle);

  int armIdx = findMappedSensor({"Arm", nullptr});
  bool armed = (armIdx >= 0) ? (getSensorValue(armIdx) > 0.5f) : false;
  if (armIdx < 0) armed = (channelOutputs[4] > 0);
  lv_color_t armColor = armed ? lv_color_make(0x00, 0xFF, 0x00)
                               : lv_color_make(0xFF, 0x30, 0x30);
  int armY = 20, armW = 60, armH = 6;
  for (int dy = 0; dy < armH; dy++)
    for (int dx = -armW/2; dx < armW/2; dx++) {
      int ix = HUD_W/2 + dx, iy = armY + dy;
      if (ix >= 2 && ix < HUD_W-2 && iy >= 0 && iy < HUD_H)
        buf[iy * HUD_W + ix] = armColor;
    }

  int cx = HUD_W / 2, cy = HUD_H / 2;
  for (int x = cx - 10; x <= cx + 10; x++)
    if (x >= 0 && x < HUD_W && cy >= 0 && cy < HUD_H)
      buf[cy * HUD_W + x] = lv_color_white();
  for (int y = cy - 10; y <= cy + 10; y++)
    if (y >= 0 && y < HUD_H && cx >= 0 && cx < HUD_W)
      buf[y * HUD_W + cx] = lv_color_white();

  lv_obj_invalidate(hudCanvas);
}

void TelemetryDashViewMenu::drawArtificialHorizon(lv_color_t* buf, int pitch, int roll)
{
  lv_color_t colorSky = lv_color_make(0x08, 0x18, 0x30);
  lv_color_t colorGround = lv_color_make(0x30, 0x18, 0x08);
  lv_color_t colorLine = lv_color_make(0xC0, 0xC0, 0xC0);
  int cw = HUD_W / 2, ch = HUD_H / 2;
  float pitchOff = (float)pitch * 1.85f;
  if (pitchOff > ch) pitchOff = ch;
  if (pitchOff < -ch) pitchOff = -ch;
  int splitY = ch + (int)pitchOff;

  float rollRad = (float)roll * M_PI / 180.0f;
  float cosR = cosf(-rollRad);
  float sinR = sinf(-rollRad);

  for (int y = 0; y < HUD_H; y++) {
    for (int x = 0; x < HUD_W; x++) {
      float dx = (float)(x - cw);
      float dy = (float)(y - ch);
      float ry = dx * sinR + dy * cosR;
      int srcY = ch + (int)ry;
      buf[y * HUD_W + x] = (srcY < splitY) ? colorSky : colorGround;
    }
  }

  // Pitch ladder lines (drawn in rotated space)
  const int LS = 12;
  for (int i = -6; i <= 6; i++) {
    if (i == 0) continue;
    int lineY = splitY + i * LS;
    if (lineY < 2 || lineY > HUD_H - 2) continue;
    int lineW = (abs(i) % 5 == 0) ? 80 : 40;
    for (int lx = cw - lineW / 2; lx <= cw + lineW / 2; lx++) {
      float dx = (float)(lx - cw);
      float dy = (float)(lineY - ch);
      float rx = dx * cosR + dy * sinR;
      float ry = -dx * sinR + dy * cosR;
      int px = cw + (int)rx;
      int py = ch + (int)ry;
      if (px >= 2 && px < HUD_W - 2 && py >= 0 && py < HUD_H)
        buf[py * HUD_W + px] = colorLine;
    }
  }

  // Horizon line
  for (int x = 4; x < HUD_W - 4; x++) {
    float dx = (float)(x - cw);
    float dy = (float)(splitY - ch);
    float rx = dx * cosR + dy * sinR;
    float ry = -dx * sinR + dy * cosR;
    int px = cw + (int)rx;
    int py = ch + (int)ry;
    if (px >= 0 && px < HUD_W && py >= 0 && py < HUD_H) {
      buf[py * HUD_W + px] = colorLine;
      if (py + 1 >= 0 && py + 1 < HUD_H)
        buf[(py + 1) * HUD_W + px] = colorLine;
    }
  }
}

void TelemetryDashViewMenu::drawCompassRibbon(lv_color_t* buf, int yaw)
{
  static const char* pts[] = {
    "N", nullptr, "NE", nullptr, "E", nullptr, "SE", nullptr,
    "S", nullptr, "SW", nullptr, "W", nullptr, "NW", nullptr
  };
  const int RY = 2, BW = 40, BH = 18;
  lv_color_t ct = lv_color_make(0xC0, 0xC0, 0xC0);
  lv_color_t cc = lv_color_make(0xFF, 0xFF, 0x00);
  int cw = HUD_W / 2;
  int step = (HUD_W - 24) / 8;
  int chdg = (yaw / 225) * 225;
  int ti = ((chdg / 225) - 4) % 16;
  if (ti < 0) ti += 16;
  int tx = cw - 4 * step;

  for (int i = 0; i < 9; i++) {
    if (tx >= 2 && tx < HUD_W - 2) {
      if (pts[ti] == nullptr) {
        for (int dy = RY; dy < RY + 8; dy++)
          for (int dx = -1; dx <= 1; dx++) {
            int ix = tx + dx;
            if (ix >= 0 && ix < HUD_W && dy >= 0 && dy < HUD_H)
              buf[dy * HUD_W + ix] = ct;
          }
      } else {
        int ly = RY - 2;
        for (int dy = -2; dy <= 2; dy++)
          for (int dx = -2; dx <= 2; dx++) {
            int ix = tx + dx, iy = ly + dy;
            if (ix >= 0 && ix < HUD_W && iy >= 0 && iy < HUD_H)
              buf[iy * HUD_W + ix] = cc;
          }
      }
    }
    ti = (ti + 1) % 16;
    tx += step;
  }

  int bx = cw - BW / 2, by = RY - 1;
  for (int y = by; y < by + BH; y++)
    for (int x = bx; x < bx + BW; x++)
      if (x >= 0 && x < HUD_W && y >= 0 && y < HUD_H)
        buf[y * HUD_W + x] = (x == bx || x == bx + BW - 1 || y == by || y == by + BH - 1)
          ? lv_color_white() : lv_color_black();
}

void TelemetryDashViewMenu::drawHomeArrow(lv_color_t* buf, int yaw, int homeAngle)
{
  int cx = HUD_W / 2, cy = HUD_H / 2, r = 62;
  float a = (float)(homeAngle - yaw) * M_PI / 180.0f;
  int ax = (int)(cx + sinf(a) * r), ay = (int)(cy - cosf(a) * r);
  for (int dy = -4; dy <= 4; dy++)
    for (int dx = -4; dx <= 4; dx++)
      if (abs(dx) + abs(dy) <= 5) {
        int ix = ax + dx, iy = ay + dy;
        if (ix >= 0 && ix < HUD_W && iy >= 0 && iy < HUD_H)
          buf[iy * HUD_W + ix] = lv_color_make(0xFF, 0xA0, 0x00);
      }
}

void TelemetryDashViewMenu::buildLeftPanel()
{
  // matches yaapu left_wp_def.lua: x=0, y=18
  // GALT/RNG label at (10,14), value at (10,23)
  galtRngLabel = new StaticText(this, {2, 14, 115, 14}, "GALT m",
                               COLOR_THEME_QM_FG_INDEX, FONT(XS));
  lv_obj_set_style_text_color(galtRngLabel->getLvObj(), lv_color_make(0x8C, 0x8C, 0x8C), LV_PART_MAIN);

  galtRngValue = new StaticText(this, {2, 23, 115, 26}, "---",
                               COLOR_THEME_QM_FG_INDEX, FONT(STD));
  lv_obj_set_style_text_color(galtRngValue->getLvObj(), lv_color_white(), LV_PART_MAIN);

  // HOME-TRAVEL label at (10,49)
  StaticText* htLabel = new StaticText(this, {2, 49, 115, 14}, "HOME-TRAVEL",
                                       COLOR_THEME_QM_FG_INDEX, FONT(XS));
  lv_obj_set_style_text_color(htLabel->getLvObj(), lv_color_make(0x8C, 0x8C, 0x8C), LV_PART_MAIN);

  // HOME distance at (10,58)
  homeDistLabel = new StaticText(this, {2, 58, 115, 22}, "---",
                                 COLOR_THEME_QM_FG_INDEX, FONT(STD));
  lv_obj_set_style_text_color(homeDistLabel->getLvObj(), lv_color_make(0xFF, 0xD0, 0x00), LV_PART_MAIN);

  // Total distance at (10,80)
  totalDistLabel = new StaticText(this, {2, 78, 115, 16}, "0.00km",
                                  COLOR_THEME_QM_FG_INDEX, FONT(XS));
  lv_obj_set_style_text_color(totalDistLabel->getLvObj(), lv_color_white(), LV_PART_MAIN);
}

void TelemetryDashViewMenu::buildRightPanel()
{
  // matches yaapu right_def.lua: x=360, y=18, width=120
  constexpr coord_t X = 360;
  lv_color_t colorLabel = lv_color_make(0x8C, 0x8C, 0x8C);

  // "CELL" label right-aligned at (470,16)
  lv_obj_t* cellLbl = lv_label_create(lvobj);
  lv_label_set_text(cellLbl, "CELL");
  lv_obj_set_style_text_color(cellLbl, colorLabel, LV_PART_MAIN);
  lv_obj_set_style_text_font(cellLbl, getFont(FONT(XS)), LV_PART_MAIN);
  lv_obj_set_pos(cellLbl, X + 110, 16);
  lv_obj_set_style_text_align(cellLbl, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);

  // Cell voltage value at (468,28) DBLSIZE equivalent
  cellVoltLabel = new StaticText(this, {X, 28, 116, 30}, "0.00",
                                 COLOR_THEME_QM_FG_INDEX, FONT(BOLD));
  lv_obj_set_style_text_align(cellVoltLabel->getLvObj(), LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
  lv_obj_set_style_text_color(cellVoltLabel->getLvObj(), lv_color_white(), LV_PART_MAIN);

  // "V" unit label at (468,45)
  lv_obj_t* vLbl = lv_label_create(lvobj);
  lv_label_set_text(vLbl, "V");
  lv_obj_set_style_text_color(vLbl, colorLabel, LV_PART_MAIN);
  lv_obj_set_style_text_font(vLbl, getFont(FONT(XS)), LV_PART_MAIN);
  lv_obj_set_pos(vLbl, X + 112, 45);
  lv_obj_set_style_text_align(vLbl, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);

  // "BATT" label at (406,64) / "CURR" label at (468,64)
  lv_obj_t* battLbl = lv_label_create(lvobj);
  lv_label_set_text(battLbl, "BATT");
  lv_obj_set_style_text_color(battLbl, colorLabel, LV_PART_MAIN);
  lv_obj_set_style_text_font(battLbl, getFont(FONT(XS)), LV_PART_MAIN);
  lv_obj_set_pos(battLbl, X + 46, 64);
  lv_obj_set_style_text_align(battLbl, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);

  // Batt voltage value at (402,86)
  battVoltLabel = new StaticText(this, {X, 86, 60, 22}, "0.0",
                                 COLOR_THEME_QM_FG_INDEX, FONT(STD));
  lv_obj_set_style_text_align(battVoltLabel->getLvObj(), LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
  lv_obj_set_style_text_color(battVoltLabel->getLvObj(), lv_color_white(), LV_PART_MAIN);

  // "CURR" label at (468,64)
  lv_obj_t* currLbl = lv_label_create(lvobj);
  lv_label_set_text(currLbl, "CURR");
  lv_obj_set_style_text_color(currLbl, colorLabel, LV_PART_MAIN);
  lv_obj_set_style_text_font(currLbl, getFont(FONT(XS)), LV_PART_MAIN);
  lv_obj_set_pos(currLbl, X + 110, 64);
  lv_obj_set_style_text_align(currLbl, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);

  // Current value at (468,73)
  currLabel = new StaticText(this, {X, 73, 116, 22}, "0A",
                             COLOR_THEME_QM_FG_INDEX, FONT(BOLD));
  lv_obj_set_style_text_align(currLabel->getLvObj(), LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
  lv_obj_set_style_text_color(currLabel->getLvObj(), lv_color_white(), LV_PART_MAIN);

  // Batt% bar at (365,108,110,21)
  battBarBg = lv_obj_create(lvobj);
  lv_obj_set_pos(battBarBg, X + 5, 108);
  lv_obj_set_size(battBarBg, 110, 21);
  lv_obj_set_style_radius(battBarBg, 2, LV_PART_MAIN);
  lv_obj_set_style_border_width(battBarBg, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(battBarBg, LV_OPA_30, LV_PART_MAIN);
  lv_obj_set_style_bg_color(battBarBg, lv_color_white(), LV_PART_MAIN);

  battBarFill = lv_obj_create(lvobj);
  lv_obj_set_pos(battBarFill, X + 5, 108);
  lv_obj_set_size(battBarFill, 0, 21);
  lv_obj_set_style_radius(battBarFill, 2, LV_PART_MAIN);
  lv_obj_set_style_border_width(battBarFill, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(battBarFill, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(battBarFill, lv_color_make(0x00, 0xFF, 0x80), LV_PART_MAIN);

  // Capacity at (470,130) right-aligned
  capaLabel = new StaticText(this, {X, 130, 116, 16}, "0mAh",
                             COLOR_THEME_QM_FG_INDEX, FONT(XS));
  lv_obj_set_style_text_align(capaLabel->getLvObj(), LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
  lv_obj_set_style_text_color(capaLabel->getLvObj(), lv_color_white(), LV_PART_MAIN);
}

void TelemetryDashViewMenu::buildStatusBar()
{
  // matches yaapu drawStatusBar(msgRows=6): bg y=194 to 320
  constexpr coord_t SY = 194, SH = 126;
  lv_obj_t* sb = lv_obj_create(lvobj);
  lv_obj_set_pos(sb, 0, SY);
  lv_obj_set_size(sb, LCD_W, SH);
  lv_obj_set_style_radius(sb, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(sb, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_color(sb, lv_color_make(0x08, 0x08, 0x08), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(sb, LV_OPA_COVER, LV_PART_MAIN);

  // GPS coords at y=192 (yaapu: absolute 275-60-23=192)
  gpsLabel = new StaticText(this, {150, 192, LCD_W - 160, 22}, "",
                            COLOR_THEME_QM_FG_INDEX, FONT(STD));
  lv_obj_set_style_text_align(gpsLabel->getLvObj(), LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
  lv_obj_set_style_text_color(gpsLabel->getLvObj(), lv_color_make(0x80, 0x80, 0x80), LV_PART_MAIN);

  // Timer at y=212 (yaapu: 272-60=212)
  timerLabel = new StaticText(this, {LCD_W - 110, 212, 106, 26},
                              "00:00", COLOR_THEME_QM_FG_INDEX, FONT(BOLD));
  lv_obj_set_style_text_align(timerLabel->getLvObj(), LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
  lv_obj_set_style_text_color(timerLabel->getLvObj(), lv_color_white(), LV_PART_MAIN);

  // FM at y=218 (yaapu: 278-60=218)
  fmLabel = new StaticText(this, {2, 218, 140, 22}, "",
                           COLOR_THEME_QM_FG_INDEX, FONT(STD));
  lv_obj_set_style_text_color(fmLabel->getLvObj(), lv_color_white(), LV_PART_MAIN);

  // Sats+HDOP at y=274
  satsLabel = new StaticText(this, {145, 274, 110, 22}, "---",
                             COLOR_THEME_QM_FG_INDEX, FONT(BOLD));
  lv_obj_set_style_text_color(satsLabel->getLvObj(), lv_color_make(0xC0, 0xC0, 0xC0), LV_PART_MAIN);
}

void TelemetryDashViewMenu::checkEvents()
{
  NavWindow::checkEvents();
  if (deleted()) return;

  // Update controller selection menu overlay
  if (menuActive) {
    lv_obj_clear_flag(menuText->getLvObj(), LV_OBJ_FLAG_HIDDEN);
    if (controllerType == CONTROLLER_ARDUPILOT) {
      menuText->setText("[ArduPilot (yaapu)]\n  INAV (iNav)");
    } else {
      menuText->setText("  ArduPilot (yaapu)\n[INAV (iNav)]");
    }
  } else {
    lv_obj_add_flag(menuText->getLvObj(), LV_OBJ_FLAG_HIDDEN);
  }

  updateValues();
}

void TelemetryDashViewMenu::onCancel()
{
  deleteLater();
}

void TelemetryDashViewMenu::onClicked()
{
  // ENT key: toggle controller selection menu
  menuActive = !menuActive;
}

void TelemetryDashViewMenu::onEvent(event_t event)
{
#if defined(HARDWARE_KEYS)
  if (event == EVT_KEY_BREAK(KEY_EXIT) || event == EVT_KEY_LONG(KEY_EXIT)) {
    if (menuActive) { menuActive = false; return; }
    deleteLater();
    return;
  }
#endif
  NavWindow::onEvent(event);
}

#if defined(HARDWARE_KEYS)
void TelemetryDashViewMenu::onLongPressRTN()
{
  deleteLater();
}
#endif

void TelemetryDashViewMenu::updateValues()
{
  char s[64];
  int idx;
  bool hudChanged = false;

  float txV = getBatteryVoltage() * 0.01f;
  if (fabsf(txV - lastTxV) > 0.05f) {
    lastTxV = txV;
    snprintf(s, sizeof(s), "%.1fV", txV);
    txVoltLabel->setText(s);
  }

  // Clock (yaapu: HH:MM:SS)
  struct gtm tm;
  gettime(&tm);
  snprintf(s, sizeof(s), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
  clockLabel->setText(s);

  int rssi = TELEMETRY_RSSI();
  if (rssi != lastRssi) {
    lastRssi = rssi;
    snprintf(s, sizeof(s), "%ddB", rssi);
    rssiLabel->setText(s);
  }

  // HUD sensors
  idx = findSensor("Hdg");
  int hdg = idx >= 0 ? (int)getSensorValue(idx) : -1;
  if (hdg != lastHdg) { lastHdg = hdg; hudChanged = true; }

  idx = findSensor("Pitch");
  int pitch = idx >= 0 ? (int)getSensorValue(idx) : 0;
  if (pitch != lastPitch) { lastPitch = pitch; hudChanged = true; }

  idx = findSensor("Roll");
  int roll = idx >= 0 ? (int)getSensorValue(idx) : 0;
  if (roll != lastRoll) { lastRoll = roll; hudChanged = true; }

  // Home angle: try "Homes" first, then fallback to calculated degree sensor
  idx = findSensor("Homes");
  if (idx < 0) {
    for (int i = 0; i < MAX_TELEMETRY_SENSORS; i++) {
      if (!telemetryItems[i].isFresh()) continue;
      if (g_model.telemetrySensors[i].unit == UNIT_DEGREE
          && g_model.telemetrySensors[i].type == TELEM_TYPE_CALCULATED)
        { idx = i; break; }
    }
  }
  int ha = idx >= 0 ? (int)getSensorValue(idx) : -1;
  if (ha != lastHomeAngle) { lastHomeAngle = ha; hudChanged = true; }

  if (hudChanged) drawHUD();

  // Left panel: GALT/RNG
  idx = findSensor("GAlt");
  float galt = idx >= 0 ? getSensorValue(idx) : -9999;
  if (fabsf(galt - lastGAlt) > 0.5f) {
    lastGAlt = galt;
    if (galt > -9990) {
      snprintf(s, sizeof(s), "%.0f", galt);
      galtRngValue->setText(s);
    }
  }

  // Left panel: HOME-TRAVEL
  idx = findSensor("Dist");
  if (idx < 0) {
    for (int i = 0; i < MAX_TELEMETRY_SENSORS; i++) {
      if (!telemetryItems[i].isFresh()) continue;
      if (g_model.telemetrySensors[i].unit == UNIT_METERS
          && g_model.telemetrySensors[i].type == TELEM_TYPE_CALCULATED)
        { idx = i; break; }
    }
  }
  float dist = idx >= 0 ? getSensorValue(idx) : -1;
  if (fabsf(dist - lastHomeDist) > 0.5f) {
    lastHomeDist = dist;
    if (dist >= 0) {
      if (dist < 999) snprintf(s, sizeof(s), "%.0fm", dist);
      else snprintf(s, sizeof(s), "%.2fkm", dist * 0.001f);
      homeDistLabel->setText(s);
    }
  }

  // HUD speed + total distance accumulation
  idx = findSensor("GSpd");
  float gspd = idx >= 0 ? getSensorValue(idx) : -1;
  if (fabsf(gspd - lastGSpd) > 0.5f) {
    lastGSpd = gspd;
    if (gspd >= 0) {
      snprintf(s, sizeof(s), "%.0f", gspd);
      hudSpeedLabel->setText(s);
      if (gspd > 0.5f) lastTotalDist += gspd * 0.1f / 3600.0f;
    }
  }

  // Total distance display (updated every cycle for smoothness)
  if (lastTotalDist >= 0.005f) {
    snprintf(s, sizeof(s), "%.2fkm", lastTotalDist);
    totalDistLabel->setText(s);
  }

  // Right panel: CELL (minimum cell voltage)
  idx = findSensor("Cels");
  float cellMin = -1;
  if (idx >= 0) {
    auto& cells = telemetryItems[idx].cells;
    for (int i = 0; i < cells.count && i < MAX_CELLS; i++) {
      float cv = cells.values[i].value * 0.01f;
      if (cellMin < 0 || cv < cellMin) cellMin = cv;
    }
  }
  if (fabsf(cellMin - lastCell) > 0.01f) {
    lastCell = cellMin;
    if (cellMin > 0) { snprintf(s, sizeof(s), "%.2f", cellMin); cellVoltLabel->setText(s); }
  }

  // Right panel: BATT (VFAS)
  idx = findSensor("VFAS");
  if (idx < 0) {
    for (int i = 0; i < MAX_TELEMETRY_SENSORS; i++) {
      if (!telemetryItems[i].isFresh()) continue;
      if (g_model.telemetrySensors[i].unit == UNIT_VOLTS
          && strncmp(g_model.telemetrySensors[i].label, "RxBt", 4) != 0) {
        float v = getSensorValue(i);
        if (v > 5.0f) { idx = i; break; }
      }
    }
  }
  float battV = idx >= 0 ? getSensorValue(idx) : -1;
  if (fabsf(battV - lastBattV) > 0.05f) {
    lastBattV = battV;
    if (battV > 0) { snprintf(s, sizeof(s), "%.1f", battV); battVoltLabel->setText(s); }
  }

  // Right panel: CURR
  idx = findSensor("Curr");
  float curr = idx >= 0 ? getSensorValue(idx) : -1;
  if (fabsf(curr - lastCurr) > 0.1f) {
    lastCurr = curr;
    if (curr >= 0) {
      snprintf(s, sizeof(s), curr < 10 ? "%.1f" : "%.0f", curr);
      currLabel->setText(s);
    }
  }

  // Right panel: Batt% bar (110px wide)
  // ArduPilot: "Bat%"  /  INAV: "Fuel"
  idx = findMappedSensor({"Bat%", "Fuel"});
  int battPct = idx >= 0 ? (int)getSensorValue(idx) : -1;
  if (battPct != lastBattPct) {
    lastBattPct = battPct;
    if (battPct >= 0) {
      int bw = battPct * 110 / 100;
      if (bw < 0) bw = 0;
      if (bw > 110) bw = 110;
      lv_obj_set_width(battBarFill, bw);
      lv_color_t bc = battPct > 50 ? lv_color_make(0x00, 0xFF, 0x00)
                    : battPct > 25 ? lv_color_make(0xFF, 0xCC, 0x00)
                                   : lv_color_make(0xFF, 0x00, 0x00);
      lv_obj_set_style_bg_color(battBarFill, bc, LV_PART_MAIN);
    }
  }

  // Right panel: Capa
  idx = findSensor("Capa");
  float capa = idx >= 0 ? getSensorValue(idx) : -1;
  if (fabsf(capa - lastCapa) > 10) {
    lastCapa = capa;
    if (capa > 0) {
      if (capa < 1000) snprintf(s, sizeof(s), "%.0fmAh", capa);
      else snprintf(s, sizeof(s), "%.2fAh", capa * 0.001f);
      capaLabel->setText(s);
    }
  }

  // Status bar: FM
  idx = findSensor("FM");
  if (idx >= 0 && telemetryItems[idx].isFresh()) {
    const char* fmText = telemetryItems[idx].text;
    if (fmText && strncmp(fmText, lastFM, sizeof(lastFM) - 1) != 0) {
      strncpy(lastFM, fmText, sizeof(lastFM) - 1);
      lastFM[sizeof(lastFM) - 1] = '\0';
      fmLabel->setText(lastFM);
    }
  }

  // Status bar: Timer
#if defined(TIMERS)
  int tsec = timersStates[0].val;
  if (tsec != lastTimerSec) {
    lastTimerSec = tsec;
    int min = tsec / 60, sec = tsec % 60;
    snprintf(s, sizeof(s), "%02d:%02d", min, sec);
    timerLabel->setText(s);
  }
#endif

  // Status bar: Sats + HDOP (combined)
  idx = findSensor("Sats");
  int sats = idx >= 0 ? (int)getSensorValue(idx) : -1;
  int hdIdx = findSensor("HDOP");
  float hdop = hdIdx >= 0 ? getSensorValue(hdIdx) : -1;
  if (sats != lastSats || fabsf(hdop - lastHdop) > 0.05f) {
    lastSats = sats; lastHdop = hdop;
    if (sats >= 0) {
      if (hdop >= 0) snprintf(s, sizeof(s), "%d  %.1f", sats, hdop * 0.1f);
      else snprintf(s, sizeof(s), "%d", sats);
      satsLabel->setText(s);
    }
  }

  // Status bar: GPS coords single line (lat lon)
  int latIdx = -1;
  for (int i = 0; i < MAX_TELEMETRY_SENSORS; i++) {
    if (!telemetryItems[i].isFresh()) continue;
    if (g_model.telemetrySensors[i].unit == UNIT_GPS_LATITUDE)
      { latIdx = i; break; }
  }
  if (latIdx >= 0) {
    int32_t lat = telemetryItems[latIdx].gps.latitude;
    int32_t lon = telemetryItems[latIdx].gps.longitude;
    float flat = lat / 10000000.0f, flon = lon / 10000000.0f;
    if (fabsf(flat - lastLat) > 0.0001f || fabsf(flon - lastLon) > 0.0001f) {
      lastLat = flat; lastLon = flon;
      snprintf(s, sizeof(s), "%.6f  %.6f", flat, flon);
      gpsLabel->setText(s);
    }
  }
}
