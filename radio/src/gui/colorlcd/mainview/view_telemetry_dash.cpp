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
#include "telemetry/crossfire.h"
#include "timers.h"
#include "rtc.h"
#include "os/time.h"
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
inline float SF(float v) { return v * (float)SCL / 100.0f; }

constexpr uint32_t C_BG = 0x060D14;
constexpr uint32_t C_LABEL = 0x8ECFE0;      // cyan labels
constexpr uint32_t C_VALUE = 0xFFFFFF;
constexpr uint32_t C_ACCENT = 0xE8B468;     // orange symbols
constexpr uint32_t C_READOUT = 0xEBAE6E;    // orange readouts
constexpr uint32_t C_OUTLINE = 0x10161C;    // dark outline around symbols
constexpr uint32_t C_RULE = 0xB39A82;       // tan rules under the top bar
constexpr uint32_t C_SAT = 0xE89A4A;
constexpr uint32_t C_BATT_BOLT = 0x2E3A1E;
constexpr uint32_t C_BATT_EMPTY = 0x404040;   // as the idle link bars
constexpr uint32_t C_TXBATT_OK = 0x00FF80;    // transmitter battery levels, as
constexpr uint32_t C_TXBATT_LOW = 0xFFD000;   // on the FPV dashboard header
constexpr uint32_t C_TXBATT_CRIT = 0xFF3030;
// the FPV dashboard header reads the level with GET_TXBATT_BARS(20)
constexpr int BATT_LEVEL_BARS = 20;
constexpr uint32_t C_LQ_OFF = 0x404040;     // FPV dashboard signal bars
constexpr uint32_t C_LQ_ON = 0x00FF80;
constexpr uint32_t C_SKY = 0x2EA3B7;
constexpr uint32_t C_GROUND_HI = 0xA86F3B;  // ground right below the horizon
constexpr uint32_t C_GROUND_LO = 0x5D3E24;  // ground near the bottom
constexpr uint32_t C_LADDER = 0xE6F2F4;
constexpr uint32_t C_WINGBAR = 0x0A0A0A;
constexpr uint32_t C_COMPASS = 0x193A46;
constexpr uint32_t C_COMPASS_RIM = 0x08151E;
constexpr uint32_t C_TICK = 0xD8E4E8;
constexpr uint32_t C_TAPE = 0x1B3D49;
constexpr uint32_t C_TAPE_CYAN = 0x66C6D2;
constexpr uint32_t C_TAPE_ORANGE = 0xE2AE66;
constexpr uint32_t C_BOX_EDGE = 0xB3AB9A;
constexpr uint32_t C_BOX_FILL = 0x0F151B;
constexpr uint32_t C_PANEL = 0x0A121C;
constexpr uint32_t C_PANEL_EDGE = 0x2E4652;
constexpr uint32_t C_DIV = 0xA08C78;        // tan panel dividers
constexpr uint32_t C_WING_FILL = 0x25313F;  // mesh of the side wings
constexpr uint32_t C_WING_DOT = 0x132030;
constexpr uint32_t C_WING_EDGE = 0x3A5260;
constexpr uint32_t C_WING_TAN = 0x5A4A3E;
constexpr uint32_t C_WING_CYAN = 0x7BD8E2;
constexpr uint32_t C_WING_CYAN_DIM = 0x2C8C9A;

inline lv_color_t rgb(uint32_t v)
{
  return lv_color_make((uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v);
}

inline uint32_t lerpColor(uint32_t a, uint32_t b, float t)
{
  auto ch = [&](int s) {
    const float ca = (float)((a >> s) & 0xFF), cb = (float)((b >> s) & 0xFF);
    return (uint32_t)(ca + (cb - ca) * t) << s;
  };
  return ch(16) | ch(8) | ch(0);
}

// top bar
constexpr int TOP_LABEL_CY = 22, TOP_VALUE_CY = 44;
constexpr int SAT_ICON_X = 16, SAT_ICON_Y = 14, SAT_ICON_W = 44, SAT_ICON_H = 42;
constexpr int SAT_TEXT_X = 63;
// flight mode caption, above the satellite count
constexpr int FLIGHT_MODE_W = 200;
constexpr int BATT_ICON_X = 385, BATT_ICON_Y = 31, BATT_ICON_W = 16, BATT_ICON_H = 24;
constexpr int BATT_TEXT_X = 405, BATT_TITLE_R = 457;
// link quality bars (FPV dashboard style), left of their percentage
constexpr int LQ_BAR_W = 3, LQ_BAR_SZ = 5, LQ_BAR_COUNT = 5;
constexpr int LQ_BARS_W = (LQ_BAR_COUNT - 1) * LQ_BAR_SZ + LQ_BAR_W;
constexpr int LQ_BARS_R = BATT_ICON_X + BATT_ICON_W;
constexpr int LQ_BARS_BOTTOM = TOP_LABEL_CY + 7;
constexpr int RULE_Y = 57;

// centre dial; the canvas adds HUD_PAD around the circle for the roll scale
constexpr int HUD_CX = 240, HUD_CY = 126, HUD_R = 102, HUD_PAD = 16;
constexpr int HUD_W = 2 * (HUD_R + HUD_PAD);
constexpr int HUD_H = HUD_W;
constexpr int HUD_X = HUD_CX - HUD_W / 2;
constexpr int HUD_Y = HUD_CY - HUD_H / 2;
constexpr int PITCH_PX10 = 33;  // pixels per 10 degrees of pitch
// half compass rose resting on the bottom panel
constexpr int CMP_DY = 110, CMP_R = 56;
constexpr float CMP_SCALE = 2.5f;  // heading degrees per screen degree
// The attitude sensors only report a few frames per second (CRSF shares a
// single telemetry frame budget), so the drawn angles glide linearly from one
// sample to the next: with the phase advanced by half a sample interval the
// ball runs at the rate the last interval showed and its average latency is
// the same as drawing the raw samples, but the step it makes is at most half
// of the raw one. Gliding without that phase advance would trail a whole
// interval behind; leading past the newest sample would overrun on reversals.
constexpr int   ATTITUDE_GLIDE_MAX_MS = 600;  // longest glide between samples
constexpr float ATTITUDE_GLIDE_PHASE = 0.5f;  // 0 = a full interval behind
constexpr int   ATTITUDE_LAG_MS = 40;         // low pass after the glide
constexpr float ATTITUDE_EPS = 0.05f;   // deg, closer than this pitch/roll snap
constexpr float HEADING_EPS = 0.2f;     // deg, headings snap closer than this

// side wings (left one; the right one is its mirror image)
constexpr int WING_X = 8, WING_Y = 62, WING_W = 46, WING_H = 162;

// value tapes
constexpr int TAPE_Y = 61, TAPE_W = 72;
constexpr int TAPE_L_X = 55, TAPE_L_H = 130;
constexpr int TAPE_R_X = DES_W - TAPE_L_X - TAPE_W, TAPE_R_H = 115;
constexpr int TAPE_BAR_W = 6;
constexpr int TAPE_STEP_PX = 36;  // pixels per scale step

// readouts under the tapes
constexpr int RO_TITLE_CY = 187, RO_VALUE_CY = 205;
constexpr int RO_SPD_CX = 390;

// bottom panel (3 columns x 2 rows)
constexpr int GRID_X = 21, GRID_Y = 218, GRID_W = 438, GRID_H = 92;
constexpr int GRID_CUT = 15;
constexpr int GRID_COL[4] = {GRID_X, 170, 310, GRID_X + GRID_W};
constexpr int GRID_HDIV_Y = 266;
constexpr int GRID_LABEL_CY[2] = {230, 277};
constexpr int GRID_VALUE_CY[2] = {252, 295};

// grid cells in panel order (top row, then bottom row)
enum { CELL_SPD, CELL_DIST, CELL_BATT, CELL_RPM1, CELL_RPM2, CELL_CURR };

//-----------------------------------------------------------------------------
// Tiny software drawing helpers for canvas buffers (canvas pixels)
//-----------------------------------------------------------------------------

struct Surf {
  uint8_t* buf;
  int      w, h;
  bool     alpha;

  void px(int x, int y, uint32_t c) const
  {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    const lv_color_t col = rgb(c);
    if (alpha) {
      uint8_t* p = buf + ((size_t)y * w + x) * LV_IMG_PX_SIZE_ALPHA_BYTE;
      memcpy(p, &col, sizeof(lv_color_t));
      p[LV_IMG_PX_SIZE_ALPHA_BYTE - 1] = 0xFF;
    } else {
      ((lv_color_t*)buf)[(size_t)y * w + x] = col;
    }
  }

  void rect(int x0, int y0, int x1, int y1, uint32_t c) const
  {
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    for (int y = y0; y <= y1; y++)
      for (int x = x0; x <= x1; x++) px(x, y, c);
  }

  // line with a square brush of t pixels
  void line(float x0, float y0, float x1, float y1, uint32_t c, int t = 1) const
  {
    const float dx = x1 - x0, dy = y1 - y0;
    const int n = (int)ceilf(fmaxf(fabsf(dx), fabsf(dy))) + 1;
    const int lo = -(t - 1) / 2, hi = t / 2;
    for (int i = 0; i < n; i++) {
      const float k = n > 1 ? (float)i / (float)(n - 1) : 0.0f;
      const int x = (int)lroundf(x0 + dx * k), y = (int)lroundf(y0 + dy * k);
      rect(x + lo, y + lo, x + hi, y + hi, c);
    }
  }

  void tri(float ax, float ay, float bx, float by, float cx, float cy,
           uint32_t c) const
  {
    const int x0 = (int)floorf(fminf(ax, fminf(bx, cx)));
    const int x1 = (int)ceilf(fmaxf(ax, fmaxf(bx, cx)));
    const int y0 = (int)floorf(fminf(ay, fminf(by, cy)));
    const int y1 = (int)ceilf(fmaxf(ay, fmaxf(by, cy)));
    auto edge = [](float px, float py, float qx, float qy, float x, float y) {
      return (qx - px) * (y - py) - (qy - py) * (x - px);
    };
    for (int y = y0; y <= y1; y++) {
      for (int x = x0; x <= x1; x++) {
        const float fx = x + 0.5f, fy = y + 0.5f;
        const float e0 = edge(ax, ay, bx, by, fx, fy);
        const float e1 = edge(bx, by, cx, cy, fx, fy);
        const float e2 = edge(cx, cy, ax, ay, fx, fy);
        if ((e0 >= 0 && e1 >= 0 && e2 >= 0) || (e0 <= 0 && e1 <= 0 && e2 <= 0))
          px(x, y, c);
      }
    }
  }

  // filled polygon (even-odd rule, pixel centres)
  void poly(const float* xs, const float* ys, int n, uint32_t c) const
  {
    for (int y = 0; y < h; y++) {
      const float fy = y + 0.5f;
      float cross[16];
      int nc = 0;
      for (int i = 0; i < n && nc < 16; i++) {
        const int j = (i + 1) % n;
        if ((ys[i] <= fy && ys[j] > fy) || (ys[j] <= fy && ys[i] > fy))
          cross[nc++] = xs[i] + (fy - ys[i]) * (xs[j] - xs[i]) / (ys[j] - ys[i]);
      }
      for (int a = 1; a < nc; a++)
        for (int b = a; b > 0 && cross[b - 1] > cross[b]; b--) {
          float t = cross[b]; cross[b] = cross[b - 1]; cross[b - 1] = t;
        }
      for (int k = 0; k + 1 < nc; k += 2)
        for (int x = (int)ceilf(cross[k] - 0.5f); x <= (int)floorf(cross[k + 1] - 0.5f); x++)
          px(x, y, c);
    }
  }

  void outline(const float* xs, const float* ys, int n, uint32_t c, int t) const
  {
    for (int i = 0; i < n; i++) {
      const int j = (i + 1) % n;
      line(xs[i], ys[i], xs[j], ys[j], c, t);
    }
  }
};

// y of a label's top edge so that its capital letters are centred on capCY
coord_t textTop(LcdFlags font, coord_t capCY)
{
  const lv_font_t* f = getFont(font);
  lv_font_glyph_dsc_t g;
  int capH = 0, ofs = 0;
  if (lv_font_get_glyph_dsc(f, &g, 'H', 0)) {
    capH = g.box_h;
    ofs = g.ofs_y;
  }
  const int baseline = f->line_height - f->base_line;
  return capCY - (baseline - ofs - capH / 2);
}

}  // namespace

//-----------------------------------------------------------------------------
// Telemetry lookup helpers
//-----------------------------------------------------------------------------

int TelemetryDashViewMenu::findSensor(const char* name) const
{
  if (!name) return -1;
  for (int i = 0; i < MAX_TELEMETRY_SENSORS; i++) {
    // attitude is only sent a few times per second, so accept the last value
    // as long as the link has not timed out
    if (!telemetryItems[i].isAvailable() || telemetryItems[i].isOld()) continue;
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

// Angle in degrees; radian sensors (CRSF attitude) are scaled exactly like the
// yaapu script does with math.deg()
float TelemetryDashViewMenu::getAngleDegrees(int idx) const
{
  if (idx < 0) return -100000;
  float value = getSensorValue(idx);
  if (g_model.telemetrySensors[idx].unit == UNIT_RADIANS)
    value *= 180.0f / (float)M_PI;
  return value;
}

// Linear interpolation of one attitude angle between its last two samples: the
// drawn value travels at the rate of the last interval, half an interval ahead
// of a straight interpolation, so it arrives as the next sample lands and does
// not overrun the samples on a reversal. Headings take the short way round the
// circle, a negative sample means "no sensor" and is passed through untouched.
float TelemetryDashViewMenu::glideAngle(AngleGlide& g, float sample,
                                        uint32_t nowMs, bool heading)
{
  if (heading && sample < 0.0f) {
    g.seen = false;
    return sample;
  }

  if (!g.seen || sample != g.cur) {
    if (g.seen) {
      g.prev = g.cur;
      g.prevMs = g.curMs;
    } else {
      g.prev = sample;
      g.prevMs = nowMs;
      g.seen = true;
    }
    g.cur = sample;
    g.curMs = nowMs;
  }

  float d = g.cur - g.prev;
  if (heading) {
    d = fmodf(d, 360.0f);
    if (d < -180.0f) d += 360.0f;
    if (d >= 180.0f) d -= 360.0f;
  }

  uint32_t span = g.curMs - g.prevMs;
  if (span > (uint32_t)ATTITUDE_GLIDE_MAX_MS) span = ATTITUDE_GLIDE_MAX_MS;
  float f = 1.0f;
  if (span)
    f = (float)(nowMs - g.curMs) / (float)span + ATTITUDE_GLIDE_PHASE;
  if (f < 0.0f) f = 0.0f;
  if (f > 1.0f) f = 1.0f;  // a late sample holds, the low pass softens that

  float value = g.prev + d * f;
  if (heading) {
    value = fmodf(value, 360.0f);
    if (value < 0.0f) value += 360.0f;
  }
  return value;
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
  for (int i = 0; i < bufCount; i++) {
    free(bufs[i]);
    bufs[i] = nullptr;
  }
  bufCount = 0;
  dialBuf = nullptr;
}

lv_obj_t* TelemetryDashViewMenu::makeCanvas(coord_t x, coord_t y, coord_t w,
                                            coord_t h, bool alpha,
                                            uint8_t*& buf)
{
  buf = nullptr;
  lv_obj_t* c = lv_canvas_create(lvobj);
  lv_obj_set_pos(c, x, y);
  lv_obj_set_size(c, w, h);
  if (bufCount >= MAX_BUFS) return c;

  const size_t sz = alpha ? LV_CANVAS_BUF_SIZE_TRUE_COLOR_ALPHA(w, h)
                          : LV_CANVAS_BUF_SIZE_TRUE_COLOR(w, h);
  buf = (uint8_t*)malloc(sz);
  if (!buf) return c;
  bufs[bufCount++] = buf;
  lv_canvas_set_buffer(c, buf, w, h,
                       alpha ? LV_IMG_CF_TRUE_COLOR_ALPHA : LV_IMG_CF_TRUE_COLOR);
  if (alpha) {
    memset(buf, 0, sz);
  } else {
    const lv_color_t bg = rgb(C_BG);
    for (int i = 0; i < w * h; i++) ((lv_color_t*)buf)[i] = bg;
  }
  return c;
}

StaticText* TelemetryDashViewMenu::makeText(coord_t x, coord_t capCenterY,
                                            coord_t w, const char* txt,
                                            LcdFlags font, uint32_t color,
                                            lv_text_align_t align)
{
  const coord_t h = getFont(font)->line_height;
  auto* t = new StaticText(this, {x, textTop(font, capCenterY), w, h}, txt,
                           COLOR_THEME_QM_FG_INDEX, font);
  lv_obj_set_style_text_align(t->getLvObj(), align, LV_PART_MAIN);
  lv_obj_set_style_text_color(t->getLvObj(), rgb(color), LV_PART_MAIN);
  return t;
}

lv_obj_t* TelemetryDashViewMenu::makeRect(coord_t x, coord_t y, coord_t w,
                                          coord_t h, uint32_t color)
{
  lv_obj_t* r = lv_obj_create(lvobj);
  lv_obj_set_pos(r, x, y);
  lv_obj_set_size(r, w, h);
  lv_obj_set_style_radius(r, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(r, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(r, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_color(r, rgb(color), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_clear_flag(r, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
  return r;
}

// pts are absolute screen coordinates and must outlive the line object
lv_obj_t* TelemetryDashViewMenu::makeLine(lv_point_t* pts, uint16_t n,
                                          coord_t width, uint32_t color)
{
  lv_obj_t* l = lv_line_create(lvobj);
  lv_obj_set_pos(l, 0, 0);
  lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(l, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_line_width(l, width, LV_PART_MAIN);
  lv_obj_set_style_line_color(l, rgb(color), LV_PART_MAIN);
  lv_obj_set_style_line_opa(l, LV_OPA_COVER, LV_PART_MAIN);
  lv_line_set_points(l, pts, n);
  return l;
}

void TelemetryDashViewMenu::buildUI()
{
  // dial first: its opaque canvas must sit below the tapes and the panel
  buildDial();
  buildWings();
  buildTopBar();
  buildTape(spdTape, true, false);
  buildTape(homeAltTape, false, true);
  buildReadouts();
  buildGrid();

  // Controller selection overlay (hidden by default, toggled with ENT)
  const coord_t mh = 2 * getFont(FONT(BOLD))->line_height + S(8);
  menuText = new StaticText(this, {SX(90), SY(HUD_CY) - mh / 2, S(300), mh}, "",
                            COLOR_THEME_QM_FG_INDEX, FONT(BOLD));
  lv_obj_set_style_text_align(menuText->getLvObj(), LV_TEXT_ALIGN_CENTER,
                              LV_PART_MAIN);
  lv_obj_set_style_text_color(menuText->getLvObj(), lv_color_white(),
                              LV_PART_MAIN);
  lv_obj_set_style_bg_color(menuText->getLvObj(), lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(menuText->getLvObj(), LV_OPA_80, LV_PART_MAIN);
  lv_obj_set_style_pad_top(menuText->getLvObj(), S(4), LV_PART_MAIN);
  lv_obj_add_flag(menuText->getLvObj(), LV_OBJ_FLAG_HIDDEN);
}

//-----------------------------------------------------------------------------
// Top bar: [sat] flight mode / SAT: n      [lq bars] n% / [batt] n.nV
//-----------------------------------------------------------------------------

void TelemetryDashViewMenu::buildTopBar()
{
  // --- satellite icon, drawn diagonally like the reference ------------------
  {
    uint8_t* buf;
    const int w = S(SAT_ICON_W), h = S(SAT_ICON_H);
    makeCanvas(SX(SAT_ICON_X), SY(SAT_ICON_Y), w, h, false, buf);
    if (buf) {
      const Surf s{buf, w, h, false};
      const float cx = SF(21), cy = SF(20);
      const float k = (float)M_SQRT1_2;
      for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
          const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
          // u runs along the solar panels (up-right), v across them
          const float u = (dx - dy) * k, v = (dx + dy) * k;
          const float au = fabsf(u), av = fabsf(v);
          bool on = false;
          if (au <= SF(5) && av <= SF(7)) on = true;                       // body
          else if (au <= SF(8.5f) && av <= SF(1)) on = true;              // arms
          else if (au >= SF(8.5f) && au <= SF(19.5f) && av <= SF(5.5f)) { // panels
            const float cell = fmodf(au - SF(8.5f), SF(3.67f));
            on = av >= SF(4.2f) || au >= SF(18.3f) || au <= SF(9.7f) ||
                 cell < SF(1.1f) || av < SF(0.6f);
          }
          // signal waves below-right of the body
          const float wx = dx - SF(5) * k, wy = dy - SF(5) * k;
          const float wd = sqrtf(wx * wx + wy * wy);
          const float wv = (wx + wy) * k;
          if (wv > 0 && fabsf(wx - wy) * k < wv * 1.1f &&
              (fabsf(wd - SF(9)) < SF(1.0f) || fabsf(wd - SF(13.5f)) < SF(1.0f)))
            on = true;
          if (on) s.px(x, y, C_SAT);
        }
      }
    }
  }

  // --- battery icon: body filled with the transmitter battery level --------
  battIconCanvas = makeCanvas(SX(BATT_ICON_X), SY(BATT_ICON_Y), S(BATT_ICON_W),
                              S(BATT_ICON_H), false, battIconBuf);
  drawBatteryIcon(C_TXBATT_OK, 100);

  // flight mode, bare like the yaapu status bar, above the satellite count
  flightModeText = makeText(SX(SAT_TEXT_X), SY(TOP_LABEL_CY),
                            S(FLIGHT_MODE_W), "", FONT(BOLD), C_VALUE,
                            LV_TEXT_ALIGN_LEFT);
  satValue = makeText(SX(SAT_TEXT_X), SY(TOP_VALUE_CY), S(120), "SAT: --",
                      FONT(BOLD), C_VALUE, LV_TEXT_ALIGN_LEFT);

  // link quality: bars then percentage, laid out like the battery row below
  static const uint8_t lqBarH[LQ_BAR_COUNT] = {5, 7, 9, 11, 13};
  for (int i = 0; i < LQ_BAR_COUNT; i++) {
    lqBar[i] = makeRect(SX(LQ_BARS_R - LQ_BARS_W + i * LQ_BAR_SZ),
                        SY(LQ_BARS_BOTTOM - lqBarH[i]), S(LQ_BAR_W),
                        S(lqBarH[i]), C_LQ_OFF);
  }
  lqText = makeText(SX(BATT_TEXT_X), SY(TOP_LABEL_CY), S(70), "--%",
                    FONT(BOLD), C_VALUE, LV_TEXT_ALIGN_LEFT);
  battValue = makeText(SX(BATT_TEXT_X), SY(TOP_VALUE_CY), S(70), "--.-V",
                       FONT(BOLD), C_VALUE, LV_TEXT_ALIGN_LEFT);

  // rules under the top bar, bending down into the 60 degree roll marks
  const float a60 = 60.0f * (float)M_PI / 180.0f;
  const int re = HUD_R + 4;
  const int ex = (int)lroundf(re * sinf(a60)), ey = (int)lroundf(re * cosf(a60));
  for (int side = 0; side < 2; side++) {
    const int sgn = side == 0 ? -1 : 1;
    rulePts[side][0] = {(lv_coord_t)SX(HUD_CX + sgn * 225), (lv_coord_t)SY(RULE_Y)};
    rulePts[side][1] = {(lv_coord_t)SX(HUD_CX + sgn * 112), (lv_coord_t)SY(RULE_Y)};
    rulePts[side][2] = {(lv_coord_t)SX(HUD_CX + sgn * ex),
                        (lv_coord_t)SY(HUD_CY - ey)};
    makeLine(rulePts[side], 3, S(2), C_RULE);
  }
}

//-----------------------------------------------------------------------------
// Decorative side wings: mesh panel with a glowing cyan bracket
//-----------------------------------------------------------------------------

void TelemetryDashViewMenu::buildWings()
{
  const int w = S(WING_W), h = S(WING_H);
  uint8_t* lbuf;
  uint8_t* rbuf;
  makeCanvas(SX(WING_X), SY(WING_Y), w, h, false, lbuf);
  makeCanvas(SX(DES_W - WING_X - WING_W), SY(WING_Y), w, h, false, rbuf);
  if (!lbuf) return;

  const Surf s{lbuf, w, h, false};
  // design-space coordinates relative to the canvas
  auto X = [](float x) { return SF(x - WING_X); };
  auto Y = [](float y) { return SF(y - WING_Y); };

  const float px[4] = {X(12), X(49), X(49), X(14)};
  const float py[4] = {Y(77), Y(98), Y(192), Y(212)};

  // mesh fill: dark 2x2 dots on a lighter grid, alternate rows shifted
  s.poly(px, py, 4, C_WING_FILL);
  const int cell = S(3) > 2 ? S(3) : 3;
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      const lv_color_t* p = (const lv_color_t*)lbuf + y * w + x;
      if (p->full != rgb(C_WING_FILL).full) continue;
      const int gx = x + ((y / cell) & 1) * (cell / 2 + 1);
      if ((gx % cell) < cell - 1 && (y % cell) < cell - 1) s.px(x, y, C_WING_DOT);
    }
  }
  s.outline(px, py, 4, C_WING_EDGE, S(2));

  // cyan bracket: dim arms, bright vertical bar
  s.line(X(15), Y(116), X(39), Y(126), C_WING_CYAN_DIM, S(3));
  s.line(X(39), Y(166), X(15), Y(179), C_WING_CYAN_DIM, S(3));
  s.rect((int)X(38), (int)Y(126), (int)X(44), (int)Y(166), C_WING_CYAN_DIM);
  s.rect((int)X(39), (int)Y(127), (int)X(43), (int)Y(165), C_WING_CYAN);

  // tan accents above and below
  s.line(X(12), Y(66), X(48), Y(87), C_WING_TAN, S(2));
  s.line(X(14), Y(221), X(51), Y(201), C_WING_TAN, S(2));

  if (rbuf) {
    auto* src = (const lv_color_t*)lbuf;
    auto* dst = (lv_color_t*)rbuf;
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++) dst[y * w + x] = src[y * w + (w - 1 - x)];
  }
}

//-----------------------------------------------------------------------------
// Centre dial: circular artificial horizon + compass rose
//-----------------------------------------------------------------------------

void TelemetryDashViewMenu::buildDial()
{
  dialCanvas =
      makeCanvas(SX(HUD_X), SY(HUD_Y), S(HUD_W), S(HUD_H), false, dialBuf);
  drawDial();
}

// The icon body fills from the bottom up with the transmitter battery level,
// as the FPV dashboard header fills its own icon sideways.
void TelemetryDashViewMenu::drawBatteryIcon(uint32_t color, int pct)
{
  if (!battIconBuf) return;
  const Surf s{battIconBuf, S(BATT_ICON_W), S(BATT_ICON_H), false};

  const int bodyTop = S(3), bodyBottom = S(23);
  s.rect(S(1), bodyTop, S(14), bodyBottom, C_BATT_EMPTY);
  const int fill = (bodyBottom - bodyTop + 1) * pct / 100;
  if (fill > 0) s.rect(S(1), bodyBottom - fill + 1, S(14), bodyBottom, color);
  // the terminal only lights up at full charge
  s.rect(S(5), 0, S(10), S(2), pct >= 100 ? color : C_BATT_EMPTY);

  const float bx[6] = {SF(9.5f), SF(4.5f), SF(7.5f), SF(6), SF(11.5f), SF(8.5f)};
  const float by[6] = {SF(6), SF(14), SF(14), SF(21), SF(11.5f), SF(11.5f)};
  s.poly(bx, by, 6, C_BATT_BOLT);

  lv_obj_invalidate(battIconCanvas);
}

void TelemetryDashViewMenu::drawDial()
{
  if (!dialBuf) return;
  const int W = S(HUD_W), H = S(HUD_H);
  const int R = S(HUD_R);
  const float cx = W / 2.0f, cy = H / 2.0f;
  const Surf s{dialBuf, W, H, false};

  {
    const lv_color_t bg = rgb(C_BG);
    for (int i = 0; i < W * H; i++) ((lv_color_t*)dialBuf)[i] = bg;
  }

  // The horizon tilts against the roll, exactly like the yaapu HUD does (it
  // delegates to lcd.drawHudRectangle(), which uses angle = tan(-roll)); the
  // roll scale and its bug stay fixed to the case.
  const float rollRad = -lastRoll * (float)M_PI / 180.0f;
  const float cosR = cosf(rollRad), sinR = sinf(rollRad);
  const float ppd = SF(PITCH_PX10) / 10.0f;
  const float pitchOff = lastPitch * ppd;
  // horizon frame (sx along the horizon, ly below it) -> canvas pixels
  auto hx = [&](float sx, float ly) { return cx + sx * cosR - ly * sinR; };
  auto hy = [&](float sx, float ly) { return cy + sx * sinR + ly * cosR; };
  auto inDial = [&](float x, float y, float r) {
    return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r;
  };

  // ---- sky / ground ------------------------------------------------------
  // the gradient is a function of the distance below the horizon, so every row
  // only has to be walked across the part of it that is inside the dial
  const float gradLen = SF(85);
  for (int y = 0; y < H; y++) {
    const float dy = y + 0.5f - cy;
    const float rem = (float)R * R - dy * dy;
    if (rem < 0) continue;
    const float half = sqrtf(rem);
    int x0 = (int)(cx - half), x1 = (int)(cx + half) + 1;
    if (x0 < 0) x0 = 0;
    if (x1 >= W) x1 = W - 1;
    for (int x = x0; x <= x1; x++) {
      const float dx = x + 0.5f - cx;
      if (dx * dx + dy * dy > (float)R * R) continue;
      const float hh = (-dx * sinR + dy * cosR) - pitchOff;
      if (hh < 0) {
        s.px(x, y, C_SKY);
      } else {
        const float t = hh >= gradLen ? 1.0f : hh / gradLen;
        s.px(x, y, lerpColor(C_GROUND_HI, C_GROUND_LO, t));
      }
    }
  }

  // ---- pitch ladder, every 2.5 degrees -----------------------------------
  // the ladder stops above the heading pointer, which sits on the rose
  const float ladderBottom = cy + SF(33);
  for (int p10 = -200; p10 <= 200; p10 += 25) {
    if (p10 == 0) continue;
    const bool major = (p10 % 100) == 0;
    const float half = major ? SF(27) : ((p10 % 50) == 0 ? SF(14) : SF(9));
    const float ly = (lastPitch - p10 / 10.0f) * ppd;
    const int steps = (int)(2 * half);
    for (int i = 0; i <= steps; i++) {
      const float sx = -half + i;
      const float x = hx(sx, ly), y = hy(sx, ly);
      if (!inDial(x, y, R - 2) || y > ladderBottom) continue;
      s.px((int)x, (int)y, C_LADDER);
      if (major) s.px((int)x, (int)y + 1, C_LADDER);
    }
  }

  // horizon line
  for (int i = -R; i <= R; i++) {
    const float x = hx(i, pitchOff), y = hy(i, pitchOff);
    if (!inDial(x, y, R - 1)) continue;
    s.px((int)x, (int)y, C_LADDER);
    s.px((int)x, (int)y - 1, C_LADDER);
  }

  // pitch numbers, upright, on both sides of the major lines
  {
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.font = getFont(FONT(XS));
    dsc.color = rgb(C_LADDER);
    dsc.align = LV_TEXT_ALIGN_CENTER;
    const int tw = S(24);
    char txt[8];
    for (int p = -20; p <= 20; p += 10) {
      if (p == 0) continue;
      const float ly = (lastPitch - p) * ppd;
      snprintf(txt, sizeof(txt), "%d", abs(p));
      for (int side = -1; side <= 1; side += 2) {
        const float x = hx(side * SF(41), ly), y = hy(side * SF(41), ly);
        if (!inDial(x, y, R - S(10)) || y > ladderBottom) continue;
        lv_canvas_draw_text(dialCanvas, (lv_coord_t)(x - tw / 2),
                            textTop(FONT(XS), (coord_t)y), tw, &dsc, txt);
      }
    }
  }

  // ---- half compass rose ---------------------------------------------------
  const float ccx = cx, ccy = cy + SF(CMP_DY);
  const float Rc = SF(CMP_R);
  int ry0 = (int)(ccy - Rc), ry1 = (int)(ccy + Rc) + 1;
  int rx0 = (int)(ccx - Rc), rx1 = (int)(ccx + Rc) + 1;
  if (ry0 < 0) ry0 = 0;
  if (rx0 < 0) rx0 = 0;
  if (ry1 >= H) ry1 = H - 1;
  if (rx1 >= W) rx1 = W - 1;
  for (int y = ry0; y <= ry1; y++) {
    for (int x = rx0; x <= rx1; x++) {
      const float dx = x + 0.5f - ccx, dy = y + 0.5f - ccy;
      const float d2 = dx * dx + dy * dy;
      if (d2 > Rc * Rc) continue;
      s.px(x, y, d2 > (Rc - SF(2.5f)) * (Rc - SF(2.5f)) ? C_COMPASS_RIM : C_COMPASS);
    }
  }

  const float yaw = lastHdg >= 0 ? lastHdg : 0;
  auto screenAngle = [&](float heading) {
    float rel = fmodf(heading - yaw, 360.0f);
    if (rel < -180.0f) rel += 360.0f;
    if (rel >= 180.0f) rel -= 360.0f;
    return rel / CMP_SCALE;
  };
  // a tick every 22.5 degrees of heading, longer ones on the 45s
  for (int mk10 = 0; mk10 < 3600; mk10 += 225) {
    const float a = screenAngle(mk10 / 10) + (mk10 % 10) / 10.0f / CMP_SCALE;
    if (fabsf(a) > 75.0f) continue;
    const float t = a * (float)M_PI / 180.0f;
    const float len = (mk10 % 450 == 0) ? SF(7) : SF(5);
    const float r0 = Rc - SF(4), r1 = r0 - len;
    s.line(ccx + r0 * sinf(t), ccy - r0 * cosf(t), ccx + r1 * sinf(t),
           ccy - r1 * cosf(t), C_TICK, S(2));
  }

  if (lastHomeAngle >= 0) {
    const float a = screenAngle(lastHomeAngle);
    if (fabsf(a) <= 80.0f) {
      const float t = a * (float)M_PI / 180.0f;
      const float r = Rc - SF(17);
      const int x = (int)(ccx + r * sinf(t)), y = (int)(ccy - r * cosf(t));
      s.rect(x - S(2), y - S(2), x + S(2), y + S(2), C_ACCENT);
    }
  }

  {
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.font = getFont(FONT(XS));
    dsc.color = rgb(C_VALUE);
    dsc.align = LV_TEXT_ALIGN_CENTER;
    static const struct {
      int         a;
      const char* s;
    } card[4] = {{0, "N"}, {90, "E"}, {180, "S"}, {270, "W"}};
    const int tw = S(20);
    for (int i = 0; i < 4; i++) {
      const float a = screenAngle(card[i].a);
      if (fabsf(a) > 70.0f) continue;
      const float t = a * (float)M_PI / 180.0f;
      const float r = Rc - SF(19);
      const float x = ccx + r * sinf(t), y = ccy - r * cosf(t);
      lv_canvas_draw_text(dialCanvas, (lv_coord_t)(x - tw / 2),
                          textTop(FONT(XS), (coord_t)y), tw, &dsc, card[i].s);
    }
  }

  // orange heading pointer with a short needle into the rose
  {
    const float top = cy + SF(37), bot = cy + SF(49);
    s.tri(cx - SF(9), top - SF(1), cx + SF(9), top - SF(1), cx, bot + SF(2),
          C_OUTLINE);
    s.tri(cx - SF(7), top, cx + SF(7), top, cx, bot, C_ACCENT);
    s.rect((int)cx - S(1), (int)bot, (int)cx, (int)(cy + SF(64)), C_ACCENT);
  }

  // ---- fixed aircraft symbol ---------------------------------------------
  s.rect((int)(cx - SF(55)), (int)(cy - SF(3)), (int)(cx - SF(22)), (int)cy,
         C_WINGBAR);
  s.rect((int)(cx + SF(22)), (int)(cy - SF(3)), (int)(cx + SF(55)), (int)cy,
         C_WINGBAR);
  // hollow orange chevron with a small down arrow inside
  {
    const float vx[6] = {cx - SF(23), cx - SF(15), cx, cx + SF(15), cx + SF(23), cx};
    const float vy[6] = {cy - SF(1), cy - SF(1), cy + SF(12), cy - SF(1),
                         cy - SF(1), cy + SF(19)};
    s.poly(vx, vy, 6, C_OUTLINE);
    s.outline(vx, vy, 6, C_ACCENT, S(2));
  }
  s.rect((int)cx - S(1), (int)(cy + SF(1)), (int)cx, (int)(cy + SF(6)),
         C_WINGBAR);
  s.tri(cx - SF(4), cy + SF(6), cx + SF(4), cy + SF(6), cx, cy + SF(11),
        C_WINGBAR);

  // ---- roll scale, roll bug and fixed index ------------------------------
  static const int rollMarks[] = {10, 20, 30, 45, 60};
  for (int m : rollMarks) {
    const bool lng = (m == 30 || m == 60);
    const float r0 = R + SF(3), r1 = R + (lng ? SF(14) : SF(8));
    for (int sgn = -1; sgn <= 1; sgn += 2) {
      const float t = sgn * m * (float)M_PI / 180.0f;
      s.line(cx + r0 * sinf(t), cy - r0 * cosf(t), cx + r1 * sinf(t),
             cy - r1 * cosf(t), C_VALUE, lng ? S(3) : S(2));
    }
  }

  {
    // the roll bug is fixed to the case, so it slides along the scale in the
    // direction of the roll: that is the horizon frame rotated by +roll
    auto bx = [&](float sx, float ly) { return cx + sx * cosR + ly * sinR; };
    auto by = [&](float sx, float ly) { return cy - sx * sinR + ly * cosR; };
    const float tipO = R - SF(6), baseO = R - SF(25);
    s.tri(bx(-SF(11), -baseO), by(-SF(11), -baseO), bx(SF(11), -baseO),
          by(SF(11), -baseO), bx(0, -tipO), by(0, -tipO), C_OUTLINE);
    const float tip = R - SF(8), base = R - SF(23);
    s.tri(bx(-SF(9), -base), by(-SF(9), -base), bx(SF(9), -base),
          by(SF(9), -base), bx(0, -tip), by(0, -tip), C_ACCENT);
  }

  s.tri(cx - SF(9), cy - R - SF(13), cx + SF(9), cy - R - SF(13), cx,
        cy - R + SF(4), C_OUTLINE);
  s.tri(cx - SF(8), cy - R - SF(12), cx + SF(8), cy - R - SF(12), cx,
        cy - R + SF(2), C_VALUE);

  lv_obj_invalidate(dialCanvas);
}

//-----------------------------------------------------------------------------
// Vertical value tape
//-----------------------------------------------------------------------------

void TelemetryDashViewMenu::buildTape(Tape& t, bool left, bool altitude)
{
  t.left = left;
  t.x = SX(left ? TAPE_L_X : TAPE_R_X);
  t.y = SY(TAPE_Y);
  t.w = S(TAPE_W);
  t.h = S(left ? TAPE_L_H : TAPE_R_H);
  t.centerY = SY(HUD_CY);
  t.stepPx = S(TAPE_STEP_PX);
  t.numW = S(52);
  t.numX = left ? t.x + S(2) : t.x + S(19);
  t.tickX = left ? t.x + t.w - S(14) : t.x + S(TAPE_BAR_W);

  makeRect(t.x, t.y, t.w, t.h, C_TAPE);

  // value bar on the dial side: cyan above / orange below on the altitude
  // tape, orange below only on the speed tape
  const coord_t barX = left ? t.x + t.w - S(TAPE_BAR_W) : t.x;
  if (altitude) makeRect(barX, t.y, S(TAPE_BAR_W), t.centerY - t.y, C_TAPE_CYAN);
  makeRect(barX, t.centerY, S(TAPE_BAR_W), t.y + t.h - t.centerY, C_TAPE_ORANGE);

  for (int i = 0; i < 5; i++) {
    t.num[i] = makeText(t.numX, t.centerY, t.numW, "", FONT(XS), C_VALUE,
                        left ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_LEFT);
    lv_obj_add_flag(t.num[i]->getLvObj(), LV_OBJ_FLAG_HIDDEN);
  }
  for (int i = 0; i < 9; i++) {
    t.tick[i] = makeRect(t.tickX, t.y, S(6), S(2), C_VALUE);
    lv_obj_add_flag(t.tick[i], LV_OBJ_FLAG_HIDDEN);
  }

  // centre box: pointer towards the dial plus a taller window around the
  // last digit
  const int bx0 = left ? 56 : 352, by0 = 105;
  const int bw = left ? 65 : 69, bh = 43;
  uint8_t* buf;
  makeCanvas(SX(bx0), SY(by0), S(bw), S(bh), true, buf);
  if (buf) {
    const Surf s{buf, S(bw), S(bh), true};
    static const float lx[] = {57, 92, 92, 108, 108, 111, 119, 111, 108, 108, 92, 92, 57};
    static const float lyv[] = {114, 114, 106, 106, 114, 114, 126, 138, 138, 146, 146, 138, 138};
    static const float rx[] = {353, 363, 398, 398, 419, 419, 363};
    static const float ryv[] = {126, 114, 114, 106, 106, 138, 138};
    const float* xs = left ? lx : rx;
    const float* ys = left ? lyv : ryv;
    const int n = left ? 13 : 7;
    float px[13], py[13];
    for (int i = 0; i < n; i++) {
      px[i] = SF(xs[i] - bx0);
      py[i] = SF(ys[i] - by0);
    }
    s.poly(px, py, n, C_BOX_FILL);
    s.outline(px, py, n, C_BOX_EDGE, 1);
  }

  // the label reaches past the box so wide values are never wrapped
  const coord_t vx = left ? SX(30) : SX(340);
  const coord_t vr = left ? SX(107) : SX(417);
  t.boxValue = makeText(vx, t.centerY, vr - vx, "--", FONT(BOLD), C_VALUE,
                        LV_TEXT_ALIGN_RIGHT);
  lv_label_set_long_mode(t.boxValue->getLvObj(), LV_LABEL_LONG_CLIP);
  if (!left) {
    makeText(SX(380), SY(145), S(39), "m", FONT(XXS), C_READOUT,
             LV_TEXT_ALIGN_RIGHT);
  }
}

void TelemetryDashViewMenu::setBoxValue(Tape& t, const char* txt)
{
  // bold digits while they fit the box, a smaller font for longer values
  const LcdFlags font = strlen(txt) > 4 ? FONT(XS) : FONT(BOLD);
  lv_obj_t* obj = t.boxValue->getLvObj();
  lv_obj_set_style_text_font(obj, getFont(font), LV_PART_MAIN);
  lv_obj_set_height(obj, getFont(font)->line_height);
  lv_obj_set_y(obj, textTop(font, t.centerY));
  t.boxValue->setText(txt);
}

void TelemetryDashViewMenu::updateTape(Tape& t, float value, float step,
                                       bool valid, int dec)
{
  char txt[16];

  if (!valid) {
    setBoxValue(t, "--");
    for (int i = 0; i < 5; i++)
      lv_obj_add_flag(t.num[i]->getLvObj(), LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < 9; i++) lv_obj_add_flag(t.tick[i], LV_OBJ_FLAG_HIDDEN);
    return;
  }

  snprintf(txt, sizeof(txt), "%.*f", dec, value);
  setBoxValue(t, txt);

  // scale numbers every step, hidden behind the centre box
  const float base = roundf(value / step) * step;
  for (int i = 0; i < 5; i++) {
    const float v = base + (float)(i - 2) * step;
    const coord_t py = t.centerY - (coord_t)((v - value) * t.stepPx / step);
    if (abs(py - t.centerY) < S(26) || py < t.y + S(6) ||
        py > t.y + t.h - S(6)) {
      lv_obj_add_flag(t.num[i]->getLvObj(), LV_OBJ_FLAG_HIDDEN);
    } else {
      snprintf(txt, sizeof(txt), "%.0f", v);
      t.num[i]->setText(txt);
      lv_obj_set_pos(t.num[i]->getLvObj(), t.numX, textTop(FONT(XS), py));
      lv_obj_clear_flag(t.num[i]->getLvObj(), LV_OBJ_FLAG_HIDDEN);
    }
  }

  // ticks every half step
  const float half = step / 2;
  const float tbase = roundf(value / half) * half;
  for (int i = 0; i < 9; i++) {
    const float v = tbase + (float)(i - 4) * half;
    const coord_t py = t.centerY - (coord_t)((v - value) * t.stepPx / step);
    if (abs(py - t.centerY) < S(14) || py < t.y + S(2) ||
        py > t.y + t.h - S(4)) {
      lv_obj_add_flag(t.tick[i], LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_set_pos(t.tick[i], t.tickX, py - S(1));
      lv_obj_clear_flag(t.tick[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
}

//-----------------------------------------------------------------------------
// Numeric readouts under the tapes
//-----------------------------------------------------------------------------

void TelemetryDashViewMenu::buildReadouts()
{
  spdReadout = makeText(SX(TAPE_L_X - 10), SY(RO_VALUE_CY), S(TAPE_W + 38),
                        "--- km/h", FONT(XS), C_READOUT, LV_TEXT_ALIGN_CENTER);

  homeAltTitle = makeText(SX(RO_SPD_CX - 50), SY(RO_TITLE_CY), S(100),
                          "Home Alt:", FONT(XS), C_LABEL, LV_TEXT_ALIGN_CENTER);
  homeAltReadout = makeText(SX(RO_SPD_CX - 50), SY(RO_VALUE_CY), S(100),
                            "--- m", FONT(XS), C_READOUT,
                            LV_TEXT_ALIGN_CENTER);
}

//-----------------------------------------------------------------------------
// Bottom 3x2 value grid
//-----------------------------------------------------------------------------

void TelemetryDashViewMenu::buildGrid()
{
  static const char* labels[6] = {"SPD", "Dist", "BATT",
                                  "RPM1", "RPM2", "CURR"};

  const coord_t gx = SX(GRID_X), gy = SY(GRID_Y);
  const coord_t gw = S(GRID_W), gh = S(GRID_H);
  const coord_t cut = S(GRID_CUT);

  // fill: a cross of two rectangles so the cut corners stay background
  makeRect(gx + cut, gy, gw - 2 * cut, gh, C_PANEL);
  makeRect(gx, gy + cut, gw, gh - 2 * cut, C_PANEL);

  const coord_t gh1 = gh - 1, gw1 = gw - 1;
  gridPts[0] = {(lv_coord_t)(gx + cut), (lv_coord_t)gy};
  gridPts[1] = {(lv_coord_t)(gx + gw1 - cut), (lv_coord_t)gy};
  gridPts[2] = {(lv_coord_t)(gx + gw1), (lv_coord_t)(gy + cut)};
  gridPts[3] = {(lv_coord_t)(gx + gw1), (lv_coord_t)(gy + gh1 - cut)};
  gridPts[4] = {(lv_coord_t)(gx + gw1 - cut), (lv_coord_t)(gy + gh1)};
  gridPts[5] = {(lv_coord_t)(gx + cut), (lv_coord_t)(gy + gh1)};
  gridPts[6] = {(lv_coord_t)gx, (lv_coord_t)(gy + gh1 - cut)};
  gridPts[7] = {(lv_coord_t)gx, (lv_coord_t)(gy + cut)};
  gridPts[8] = gridPts[0];
  makeLine(gridPts, 9, S(2), C_PANEL_EDGE);

  // tan dividers, with a small gap where they cross
  const coord_t gap = S(4);
  const coord_t hy = SY(GRID_HDIV_Y);
  for (int c = 1; c < 3; c++) {
    const coord_t x = SX(GRID_COL[c]) - S(1);
    makeRect(x, gy + S(5), S(2), hy - gap - gy - S(5), C_DIV);
    makeRect(x, hy + gap, S(2), gy + gh - S(5) - hy - gap, C_DIV);
  }
  const coord_t hx0 = gx + S(15), hx1 = gx + gw - S(15);
  coord_t segX = hx0;
  for (int c = 1; c <= 3; c++) {
    const coord_t end = (c < 3) ? SX(GRID_COL[c]) - gap : hx1;
    makeRect(segX, hy - S(1), end - segX, S(2), C_DIV);
    segX = SX(GRID_COL[c < 3 ? c : 2]) + gap;
  }

  for (int i = 0; i < 6; i++) {
    const int c = i % 3, r = i / 3;
    const coord_t x0 = SX(GRID_COL[c]);
    const coord_t cw = SX(GRID_COL[c + 1]) - x0;
    makeText(x0, SY(GRID_LABEL_CY[r]), cw, labels[i],
             r == 0 ? FONT(STD) : FONT(XS), C_LABEL, LV_TEXT_ALIGN_CENTER);
    cellValue[i] = makeText(x0, SY(GRID_VALUE_CY[r]), cw, "---", FONT(BOLD),
                            C_VALUE, LV_TEXT_ALIGN_CENTER);
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

  // ArduPilot sends its home data in CRSF passthrough frames, iNav sends none,
  // so the two controllers use different sources for the home referred values
  const bool inav = controllerType == CONTROLLER_INAV;

  // ---- top bar -----------------------------------------------------------
  idx = findSensor("Sats");
  int sats = idx >= 0 ? (int)getSensorValue(idx) : -1;
  if (sats != lastSats && sats >= 0) {
    lastSats = sats;
    // yaapu's saturation marker: 15 is the top of the 4 bit field of the
    // ArduPilot passthrough frame, which iNav does not send
    const bool saturated = sats == 15 && !inav;
    snprintf(txt, sizeof(txt), saturated ? "SAT: 15+" : "SAT: %d", sats);
    satValue->setText(txt);
  }

  // flight mode text ("FM"); like the other readouts the last one stays on
  // screen once the sensor times out
  idx = findSensor("FM");
  if (idx >= 0) {
    char mode[TELEMETRY_SENSOR_TEXT_LENGTH + 1];
    memcpy(mode, telemetryItems[idx].text, TELEMETRY_SENSOR_TEXT_LENGTH);
    mode[TELEMETRY_SENSOR_TEXT_LENGTH] = '\0';
    if (mode[0] && flightModeText->getText() != mode)
      flightModeText->setText(mode);
  }

  // link quality ("RQly"), the FPV dashboard's bar thresholds
  idx = findSensor("RQly");
  int lq = idx >= 0 ? (int)getSensorValue(idx) : -1;
  if (lq != lastLq) {
    lastLq = lq;
    static const uint8_t lqThreshold[LQ_BAR_COUNT] = {30, 40, 50, 60, 80};
    for (int i = 0; i < LQ_BAR_COUNT; i++) {
      lv_obj_set_style_bg_color(
          lqBar[i], rgb(lq >= lqThreshold[i] ? C_LQ_ON : C_LQ_OFF),
          LV_PART_MAIN);
    }
    if (lq >= 0)
      snprintf(txt, sizeof(txt), "%d%%", lq);
    else
      snprintf(txt, sizeof(txt), "--%%");
    lqText->setText(txt);
  }

  // transmitter battery voltage, as on the FPV dashboard header
  int vbat = g_vbat100mV;
  if (vbat != lastVbat) {
    lastVbat = vbat;
    snprintf(txt, sizeof(txt), "%.1fV", vbat * 0.1f);
    battValue->setText(txt);
    // level and colours exactly as the FPV dashboard header computes them
    int pct = GET_TXBATT_BARS(BATT_LEVEL_BARS) * 100 / BATT_LEVEL_BARS;
    if (pct != battIconPct) {
      battIconPct = pct;
      drawBatteryIcon(pct >= 40 ? C_TXBATT_OK
                    : pct >= 20 ? C_TXBATT_LOW
                                : C_TXBATT_CRIT, pct);
    }
  }

  // ---- dial --------------------------------------------------------------
  // yaapu reads the CRSF attitude sensors Roll / Ptch / Yaw in radians and
  // converts them with math.deg(); Hdg is the yaw alias it publishes for the
  // non-CRSF case. Values are sampled every UI frame, but the sensors only
  // report a few frames per second, so each angle is glided linearly from one
  // sample to the next and then smoothed by a short low pass: the ball keeps
  // moving evenly between samples instead of moving in one lurch per sample,
  // without the latency of simply easing towards the newest sample.
  const uint32_t nowMs = time_get_ms();
  const uint32_t dtMs = lastEaseMs ? nowMs - lastEaseMs : 0;
  lastEaseMs = nowMs;
  // a long gap means the page was not running (a dialog was up), so snap
  float k = 1.0f;
  if (dtMs && dtMs < 5u * ATTITUDE_LAG_MS)
    k = 1.0f - expf(-(float)dtMs / (float)ATTITUDE_LAG_MS);
  // pitch, roll: plain low pass on the glided value
  auto ease = [&](float target, float current) {
    if (fabsf(target - current) < ATTITUDE_EPS) return target;
    return current + (target - current) * k;
  };
  // headings: same along the shortest way round the circle; a negative value
  // means "no sensor" and is passed straight through
  auto easeHeading = [&](float target, float current) {
    if (target < 0.0f || current < 0.0f) return target;
    float d = fmodf(target - current, 360.0f);
    if (d < -180.0f) d += 360.0f;
    if (d >= 180.0f) d -= 360.0f;
    if (fabsf(d) < HEADING_EPS) return target;
    float v = fmodf(current + d * k, 360.0f);
    if (v < 0.0f) v += 360.0f;
    return v;
  };

  idx = findMappedSensor("Yaw", "Hdg");
  float hdg = -1.0f;
  if (idx >= 0) {
    // yaw is signed, but drawDial() uses -1 as the "no sensor" marker
    hdg = fmodf(getAngleDegrees(idx), 360.0f);
    if (hdg < 0.0f) hdg += 360.0f;
  }
  idx = findSensor("Ptch");
  float pitch = idx >= 0 ? getAngleDegrees(idx) : 0.0f;
  idx = findSensor("Roll");
  float roll = idx >= 0 ? getAngleDegrees(idx) : 0.0f;

  // like yaapu, the home direction comes from the ArduPilot passthrough home
  // frame; iNav sends no home frame, so there the home point of the model's GPS
  // sensor is used. Both stay at -1 while no home bearing is known
  const int16_t homeBearing =
      inav ? getGpsHomeBearing() : getArduPilotHomeBearing();
  float homeAngle = homeBearing >= 0 ? (float)homeBearing : -1.0f;

  const float drawnHdg =
      easeHeading(glideAngle(glideHdg, hdg, nowMs, true), lastHdg);
  const float drawnPitch =
      ease(glideAngle(glidePitch, pitch, nowMs, false), lastPitch);
  const float drawnRoll =
      ease(glideAngle(glideRoll, roll, nowMs, false), lastRoll);
  const float drawnHome = easeHeading(
      glideAngle(glideHome, homeAngle, nowMs, true), lastHomeAngle);

  if (drawnHdg != lastHdg || drawnPitch != lastPitch ||
      drawnRoll != lastRoll || drawnHome != lastHomeAngle) {
    lastHdg = drawnHdg;
    lastPitch = drawnPitch;
    lastRoll = drawnRoll;
    lastHomeAngle = drawnHome;
    drawDial();
  }

  // ---- speed tape (metric) -----------------------------------------------
  idx = findForController("ASpd", "GSpd");
  const float spd = idx >= 0 ? getSensorValueIn(idx, UNIT_KMH, 1) : -100000;
  const bool spdValid = spd > -10000;
  // like yaapu: one decimal below 10 km/h, none above
  const bool spdDec = fabsf(spd) < 10.0f;
  if (fabsf(spd - lastSpd) > 0.05f) {
    lastSpd = spd;
    snprintf(txt, sizeof(txt),
             spdValid ? (spdDec ? "%.1f km/h" : "%.0f km/h") : "--- km/h", spd);
    spdReadout->setText(txt);
    updateTape(spdTape, spd, 10.0f, spdValid, spdDec ? 1 : 0);
  }

  // ---- altitude tape (metric) --------------------------------------------
  // the ArduPilot home frame carries the altitude above home; iNav sends no
  // home frame and no altitude above home either, so its absolute altitude is
  // shown instead
  float homeAlt = -100000.0f;
  if (inav) {
    idx = findMappedSensor("Alt", "GAlt");
    if (idx >= 0) homeAlt = getSensorValueIn(idx, UNIT_METERS, 0);
  } else {
    homeAlt = getArduPilotHomeAltitude();
  }
  const bool homeAltValid = homeAlt > -10000.0f;

  const char* altTitle = inav ? "Altitude:" : "Home Alt:";
  if (homeAltTitle->getText() != altTitle) homeAltTitle->setText(altTitle);

  if (fabsf(homeAlt - lastHomeAlt) > 0.4f) {
    lastHomeAlt = homeAlt;
    snprintf(txt, sizeof(txt), homeAltValid ? "%.0f m" : "--- m", homeAlt);
    homeAltReadout->setText(txt);
    updateTape(homeAltTape, homeAlt, 10.0f, homeAltValid);
  }

  // ---- bottom grid -------------------------------------------------------
  static const struct {
    const char* ap;
    const char* inav;
    uint8_t     unit;
    uint8_t     prec;
    const char* suffix;
  } cells[6] = {
      {"ASpd", "GSpd", UNIT_KMH, 1, "km/h"},  // horizontal speed
      {nullptr, nullptr, 0, 0, "m"},          // distance to home
      {"RxBt", "VFAS", UNIT_VOLTS, 1, "V"},   // aircraft battery voltage
      {"RPM", nullptr, UNIT_RPMS, 0, nullptr},
      {"RPM2", nullptr, UNIT_RPMS, 0, nullptr},
      {"Curr", "Curr", UNIT_AMPS, 1, "A"},   // aircraft current
  };

  for (int i = 0; i < 6; i++) {
    int prec = cells[i].prec;
    const char* unit = cells[i].suffix;
    int v = -100000;

    if (i == CELL_DIST) {
      // like yaapu: meters up to 999, above that km with two decimals
      const float dist =
          inav ? getGpsHomeDistance() : getArduPilotHomeDistance();
      if (dist > -10000.0f) {
        if (dist < 1000.0f) {
          v = (int)lroundf(dist);
        } else {
          prec = 2;
          unit = "km";
          v = (int)lroundf(dist * 0.1f);
        }
      }
    } else {
      const int si = findForController(cells[i].ap, cells[i].inav);
      if (si >= 0) {
        const float f = getSensorValueIn(si, cells[i].unit, cells[i].prec);
        // getSensorValueIn() returns -100000 for a missing sensor, rpm can be
        // negative (reverse thrust)
        if (f > -100000.0f) {
          // like yaapu: amps and speeds keep one decimal below 10 and drop it
          // above
          if (prec == 1 && fabsf(f) >= 10.0f &&
              (cells[i].unit == UNIT_AMPS ||
               cells[i].unit == UNIT_METERS_PER_SECOND ||
               cells[i].unit == UNIT_KMH)) {
            prec = 0;
          }
          float sc = 1.0f;
          for (int p = 0; p < prec; p++) sc *= 10.0f;
          v = (int)lroundf(f * sc);
        }
      }
    }

    if (v != lastCells[i] || prec != lastCellPrec[i]) {
      lastCells[i] = v;
      lastCellPrec[i] = prec;
      if (v == -100000) {
        cellValue[i]->setText("---");
      } else {
        float sc = 1.0f;
        for (int p = 0; p < prec; p++) sc *= 10.0f;
        snprintf(txt, sizeof(txt), "%.*f%s", prec, v / sc, unit ? unit : "");
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
