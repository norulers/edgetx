/*
 * Yaapu-style telemetry dashboard for EdgeTX C++ (screen content only).
 * See view_telemetry_dash.h for the layout description.
 */

#include "view_telemetry_dash.h"
#include "edgetx.h"
#include "etx_lv_theme.h"
#include "mainwindow.h"
#include "static.h"
#include "bitmaps.h"
#include "telemetry/telemetry.h"
#include "timers.h"
#include "rtc.h"
#include "hal/adc_driver.h"
#include <cmath>
#include <cstring>

// Default to ArduPilot; toggled with the ENT key
TelemetryController TelemetryDashViewMenu::controllerType = CONTROLLER_ARDUPILOT;

//-----------------------------------------------------------------------------
// Geometry: everything is laid out in a 480x320 "design space" and scaled
// uniformly to the actual LCD, so other panel resolutions only need the
// design-space constants below adjusted.
//-----------------------------------------------------------------------------

namespace {

constexpr int DES_W = 480;
constexpr int DES_H = 320;

 // percent, 100 on a 480x320 panel
constexpr int SCL = ((LCD_W * 100 / DES_W) < (LCD_H * 100 / DES_H))
                        ? (LCD_W * 100 / DES_W)
                        : (LCD_H * 100 / DES_H);

constexpr coord_t S(int v) { return (coord_t)((v * SCL) / 100); }
constexpr coord_t SX(int v)
{
  return (coord_t)((v * SCL) / 100) + (LCD_W - (DES_W * SCL) / 100) / 2;
}
constexpr coord_t SY(int v)
{
  return (coord_t)((v * SCL) / 100) + (LCD_H - (DES_H * SCL) / 100) / 2;
}

constexpr uint32_t C_BG = 0x0B1219;      // page background (average of the mock's vignette)
constexpr uint32_t C_LABEL = 0x7FD2E4;   // cyan labels
constexpr uint32_t C_VALUE = 0xFFFFFF;
constexpr uint32_t C_ACCENT = 0xE8A23C;  // orange values
constexpr uint32_t C_RULE = 0x8A6238;    // tan rule under the top bar
constexpr uint32_t C_SKY = 0x30A4B8;
constexpr uint32_t C_GROUND = 0x84623F;
constexpr uint32_t C_GROUND_HI = 0xA66A33;
constexpr uint32_t C_GROUND_MID = 0x6B4A28;
constexpr uint32_t C_GROUND_FAR = 0x57391F;
constexpr uint32_t C_TAPE = 0x1C3E49;
constexpr uint32_t C_PANEL = 0x16202A;
constexpr uint32_t C_PANEL_EDGE = 0x2A3E4A;  // blue-grey panel outline
constexpr uint32_t C_DIV = 0x5A3A18;     // amber panel dividers
constexpr uint32_t C_BAND = 0x1B3A45;    // compass rose band
constexpr uint32_t C_RING = 0x78C9D2;    // cyan bezel around the dial
constexpr uint32_t C_RIM_DARK = 0x04161F;  // ring between the sky and the bezel
constexpr uint32_t C_RIM_GLOW1 = 0x307580; // teal glow outside the bezel
constexpr uint32_t C_RIM_GLOW2 = 0x16414C;
constexpr uint32_t C_HZN_SHADOW = 0x8A6440;  // shading under the horizon line
constexpr uint32_t C_BAND_EDGE = 0x0A0E12;   // dark ring inside the compass band

inline lv_color_t rgb(uint32_t v)
{
  return lv_color_make((uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v);
}

// top bar
constexpr int ICON_W = 32, ICON_H = 34, ICON_Y = 22;
constexpr int SAT_ICON_X = 20;
constexpr int SAT_TEXT_X = 62, TOP_TEXT_W = 150;
constexpr int BATT_ICON_X = DES_W - SAT_ICON_X - ICON_W;
constexpr int BATT_TEXT_X = 290, BATT_TEXT_W = 130;
constexpr int TOP_RULE_Y = 57;

// centre dial; the canvas adds HUD_PAD around the circle for the rim/roll scale
constexpr int HUD_CX = 240, HUD_CY = 128, HUD_R = 103, HUD_PAD = 18;
constexpr int HUD_W = 2 * (HUD_R + HUD_PAD);
constexpr int HUD_H = HUD_W;
constexpr int HUD_X = HUD_CX - HUD_W / 2;
constexpr int HUD_Y = HUD_CY - HUD_H / 2;
// the dial's lower rim overlaps the panel (as in the reference), so the part
// that falls inside the panel is drawn by a small transparent canvas on top
constexpr int HUD_SKIRT_Y = 222;
constexpr int HUD_SKIRT_H = 30;
constexpr int BAND_HALF = 42;  // compass band half width, degrees

// value tapes
constexpr int TAPE_W = 66, TAPE_H = 124, TAPE_Y = 58;
constexpr int TAPE_L_X = 50;
constexpr int TAPE_R_X = DES_W - 50 - TAPE_W;
constexpr int TAPE_BOX_H = 30;
constexpr int TAPE_STEP_PX = 30;

// readouts under the tapes
constexpr int RO_TITLE_Y = 184, RO_TITLE_H = 14;
constexpr int RO_VALUE_Y = 200, RO_VALUE_H = 22;

// bottom panel (3 columns x 2 rows)
constexpr int GRID_X = 25, GRID_Y = 222, GRID_W = 430, GRID_H = 96;

//-----------------------------------------------------------------------------
// Tiny software drawing helpers (all coordinates in canvas pixels)
//-----------------------------------------------------------------------------

inline void putPx(lv_color_t* buf, int w, int h, int x, int y, lv_color_t c)
{
  if (x < 0 || y < 0 || x >= w || y >= h) return;
  buf[y * w + x] = c;
}

void fillRect(lv_color_t* buf, int w, int h, int x0, int y0, int x1, int y1,
              lv_color_t c)
{
  if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
  if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++) putPx(buf, w, h, x, y, c);
}

void rectOutline(lv_color_t* buf, int w, int h, int x0, int y0, int x1, int y1,
                 lv_color_t c)
{
  fillRect(buf, w, h, x0, y0, x1, y0, c);
  fillRect(buf, w, h, x0, y1, x1, y1, c);
  fillRect(buf, w, h, x0, y0, x0, y1, c);
  fillRect(buf, w, h, x1, y0, x1, y1, c);
}

void drawLine(lv_color_t* buf, int w, int h, int x0, int y0, int x1, int y1,
              lv_color_t c, int t = 2)
{
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  while (true) {
    fillRect(buf, w, h, x0 - t / 2, y0 - t / 2, x0 + t / 2, y0 + t / 2, c);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

}  // namespace

//-----------------------------------------------------------------------------
// Telemetry lookup helpers
//-----------------------------------------------------------------------------

int TelemetryDashViewMenu::findSensor(const char* name) const
{
  if (!name) return -1;
  for (int i = 0; i < MAX_TELEMETRY_SENSORS; i++) {
    if (!telemetryItems[i].isFresh()) continue;
    if (strncmp(g_model.telemetrySensors[i].label, name, TELEM_LABEL_LEN) == 0)
      return i;
  }
  return -1;
}

int TelemetryDashViewMenu::findMappedSensor(const char* primary,
                                            const char* secondary) const
{
  int idx = findSensor(primary);
  if (idx >= 0) return idx;
  return secondary ? findSensor(secondary) : -1;
}

// Prefer the sensor name of the selected controller, fall back to the other
int TelemetryDashViewMenu::findForController(const char* apName,
                                             const char* inavName) const
{
  return (controllerType == CONTROLLER_INAV)
             ? findMappedSensor(inavName, apName)
             : findMappedSensor(apName, inavName);
}

float TelemetryDashViewMenu::getSensorValue(int idx) const
{
  if (idx < 0) return -100000;
  auto& sc = g_model.telemetrySensors[idx];
  float scale = 1.0f;
  for (int p = 0; p < sc.prec; p++) scale *= 0.1f;
  return telemetryItems[idx].value * scale;
}

// Value converted to the requested unit / precision (metric display)
float TelemetryDashViewMenu::getSensorValueIn(int idx, uint8_t dstUnit,
                                              uint8_t dstPrec) const
{
  if (idx < 0) return -100000;
  auto& sc = g_model.telemetrySensors[idx];
  if (sc.unit == dstUnit && sc.prec == dstPrec) return getSensorValue(idx);
  int32_t conv = convertTelemetryValue(telemetryItems[idx].value, sc.unit,
                                       sc.prec, dstUnit, dstPrec);
  float out = 1.0f;
  for (int p = 0; p < dstPrec; p++) out *= 0.1f;
  return conv * out;
}

//-----------------------------------------------------------------------------
// Construction
//-----------------------------------------------------------------------------

TelemetryDashViewMenu::TelemetryDashViewMenu() :
    NavWindow(MainWindow::instance(), {0, 0, LCD_W, LCD_H})
{
  pushLayer();
  lv_obj_set_style_bg_color(lvobj, rgb(C_BG), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(lvobj, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(lvobj, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(lvobj, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(lvobj, 0, LV_PART_MAIN);
  setWindowFlag(NO_FOCUS);
  setWindowFlag(NO_SCROLL);
  lv_obj_clear_flag(lvobj, LV_OBJ_FLAG_SCROLLABLE);

  // Receive encoder/scroll events for the controller selection menu
  lv_group_add_obj(lv_group_get_default(), lvobj);
  lv_group_set_editing(lv_group_get_default(), true);
  lv_obj_add_event_cb(
      lvobj,
      [](lv_event_t* e) {
        auto* self = (TelemetryDashViewMenu*)lv_event_get_user_data(e);
        uint32_t key = lv_event_get_key(e);
        if (self->menuActive && (key == LV_KEY_LEFT || key == LV_KEY_RIGHT)) {
          self->controllerType = (self->controllerType == CONTROLLER_ARDUPILOT)
                                     ? CONTROLLER_INAV
                                     : CONTROLLER_ARDUPILOT;
        }
      },
      LV_EVENT_KEY, this);

  buildUI();
}

TelemetryDashViewMenu::~TelemetryDashViewMenu()
{
  if (dialBuf) { free(dialBuf); dialBuf = nullptr; }
  if (skirtBuf) { free(skirtBuf); skirtBuf = nullptr; }
  if (satIconBuf) { free(satIconBuf); satIconBuf = nullptr; }
  if (battIconBuf) { free(battIconBuf); battIconBuf = nullptr; }
}

void TelemetryDashViewMenu::buildUI()
{
  // dial first: its opaque canvas must sit below every other widget
  buildDial();
  buildTopBar();
  buildTape(altTape, TAPE_L_X, true);
  buildTape(spdTape, TAPE_R_X, false);
  buildReadouts();
  buildGrid();
  // ...and the part of the dial rim that overlaps the panel goes on top
  buildSkirt();

  // Controller selection overlay (hidden by default, toggled with ENT)
  menuText = new StaticText(this, {SX(40), SY(120), S(400), S(50)}, "",
                            COLOR_THEME_QM_FG_INDEX, FONT(BOLD));
  lv_obj_set_style_text_align(menuText->getLvObj(), LV_TEXT_ALIGN_CENTER,
                              LV_PART_MAIN);
  lv_obj_set_style_text_color(menuText->getLvObj(), lv_color_white(),
                              LV_PART_MAIN);
  lv_obj_set_style_bg_color(menuText->getLvObj(), lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(menuText->getLvObj(), LV_OPA_80, LV_PART_MAIN);
  lv_obj_add_flag(menuText->getLvObj(), LV_OBJ_FLAG_HIDDEN);
}

//-----------------------------------------------------------------------------
// Top bar: [satellite] SATELLITE / SAT: n      BATTERY [battery] / 3.8V
//-----------------------------------------------------------------------------

void TelemetryDashViewMenu::buildTopBar()
{
  const int iw = S(ICON_W), ih = S(ICON_H);

  // --- satellite icon (drawn once) ---------------------------------------
  satIcon = lv_canvas_create(lvobj);
  lv_obj_set_pos(satIcon, SX(SAT_ICON_X), SY(ICON_Y));
  lv_obj_set_size(satIcon, iw, ih);
  satIconBuf = (uint8_t*)malloc(iw * ih * sizeof(lv_color_t));
  if (satIconBuf) {
    memset(satIconBuf, 0, iw * ih * sizeof(lv_color_t));
    lv_canvas_set_buffer(satIcon, satIconBuf, iw, ih, LV_IMG_CF_TRUE_COLOR);
    auto* buf = (lv_color_t*)satIconBuf;
    const lv_color_t c = rgb(C_ACCENT);
    // solar panels
    for (int p = 0; p < 2; p++) {
      const int px0 = p == 0 ? iw / 16 : iw * 11 / 16;
      const int px1 = px0 + iw * 4 / 16;
      rectOutline(buf, iw, ih, px0, ih * 3 / 8, px1, ih * 5 / 8, c);
      for (int k = 1; k < 3; k++) {
        const int x = px0 + k * (px1 - px0) / 3;
        drawLine(buf, iw, ih, x, ih * 3 / 8, x, ih * 5 / 8, c, 1);
      }
    }
    // body + antenna
    fillRect(buf, iw, ih, iw * 6 / 16, ih * 5 / 16, iw * 10 / 16, ih * 11 / 16, c);
    drawLine(buf, iw, ih, iw / 2, ih * 5 / 16, iw / 2, ih / 10, c, 1);
    fillRect(buf, iw, ih, iw * 7 / 16, ih / 14, iw * 9 / 16, ih / 5, c);
  }

  // --- battery icon ------------------------------------------------------
  battIcon = lv_canvas_create(lvobj);
  lv_obj_set_pos(battIcon, SX(BATT_ICON_X), SY(ICON_Y));
  lv_obj_set_size(battIcon, iw, ih);
  battIconBuf = (uint8_t*)malloc(iw * ih * sizeof(lv_color_t));
  if (battIconBuf) {
    memset(battIconBuf, 0, iw * ih * sizeof(lv_color_t));
    lv_canvas_set_buffer(battIcon, battIconBuf, iw, ih, LV_IMG_CF_TRUE_COLOR);
    auto* buf = (lv_color_t*)battIconBuf;
    const lv_color_t c = rgb(C_ACCENT);
    const lv_color_t fill = lv_color_make(0x6F, 0xD0, 0x4F);
    rectOutline(buf, iw, ih, iw / 8, ih / 4, iw * 7 / 8, ih * 15 / 16, c);
    // terminal
    fillRect(buf, iw, ih, iw * 5 / 12, ih / 8, iw * 7 / 12, ih / 4, c);
    // charge level
    fillRect(buf, iw, ih, iw / 8 + 2, ih / 4 + 2 + (ih * 11 / 16 - 4) / 3,
             iw * 7 / 8 - 2, ih * 15 / 16 - 2, fill);
  }

  const lv_color_t cyan = rgb(C_LABEL);

  StaticText* satTitle =
      new StaticText(this, {SX(SAT_TEXT_X), SY(14), S(TOP_TEXT_W), S(16)},
                     "SATELLITE", COLOR_THEME_QM_FG_INDEX, FONT(BOLD));
  lv_obj_set_style_text_color(satTitle->getLvObj(), cyan, LV_PART_MAIN);

  satValue = new StaticText(this, {SX(SAT_TEXT_X), SY(30), S(TOP_TEXT_W), S(24)},
                            "SAT: --", COLOR_THEME_QM_FG_INDEX, FONT(L));
  lv_obj_set_style_text_color(satValue->getLvObj(), lv_color_white(), LV_PART_MAIN);

  StaticText* battTitle =
      new StaticText(this, {SX(BATT_TEXT_X), SY(14), S(BATT_TEXT_W), S(16)},
                     "BATTERY", COLOR_THEME_QM_FG_INDEX, FONT(BOLD));
  lv_obj_set_style_text_align(battTitle->getLvObj(), LV_TEXT_ALIGN_RIGHT,
                              LV_PART_MAIN);
  lv_obj_set_style_text_color(battTitle->getLvObj(), cyan, LV_PART_MAIN);

  battValue = new StaticText(this, {SX(BATT_TEXT_X), SY(30), S(BATT_TEXT_W), S(24)},
                             "--.-V", COLOR_THEME_QM_FG_INDEX, FONT(L));
  lv_obj_set_style_text_align(battValue->getLvObj(), LV_TEXT_ALIGN_RIGHT,
                              LV_PART_MAIN);
  lv_obj_set_style_text_color(battValue->getLvObj(), lv_color_white(),
                              LV_PART_MAIN);

  // rule under the top bar, ending at the roll scale
  const int gap = HUD_R + 13;
  const coord_t seg[2][2] = {{SX(18), SX(HUD_CX - gap)},
                             {SX(HUD_CX + gap), SX(462)}};
  for (int i = 0; i < 2; i++) {
    lv_obj_t* r = lv_obj_create(lvobj);
    lv_obj_set_size(r, seg[i][1] - seg[i][0], S(2));
    lv_obj_set_pos(r, seg[i][0], SY(TOP_RULE_Y));
    lv_obj_set_style_radius(r, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(r, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(r, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(r, rgb(C_RULE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_PART_MAIN);
  }
}

//-----------------------------------------------------------------------------
// Centre dial: circular artificial horizon + compass rose
//-----------------------------------------------------------------------------

void TelemetryDashViewMenu::buildDial()
{
  const int w = S(HUD_W), h = S(HUD_H);
  dialCanvas = lv_canvas_create(lvobj);
  lv_obj_set_pos(dialCanvas, SX(HUD_X), SY(HUD_Y));
  lv_obj_set_size(dialCanvas, w, h);
  dialBuf = (uint8_t*)malloc(w * h * sizeof(lv_color_t));
  if (!dialBuf) return;
  memset(dialBuf, 0, w * h * sizeof(lv_color_t));
  lv_canvas_set_buffer(dialCanvas, dialBuf, w, h, LV_IMG_CF_TRUE_COLOR);
  drawDial();
}

void TelemetryDashViewMenu::drawDial()
{
  if (!dialBuf) return;
  const int W = S(HUD_W), H = S(HUD_H);
  const int R = S(HUD_R);
  const int cx = W / 2;
  const int cy = H / 2;
  const int R2 = R * R;
  auto* buf = (lv_color_t*)dialBuf;

  const lv_color_t bg = rgb(C_BG);
  const lv_color_t hznShadow = rgb(C_HZN_SHADOW);
  const lv_color_t rimDark = rgb(C_RIM_DARK);
  const lv_color_t line = lv_color_white();
  const lv_color_t accent = rgb(C_ACCENT);

  for (int i = 0; i < W * H; i++) buf[i] = bg;

  const float rollRad = (float)lastRoll * (float)M_PI / 180.0f;
  dashCosR = cosf(rollRad);
  dashSinR = sinf(rollRad);
  const float ppd = (float)R / 30.0f;  // +-30 deg of pitch across the dial
  const float pitchOff = (float)lastPitch * ppd;
  dashPitchOff = pitchOff;
  const float cosR = dashCosR, sinR = dashSinR;

  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      const int dx = x - cx, dy = y - cy;
      if (dx * dx + dy * dy > R2) continue;
      buf[y * W + x] = rgb(dialPixel(dx, dy));
    }
  }

  // rim outwards from the sky: near-black ring, cyan bezel, then a short teal
  // glow that fades into the page background. The bezel is interrupted around
  // 12 o'clock (roll scale area) exactly like the reference.
  {
    const lv_color_t glow1 = rgb(C_RIM_GLOW1);
    const lv_color_t glow2 = rgb(C_RIM_GLOW2);
    for (int a = 0; a < 2880; a++) {
      const float t = (float)a * (float)M_PI / 1440.0f;
      const float ct = cosf(t), st = sinf(t);
      // a is measured from 3 o'clock; 12 o'clock is at a = 2160
      const bool nearTop = (a > 1680 && a < 2640);  // +-60 deg around 12 o'clock
      const int kMax = nearTop ? S(6) : S(18);
      for (int k = 0; k <= kMax; k++) {
        const lv_color_t c = (k <= S(6))    ? rimDark
                             : (k <= S(12)) ? rgb(C_RING)
                             : (k <= S(15)) ? glow1
                                            : glow2;
        putPx(buf, W, H, cx + (int)lroundf((float)(R + k) * ct),
              cy + (int)lroundf((float)(R + k) * st), c);
      }
    }
  }
  for (int a = -60; a <= 60; a += 10) {
    if (a == 0) continue;
    const float t = (float)a * (float)M_PI / 180.0f;
    const int len = (a % 30 == 0) ? S(6) : S(4);
    for (int k = S(10); k < S(10) + len; k++) {
      putPx(buf, W, H, cx + (int)lroundf((R + k) * sinf(t)),
            cy - (int)lroundf((R + k) * cosf(t)), line);
    }
  }

  // pitch ladder: labelled lines at +-10/+-20, short dashes in between; the
  // lines run straight through the centre like the reference
  for (int p = -30; p <= 30; p += 5) {
    if (p == 0) continue;
    const bool major = (abs(p) == 10 || abs(p) == 20);
    const float ly = (float)(lastPitch - p) * ppd;
    const int dashOut = major ? S(46) : S(20);
    for (int sx = -dashOut; sx <= dashOut; sx++) {
      const float dx = (float)sx;
      const int px = cx + (int)(dx * cosR - ly * sinR);
      const int py = cy + (int)(dx * sinR + ly * cosR);
      if ((px - cx) * (px - cx) + (py - cy) * (py - cy) > R2) continue;
      putPx(buf, W, H, px, py, line);
      if (major) putPx(buf, W, H, px, py + 1, line);
    }
  }

  // horizon: white line with a darker tan shadow right below it
  for (int sx = -R; sx <= R; sx++) {
    const float dx = (float)sx;
    const int px = cx + (int)(dx * cosR - pitchOff * sinR);
    for (int k = 0; k < S(6); k++) {
      const int py = cy + (int)(dx * sinR + (pitchOff + (float)k) * cosR);
      if ((px - cx) * (px - cx) + (py - cy) * (py - cy) > R2) continue;
      putPx(buf, W, H, px, py, k < S(3) ? line : hznShadow);
    }
  }

  // pitch numbers (upright text)
  {
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.font = getFont(FONT(XS));
    dsc.color = line;
    char txt[8];
    for (int p = -20; p <= 20; p += 10) {
      if (p == 0) continue;
      const float ly = (float)(lastPitch - p) * ppd;
      const int halfW = S(46);
      const int py = cy + (int)(ly * cosR);
      if (py < S(26) || py > cy + S(44)) continue;
      snprintf(txt, sizeof(txt), "%d", abs(p));
      lv_canvas_draw_text(dialCanvas, cx - halfW - S(22), py - S(7), S(22), &dsc,
                          txt);
      lv_canvas_draw_text(dialCanvas, cx + halfW + S(2), py - S(7), S(22), &dsc,
                          txt);
    }
  }

  // white roll pointer straddling the rim, apex pointing inwards
  {
    const int tH = S(13);
    const int top = cy - R - S(3);
    for (int i = 0; i < tH; i++) {
      const int half = S(9) * (tH - 1 - i) / tH;
      fillRect(buf, W, H, cx - half, top + i, cx + half, top + i, line);
    }
  }

  // orange roll bug inside the rim, clear of the white pointer
  {
    const int tH = S(20);
    const int top = cy - R + S(9);
    for (int i = 0; i < tH; i++) {
      const int half = S(10) * (tH - 1 - i) / tH;
      fillRect(buf, W, H, cx - half, top + i, cx + half, top + i, accent);
    }
  }

  // fixed aircraft symbol: dark bug inside a hollow orange chevron
  drawLine(buf, W, H, cx - S(19), cy + S(1), cx, cy + S(17), accent, S(3));
  drawLine(buf, W, H, cx + S(19), cy + S(1), cx, cy + S(17), accent, S(3));
  for (int i = 0; i < S(13); i++) {
    const int half = S(8) * (S(13) - i) / S(13);
    fillRect(buf, W, H, cx - half, cy + S(1) + i, cx + half, cy + S(1) + i,
             lv_color_black());
  }

  // ---- compass rose band across the lower half of the dial --------------
  const int R_in = R - S(37);
  const int yaw = lastHdg >= 0 ? lastHdg : 0;

  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      uint32_t c;
      if (bandPixel(x - cx, y - cy, c)) buf[y * W + x] = rgb(c);
    }
  }

  // ticks sit just inside the inner edge of the band
  for (int th = -BAND_HALF; th <= BAND_HALF; th += 3) {
    const float t = (float)th * (float)M_PI / 180.0f;
    const int len = (th % 15 == 0) ? S(28) : ((th % 5 == 0) ? S(18) : S(11));
    for (int k = 0; k < len; k++) {
      const int r = R_in + S(3) + k;
      putPx(buf, W, H, cx + (int)lroundf((float)r * sinf(t)),
            cy + (int)lroundf((float)r * cosf(t)), line);
    }
  }

  lv_draw_label_dsc_t cdsc;
  lv_draw_label_dsc_init(&cdsc);
  cdsc.color = line;

  static const struct {
    int         a;
    const char* s;
  } card[4] = {{0, "N"}, {90, "E"}, {180, "S"}, {270, "W"}};

  for (int i = 0; i < 4; i++) {
    const int rel = (((card[i].a - yaw) % 360) + 540) % 360 - 180;
    if (rel < -(BAND_HALF - 2) || rel > (BAND_HALF - 2)) continue;
    const float t = (float)rel * (float)M_PI / 180.0f;
    const int r = R - S(18);
    const int lx = cx + (int)lroundf((float)r * sinf(t));
    const int ly = cy + (int)lroundf((float)r * cosf(t));
    cdsc.font = getFont(FONT(XL));
    lv_canvas_draw_text(dialCanvas, lx - S(22), ly - S(17), S(44), &cdsc,
                        card[i].s);
  }

  // home direction marker just inside the band
  if (lastHomeAngle >= 0) {
    const int rel = ((lastHomeAngle - yaw + 540) % 360) - 180;
    if (rel >= -(BAND_HALF - 2) && rel <= (BAND_HALF - 2)) {
      const float t = (float)rel * (float)M_PI / 180.0f;
      const int x = cx + (int)lroundf((R_in - S(6)) * sinf(t));
      const int y = cy + (int)lroundf((R_in - S(6)) * cosf(t));
      fillRect(buf, W, H, x - S(3), y - S(3), x + S(3), y + S(3), accent);
    }
  }

  // orange heading pointer above the rose
  {
    const int tH = S(10);
    const int top = S(64);
    for (int i = 0; i < tH; i++) {
      const int half = S(8) * (tH - 1 - i) / tH;
      fillRect(buf, W, H, cx - half, cy + top + i, cx + half, cy + top + i,
               accent);
    }
    fillRect(buf, W, H, cx - S(2), cy + top + tH - S(3), cx + S(2), cy + R_in,
             accent);
  }

  lv_obj_invalidate(dialCanvas);
  drawSkirt();   // the strip of the dial that overlaps the panel
}

// Interior colour (packed RGB) of the dial at an offset from its centre.
// The compass band is not included, it is drawn in a later pass.
uint32_t TelemetryDashViewMenu::dialPixel(int dx, int dy) const
{
  const int R = S(HUD_R);
  const int d2 = dx * dx + dy * dy;
  const float hh = (-(float)dx * dashSinR + (float)dy * dashCosR) - dashPitchOff;

  if (hh < 0.0f) return C_SKY;
  if (d2 > R * R * 24 / 25) return C_GROUND_FAR;   // darkens towards the rim
  if (d2 > R * R * 4 / 5) return C_GROUND_MID;
  return (hh < (float)S(16)) ? C_GROUND_HI : C_GROUND;
}

// Compass band colour for a pixel inside the band, false if outside
bool TelemetryDashViewMenu::bandPixel(int dx, int dy, uint32_t& col) const
{
  const int R = S(HUD_R);
  const int d2 = dx * dx + dy * dy;
  const int R_in = R - S(37);
  const int R_out = R + S(6);

  if (dy <= 0 || d2 < R_in * R_in || d2 > R_out * R_out) return false;
  const float ang = atan2f((float)dx, (float)dy) * 180.0f / (float)M_PI;
  if (fabsf(ang) > (float)BAND_HALF) return false;

  const int r = (int)lroundf(sqrtf((float)d2));
  col = (r < R_in + S(4)) ? C_BAND_EDGE : C_BAND;
  return true;
}

//-----------------------------------------------------------------------------
// Lower rim of the dial
//
// The reference draws the dial over the value panel, so the rim that falls
// inside the panel is drawn by a small canvas with an alpha channel placed on
// top of it (LVGL's canvas text/colour helpers do not write the alpha value,
// so this canvas is filled pixel by pixel instead).
//-----------------------------------------------------------------------------

void TelemetryDashViewMenu::buildSkirt()
{
  const int w = S(HUD_W), h = S(HUD_SKIRT_H);
  skirtCanvas = lv_canvas_create(lvobj);
  lv_obj_set_pos(skirtCanvas, SX(HUD_X), SY(HUD_SKIRT_Y));
  lv_obj_set_size(skirtCanvas, w, h);
  skirtBuf = (uint8_t*)malloc((size_t)w * h * 3);
  if (!skirtBuf) return;
  lv_canvas_set_buffer(skirtCanvas, skirtBuf, w, h, LV_IMG_CF_TRUE_COLOR_ALPHA);
  drawSkirt();
}

void TelemetryDashViewMenu::drawSkirt()
{
  if (!skirtBuf) return;

  const int W = S(HUD_W), H = S(HUD_SKIRT_H);
  const int R = S(HUD_R);
  const int cx = W / 2;
  const int cy = SY(HUD_CY) - SY(HUD_SKIRT_Y);
  uint8_t* buf = skirtBuf;

  memset(buf, 0, (size_t)W * H * 3);

  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      const int dx = x - cx, dy = y - cy;
      const int k = (int)lroundf(sqrtf((float)(dx * dx + dy * dy))) - R;
      if (k > S(18)) continue;

      uint32_t col;
      if (k < 0) {
        // inside the dial: the main canvas is covered by the panel here
        col = dialPixel(dx, dy);
        uint32_t bc;
        if (bandPixel(dx, dy, bc)) col = bc;
      } else if (k <= S(6)) {
        const float ang = atan2f((float)dx, (float)dy) * 180.0f / (float)M_PI;
        col = (dy > 0 && fabsf(ang) <= (float)BAND_HALF) ? C_BAND : C_RIM_DARK;
      } else if (k <= S(12)) {
        col = C_RING;
      } else if (k <= S(15)) {
        col = C_RIM_GLOW1;
      } else {
        col = C_RIM_GLOW2;
      }

      const lv_color_t c = rgb(col);
      uint8_t* p = buf + ((size_t)y * W + x) * 3;
      memcpy(p, &c, 2);
      p[2] = 0xFF;
    }
  }

  lv_obj_invalidate(skirtCanvas);
}

//-----------------------------------------------------------------------------
// Vertical value tape
//-----------------------------------------------------------------------------

void TelemetryDashViewMenu::buildTape(Tape& t, coord_t designX, bool left)
{
  t.left = left;
  const coord_t x = SX(designX);
  const coord_t w = S(TAPE_W);
  const coord_t y = SY(TAPE_Y);
  const coord_t h = S(TAPE_H);
  const coord_t numW = S(56);

  // geometry is kept in the tape so updateTape() cannot drift from this code
  t.x = x;
  t.y = y;
  t.w = w;
  t.h = h;
  t.numW = numW;
  t.centerY = y + h / 2;
  t.stepPx = S(TAPE_STEP_PX);

  // tape background
  lv_obj_t* bg = lv_obj_create(lvobj);
  lv_obj_set_pos(bg, x, y);
  lv_obj_set_size(bg, w, h);
  lv_obj_set_style_radius(bg, S(4), LV_PART_MAIN);
  lv_obj_set_style_border_width(bg, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(bg, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_color(bg, rgb(C_TAPE), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(bg, LV_OPA_COVER, LV_PART_MAIN);

  // scale numbers sit on the outer side of the tape
  const coord_t numX = left ? x + S(2) : x + w - numW - S(2);
  t.numX = numX;

  for (int i = 0; i < 5; i++) {
    t.num[i] = new StaticText(this, {numX, y, numW, S(20)}, "",
                              COLOR_THEME_QM_FG_INDEX, FONT(BOLD));
    lv_obj_set_style_text_align(t.num[i]->getLvObj(),
                                left ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_RIGHT,
                                LV_PART_MAIN);
    lv_obj_set_style_text_color(t.num[i]->getLvObj(), lv_color_white(),
                                LV_PART_MAIN);
  }

  // tick marks sit on the dial side
  const coord_t tickX = left ? x + w - S(8) : x + S(2);
  t.tickX = tickX;
  for (int i = 0; i < 6; i++) {
    t.tick[i] = lv_obj_create(lvobj);
    lv_obj_set_size(t.tick[i], S(6), S(2));
    lv_obj_set_pos(t.tick[i], tickX, y);
    lv_obj_set_style_radius(t.tick[i], 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(t.tick[i], 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(t.tick[i], 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(t.tick[i], lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(t.tick[i], LV_OPA_COVER, LV_PART_MAIN);
  }

  // centre box with the current value: dark body, orange pentagon outline with
  // the tip pointing at the dial
  const coord_t boxH = S(TAPE_BOX_H);
  const coord_t boxY = y + (h - boxH) / 2;
  const coord_t tip = S(10);

  t.box = lv_obj_create(lvobj);
  lv_obj_set_size(t.box, w, boxH);
  lv_obj_set_pos(t.box, x, boxY);
  lv_obj_set_style_radius(t.box, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(t.box, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_color(t.box, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(t.box, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(t.box, 0, LV_PART_MAIN);

  // vertices in path order: body top-left, top-right, tip, bottom-right,
  // bottom-left (mirrored for the right-hand tape, whose tip is on the left)
  const coord_t outlineX = left ? x : x - tip;
  const coord_t b0 = left ? 0 : tip;
  const coord_t b1 = left ? w : w + tip;
  const coord_t apex = left ? w + tip : 0;
  const coord_t bot = boxH - 1;
  const coord_t midY = boxH / 2;

  t.boxPts[0] = {(lv_coord_t)b0, 0};
  t.boxPts[1] = {(lv_coord_t)b1, 0};
  if (left) {
    t.boxPts[2] = {(lv_coord_t)apex, (lv_coord_t)midY};
    t.boxPts[3] = {(lv_coord_t)b1, (lv_coord_t)bot};
    t.boxPts[4] = {(lv_coord_t)b0, (lv_coord_t)bot};
  } else {
    t.boxPts[2] = {(lv_coord_t)b1, (lv_coord_t)bot};
    t.boxPts[3] = {(lv_coord_t)b0, (lv_coord_t)bot};
    t.boxPts[4] = {(lv_coord_t)apex, (lv_coord_t)midY};
  }
  t.boxPts[5] = t.boxPts[0];

  t.boxOutline = lv_line_create(lvobj);
  lv_obj_set_pos(t.boxOutline, outlineX, boxY);
  lv_obj_clear_flag(t.boxOutline, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(t.boxOutline, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(t.boxOutline, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(t.boxOutline, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(t.boxOutline, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(t.boxOutline, 0, LV_PART_MAIN);
  lv_obj_set_style_line_width(t.boxOutline, S(2), LV_PART_MAIN);
  lv_obj_set_style_line_color(t.boxOutline, rgb(C_ACCENT), LV_PART_MAIN);
  lv_obj_set_style_line_opa(t.boxOutline, LV_OPA_COVER, LV_PART_MAIN);
  lv_line_set_points(t.boxOutline, t.boxPts, 6);

  t.boxValue = new StaticText(this, {x + S(2), y + (h - S(24)) / 2, w - S(4), S(24)},
                              "--", COLOR_THEME_QM_FG_INDEX, FONT(L));
  lv_obj_set_style_text_align(t.boxValue->getLvObj(), LV_TEXT_ALIGN_CENTER,
                              LV_PART_MAIN);
  lv_obj_set_style_text_color(t.boxValue->getLvObj(), lv_color_white(),
                              LV_PART_MAIN);
}

void TelemetryDashViewMenu::updateTape(Tape& t, float value, float step,
                                       bool valid)
{
  char txt[16];

  if (!valid) {
    t.boxValue->setText("--");
    for (int i = 0; i < 5; i++)
      lv_obj_add_flag(t.num[i]->getLvObj(), LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < 6; i++)
      lv_obj_add_flag(t.tick[i], LV_OBJ_FLAG_HIDDEN);
    return;
  }

  snprintf(txt, sizeof(txt), "%.0f", value);
  t.boxValue->setText(txt);

  const float base = floorf(value / step) * step;
  for (int i = 0; i < 5; i++) {
    const float v = base + (float)(i - 2) * step;
    const coord_t py = t.centerY - (coord_t)((v - value) * t.stepPx / step);
    const bool nearBox = fabsf(v - value) < step * 0.5f;
    if (nearBox || py < t.y - S(20) || py > t.y + t.h) {
      lv_obj_add_flag(t.num[i]->getLvObj(), LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_clear_flag(t.num[i]->getLvObj(), LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_pos(t.num[i]->getLvObj(), t.numX, py - S(7));
      snprintf(txt, sizeof(txt), "%.0f", v);
      t.num[i]->setText(txt);
    }
  }

  for (int i = 0; i < 6; i++) {
    const float v = base + ((float)i - 2.5f) * step;
    const coord_t py = t.centerY - (coord_t)((v - value) * t.stepPx / step);
    if (py < t.y || py > t.y + t.h) {
      lv_obj_add_flag(t.tick[i], LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_clear_flag(t.tick[i], LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_pos(t.tick[i], t.tickX, py);
    }
  }
}

//-----------------------------------------------------------------------------
// Numeric readouts under the tapes
//-----------------------------------------------------------------------------

void TelemetryDashViewMenu::buildReadouts()
{
  // Both readouts overlap the dial canvas' corners, which are plain C_BG, so
  // they stay invisible as long as the canvas keeps C_BG as its fill.
  const coord_t titleY = SY(RO_TITLE_Y), valueY = SY(RO_VALUE_Y);
  const coord_t titleH = S(RO_TITLE_H), valueH = S(RO_VALUE_H);

  // altitude: value only, centred under the left tape
  altReadout = new StaticText(this,
                              {SX(TAPE_L_X - 18), valueY, S(102), valueH},
                              "--- m", COLOR_THEME_QM_FG_INDEX, FONT(L));
  lv_obj_set_style_text_align(altReadout->getLvObj(), LV_TEXT_ALIGN_CENTER,
                              LV_PART_MAIN);
  lv_obj_set_style_text_color(altReadout->getLvObj(), rgb(C_ACCENT), LV_PART_MAIN);

  // ground speed: title above value, under the right tape
  const coord_t rw = S(100);
  const coord_t rx = SX(TAPE_R_X + TAPE_W) - rw;

  spdTitle = new StaticText(this, {rx, titleY, rw, titleH}, "GND SPD",
                            COLOR_THEME_QM_FG_INDEX, FONT(BOLD));
  lv_obj_set_style_text_align(spdTitle->getLvObj(), LV_TEXT_ALIGN_RIGHT,
                              LV_PART_MAIN);
  lv_obj_set_style_text_color(spdTitle->getLvObj(), rgb(C_LABEL), LV_PART_MAIN);

  spdReadout = new StaticText(this, {rx, valueY, rw, valueH}, "--- km/h",
                              COLOR_THEME_QM_FG_INDEX, FONT(BOLD));
  lv_obj_set_style_text_align(spdReadout->getLvObj(), LV_TEXT_ALIGN_RIGHT,
                              LV_PART_MAIN);
  lv_obj_set_style_text_color(spdReadout->getLvObj(), rgb(C_ACCENT), LV_PART_MAIN);
}

//-----------------------------------------------------------------------------
// Bottom 3x2 value grid
//-----------------------------------------------------------------------------

void TelemetryDashViewMenu::buildGrid()
{
  static const char* labels[6] = {"VSpd", "RxV",  "Amp",
                                  "FLv",  "RSSI", "FRv"};

  const coord_t gx = SX(GRID_X), gy = SY(GRID_Y);
  const coord_t gw = S(GRID_W), gh = S(GRID_H);
  const coord_t cellW = gw / 3, cellH = gh / 2;

  gridFrame = lv_obj_create(lvobj);
  lv_obj_set_pos(gridFrame, gx, gy);
  lv_obj_set_size(gridFrame, gw, gh);
  lv_obj_set_style_radius(gridFrame, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(gridFrame, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_color(gridFrame, rgb(C_PANEL), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(gridFrame, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(gridFrame, 0, LV_PART_MAIN);

  // chamfered outline (all corners cut) like the reference panel
  const coord_t cut = S(14);
  const coord_t gh1 = gh - 1, gw1 = gw - 1;
  gridPts[0] = {(lv_coord_t)cut, 0};
  gridPts[1] = {(lv_coord_t)(gw1 - cut), 0};
  gridPts[2] = {(lv_coord_t)gw1, (lv_coord_t)cut};
  gridPts[3] = {(lv_coord_t)gw1, (lv_coord_t)(gh1 - cut)};
  gridPts[4] = {(lv_coord_t)(gw1 - cut), (lv_coord_t)gh1};
  gridPts[5] = {(lv_coord_t)cut, (lv_coord_t)gh1};
  gridPts[6] = {0, (lv_coord_t)(gh1 - cut)};
  gridPts[7] = {0, (lv_coord_t)cut};
  gridPts[8] = gridPts[0];

  gridOutline = lv_line_create(lvobj);
  lv_obj_set_pos(gridOutline, gx, gy);
  lv_obj_clear_flag(gridOutline, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(gridOutline, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(gridOutline, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(gridOutline, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(gridOutline, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(gridOutline, 0, LV_PART_MAIN);
  lv_obj_set_style_line_width(gridOutline, S(2), LV_PART_MAIN);
  lv_obj_set_style_line_color(gridOutline, rgb(C_PANEL_EDGE), LV_PART_MAIN);
  lv_obj_set_style_line_opa(gridOutline, LV_OPA_COVER, LV_PART_MAIN);
  lv_line_set_points(gridOutline, gridPts, 9);

  for (int c = 1; c < 3; c++) {
    lv_obj_t* v = lv_obj_create(lvobj);
    lv_obj_set_size(v, S(2), gh - S(4));
    lv_obj_set_pos(v, gx + c * cellW, gy + S(2));
    lv_obj_set_style_radius(v, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(v, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(v, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(v, rgb(C_DIV), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(v, LV_OPA_COVER, LV_PART_MAIN);
  }

  lv_obj_t* hline = lv_obj_create(lvobj);
  lv_obj_set_size(hline, gw - S(6), S(2));
  lv_obj_set_pos(hline, gx + S(3), gy + cellH);
  lv_obj_set_style_radius(hline, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(hline, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(hline, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_color(hline, rgb(C_DIV), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(hline, LV_OPA_COVER, LV_PART_MAIN);

  for (int i = 0; i < 6; i++) {
    const int c = i % 3, r = i / 3;
    const coord_t cx0 = gx + c * cellW;
    const coord_t cy0 = gy + r * cellH;

    cellLabel[i] = new StaticText(this, {cx0, cy0 + S(4), cellW, S(16)},
                                  labels[i], COLOR_THEME_QM_FG_INDEX, FONT(BOLD));
    lv_obj_set_style_text_align(cellLabel[i]->getLvObj(), LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN);
    lv_obj_set_style_text_color(cellLabel[i]->getLvObj(), rgb(C_LABEL),
                                LV_PART_MAIN);

    cellValue[i] = new StaticText(this, {cx0, cy0 + S(20), cellW, S(26)}, "---",
                                  COLOR_THEME_QM_FG_INDEX, FONT(L));
    lv_obj_set_style_text_align(cellValue[i]->getLvObj(), LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN);
    lv_obj_set_style_text_color(cellValue[i]->getLvObj(), rgb(C_VALUE),
                                LV_PART_MAIN);
  }
}

//-----------------------------------------------------------------------------
// Periodic update
//-----------------------------------------------------------------------------

void TelemetryDashViewMenu::checkEvents()
{
  NavWindow::checkEvents();
  if (deleted()) return;

  if (menuActive) {
    lv_obj_clear_flag(menuText->getLvObj(), LV_OBJ_FLAG_HIDDEN);
    if (controllerType == CONTROLLER_ARDUPILOT)
      menuText->setText("[ArduPilot (yaapu)]\n  INAV (iNav)");
    else
      menuText->setText("  ArduPilot (yaapu)\n[INAV (iNav)]");
  } else {
    lv_obj_add_flag(menuText->getLvObj(), LV_OBJ_FLAG_HIDDEN);
  }

  updateValues();
}

void TelemetryDashViewMenu::updateValues()
{
  char txt[32];
  int idx;

  // ---- top bar -----------------------------------------------------------
  idx = findSensor("Sats");
  int sats = idx >= 0 ? (int)getSensorValue(idx) : -1;
  if (sats != lastSats && sats >= 0) {
    lastSats = sats;
    snprintf(txt, sizeof(txt), "SAT: %d", sats);
    satValue->setText(txt);
  }

  idx = findForController("VFAS", "RxBt");
  if (idx < 0) {
    for (int i = 0; i < MAX_TELEMETRY_SENSORS; i++) {
      if (!telemetryItems[i].isFresh()) continue;
      if (g_model.telemetrySensors[i].unit != UNIT_VOLTS) continue;
      if (getSensorValue(i) > 5.0f) { idx = i; break; }
    }
  }
  float battV = idx >= 0 ? getSensorValueIn(idx, UNIT_VOLTS, 2) : -100000;
  if (battV > -1000 && fabsf(battV - lastBattV) > 0.02f) {
    lastBattV = battV;
    snprintf(txt, sizeof(txt), "%.1fV", battV);
    battValue->setText(txt);
  }

  // ---- dial --------------------------------------------------------------
  idx = findSensor("Hdg");
  int hdg = idx >= 0 ? (int)getSensorValue(idx) : -1;
  idx = findSensor("Pitch");
  int pitch = idx >= 0 ? (int)getSensorValue(idx) : 0;
  idx = findSensor("Roll");
  int roll = idx >= 0 ? (int)getSensorValue(idx) : 0;

  idx = findSensor("Homes");
  if (idx < 0) {
    for (int i = 0; i < MAX_TELEMETRY_SENSORS; i++) {
      if (!telemetryItems[i].isFresh()) continue;
      if (g_model.telemetrySensors[i].unit == UNIT_DEGREE &&
          g_model.telemetrySensors[i].type == TELEM_TYPE_CALCULATED) {
        idx = i;
        break;
      }
    }
  }
  int homeAngle = idx >= 0 ? (int)getSensorValue(idx) : -1;

  if (hdg != lastHdg || pitch != lastPitch || roll != lastRoll ||
      homeAngle != lastHomeAngle) {
    lastHdg = hdg;
    lastPitch = pitch;
    lastRoll = roll;
    lastHomeAngle = homeAngle;
    drawDial();
  }

  // ---- altitude tape (metric) -------------------------------------------
  idx = findForController("GAlt", "Alt");
  float alt = idx >= 0 ? getSensorValueIn(idx, UNIT_METERS, 0) : -100000;
  const bool altValid = alt > -10000;
  if (fabsf(alt - lastAlt) > 0.4f) {
    lastAlt = alt;
    snprintf(txt, sizeof(txt), altValid ? "%.0f m" : "--- m", alt);
    altReadout->setText(txt);
    updateTape(altTape, alt, 10.0f, altValid);
  }

  // ---- speed tape (metric) ----------------------------------------------
  idx = findForController("GSpd", "GSpd");
  float spd = idx >= 0 ? getSensorValueIn(idx, UNIT_KMH, 0) : -100000;
  const bool spdValid = spd > -10000;
  if (fabsf(spd - lastSpd) > 0.4f) {
    lastSpd = spd;
    snprintf(txt, sizeof(txt), spdValid ? "%.0f km/h" : "--- km/h", spd);
    spdReadout->setText(txt);
    updateTape(spdTape, spd, 10.0f, spdValid);
  }

  // ---- bottom grid -------------------------------------------------------
  static const struct {
    const char* ap;
    const char* inav;
    uint8_t     unit;
    uint8_t     prec;
  } cells[6] = {
      {"VSpd", nullptr, UNIT_METERS_PER_SECOND, 1},
      {"RxV", "RxV", UNIT_VOLTS, 2},
      {"Curr", "Curr", UNIT_AMPS, 2},
      {"Bat%", "Fuel", UNIT_PERCENT, 0},
      {nullptr, nullptr, 0, 0},  // RSSI comes from the link, not a sensor
      {"FRv", "VFAS", UNIT_VOLTS, 2},
  };

  for (int i = 0; i < 6; i++) {
    const int prec = (i == 4) ? 0 : cells[i].prec;
    int v = -100000;

    if (i == 4) {
      v = TELEMETRY_RSSI();
    } else {
      const int si = findForController(cells[i].ap, cells[i].inav);
      if (si >= 0) {
        const float f = getSensorValueIn(si, cells[i].unit, cells[i].prec);
        if (f > -1000) {
          float sc = 1.0f;
          for (int p = 0; p < cells[i].prec; p++) sc *= 10.0f;
          v = (int)lroundf(f * sc);
        }
      }
    }

    if (v != lastCells[i]) {
      lastCells[i] = v;
      if (v <= -1000) {
        cellValue[i]->setText("---");
      } else {
        float sc = 1.0f;
        for (int p = 0; p < prec; p++) sc *= 10.0f;
        snprintf(txt, sizeof(txt), "%.*f", prec, v / sc);
        cellValue[i]->setText(txt);
      }
    }
  }
}

//-----------------------------------------------------------------------------
// Events
//-----------------------------------------------------------------------------

void TelemetryDashViewMenu::onCancel() { closeWindow(); }

void TelemetryDashViewMenu::onClicked()
{
  // ENT: toggle the controller selection overlay
  menuActive = !menuActive;
}

void TelemetryDashViewMenu::onEvent(event_t event)
{
#if defined(HARDWARE_KEYS)
  if (event == EVT_KEY_BREAK(KEY_EXIT) || event == EVT_KEY_LONG(KEY_EXIT)) {
    if (menuActive) { menuActive = false; return; }
    closeWindow();
    return;
  }
#endif
  NavWindow::onEvent(event);
}

#if defined(HARDWARE_KEYS)
void TelemetryDashViewMenu::onLongPressRTN() { closeWindow(); }
#endif
