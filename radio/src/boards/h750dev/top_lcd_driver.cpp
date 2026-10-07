/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

// H750DEV secondary SPI LCD — LVGL-powered status panel (ST7789 320×240 RGB565)
//
// Layout (px):
//  Y=  0.. 33  Header : "H750DEV"  |  model name        (navy)
//  Y= 34.. 35  Divider: cyan 2px
//  Y= 36..131  Row A  : TMR1 value | RSSI value + bar
//  Y=132..133  Divider: cyan 2px
//  Y=134..199  Row B  : VBAT value + bar | OPTIME
//  Y=200..202  Divider: cyan 3px
//  Y=203..239  Footer : "MODEL: XXX" centred (navy)
//
// LVGL display is registered lazily on first toplcdRefreshEnd() because
// boardInit() runs before lv_init().

#include "top_lcd_driver.h"
#include "lcd_driver_spi.h"
#include "board.h"

// The top-LCD connector has no SDO/MISO, so panel presence can't be probed over
// SPI at runtime. Use the TOPLCD build option instead: set it ON once the panel
// is physically attached, then rebuild & flash. When OFF the whole SPI top-LCD
// subsystem is skipped entirely (no SPI writes -> no UI stutter on the dev
// board). Must be defined before any BOOT guard so the bootloader compiles too.
#if defined(TOPLCD)
  #define TOPLCD_ENABLED true
#else
  #define TOPLCD_ENABLED false
#endif

#ifndef BOOT
#include "edgetx.h"
#include <lvgl/lvgl.h>
#include "os/task.h"
#endif

#include <string.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// LVGL display driver — registered lazily once lv_init() has run
// ---------------------------------------------------------------------------
#ifndef BOOT

// Fonts are LZ4-compressed etxLz4Font — must be accessed via getFont();
// using LV_FONT_DECLARE directly would cause a BusFault (wrong struct layout).
#include "fonts.h"

#define SPI_LCD_W   320
#define SPI_LCD_H   240
#define SPI_BUF_ROWS 20   // partial buf: 320×20×2 = 12.8 KB

static lv_color_t         _spi_buf[SPI_LCD_W * SPI_BUF_ROWS];
static lv_disp_draw_buf_t _draw_buf;
static lv_disp_drv_t      _disp_drv;
static lv_disp_t*         _disp = nullptr;
static bool               _screen_ready = false;

static lv_obj_t* _lbl_timer  = nullptr;
static lv_obj_t* _lbl_rssi   = nullptr;
static lv_obj_t* _bar_rssi   = nullptr;
static lv_obj_t* _lbl_vbat   = nullptr;
static lv_obj_t* _bar_bat    = nullptr;
static lv_obj_t* _lbl_optime = nullptr;
static lv_obj_t* _lbl_model  = nullptr;
static lv_obj_t* _lbl_footer = nullptr;

// Throttle the (optional) SPI top-LCD refresh so it doesn't run on every main
// GUI cycle. When no panel is connected this avoids re-rendering/SPI-writes
// every loop (which caused visible UI stutter).
static uint8_t _refresh_cnt = 0;
#define TOPLCD_REFRESH_DIV  4

// ---------------------------------------------------------------------------
// Flush callback — LVGL → SPI hardware
// ---------------------------------------------------------------------------
static void spi_flush_cb(lv_disp_drv_t* drv, const lv_area_t* area,
                         lv_color_t* px)
{
  uint32_t len = (uint32_t)(area->x2 - area->x1 + 1) *
                 (uint32_t)(area->y2 - area->y1 + 1);
  lcdSpiSetWindow((uint16_t)area->x1, (uint16_t)area->y1,
                  (uint16_t)area->x2, (uint16_t)area->y2);
  lcdSpiBeginData();
  lcdSpiPushBuf((const uint16_t*)px, len);
  lcdSpiEndData();
  lv_disp_flush_ready(drv);
}

// ---------------------------------------------------------------------------
// Color helpers
// ---------------------------------------------------------------------------
static inline lv_color_t rssiColor(uint32_t rssi)
{
  if (rssi >= 70) return lv_palette_main(LV_PALETTE_GREEN);
  if (rssi >= 45) return lv_palette_main(LV_PALETTE_ORANGE);
  return lv_palette_main(LV_PALETTE_RED);
}

static inline lv_color_t batColor(int bars)
{
  if (bars >= 7) return lv_palette_main(LV_PALETTE_GREEN);
  if (bars >= 3) return lv_palette_main(LV_PALETTE_ORANGE);
  return lv_palette_main(LV_PALETTE_RED);
}

// ---------------------------------------------------------------------------
// Helper: opaque container, no scrollbar / border / radius
// ---------------------------------------------------------------------------
static lv_obj_t* make_panel(lv_obj_t* parent, int16_t x, int16_t y,
                             int16_t w, int16_t h, lv_color_t bg)
{
  lv_obj_t* obj = lv_obj_create(parent);
  lv_obj_set_pos(obj, x, y);
  lv_obj_set_size(obj, w, h);
  lv_obj_set_style_bg_color(obj, bg, 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(obj, 0, 0);
  lv_obj_set_style_border_width(obj, 0, 0);
  lv_obj_set_style_radius(obj, 0, 0);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  return obj;
}

// ---------------------------------------------------------------------------
// Screen creation (called once after LVGL is ready)
// ---------------------------------------------------------------------------
static void create_screen()
{
  // Temporarily set _disp as default so lv_obj_create(NULL) allocates
  // the new screen into _disp's object tree, not the main display's.
  lv_disp_t* main_disp = lv_disp_get_default();
  lv_disp_set_default(_disp);
  lv_obj_t* scr = lv_obj_create(NULL);   // allocated under _disp
  lv_disp_set_default(main_disp);         // restore immediately

  static const lv_color_t C_NAVY   = {.full = 0x001A};
  static const lv_color_t C_BLACK  = {.full = 0x0000};
  static const lv_color_t C_PANEL  = {.full = 0x0140};
  static const lv_color_t C_CYAN   = {.full = 0x07FF};
  static const lv_color_t C_YELLOW = {.full = 0xFFE0};
  static const lv_color_t C_WHITE  = {.full = 0xFFFF};
  static const lv_color_t C_GRAY   = {.full = 0x8C71};
  static const lv_color_t C_DIVIDER= {.full = 0x0820};

  // Style the root screen
  lv_obj_set_style_bg_color(scr, C_BLACK, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);
  lv_obj_set_style_border_width(scr, 0, 0);
  lv_obj_set_style_radius(scr, 0, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  // ── Header (Y:0-33) ────────────────────────────────────────────────────
  lv_obj_t* hdr = make_panel(scr, 0, 0, 320, 34, C_NAVY);

  lv_obj_t* t_title = lv_label_create(hdr);
  lv_label_set_text(t_title, "H750DEV");
  lv_obj_set_style_text_color(t_title, C_CYAN, 0);
  lv_obj_set_style_text_font(t_title, getFont(FONT(STD)), 0);
  lv_obj_align(t_title, LV_ALIGN_LEFT_MID, 6, 0);

  _lbl_model = lv_label_create(hdr);
  lv_label_set_text(_lbl_model, "");
  lv_obj_set_style_text_color(_lbl_model, C_YELLOW, 0);
  lv_obj_set_style_text_font(_lbl_model, getFont(FONT(STD)), 0);
  lv_obj_align(_lbl_model, LV_ALIGN_RIGHT_MID, -6, 0);

  // ── Separator ───────────────────────────────────────────────────────────
  make_panel(scr, 0, 34, 320, 2, C_CYAN);

  // ── Row A (Y:36-131, 96px) ──────────────────────────────────────────────
  // Left: Timer
  lv_obj_t* panAL = make_panel(scr, 0, 36, 159, 96, C_BLACK);

  lv_obj_t* lbl_tmr = lv_label_create(panAL);
  lv_label_set_text(lbl_tmr, "TMR1");
  lv_obj_set_style_text_color(lbl_tmr, C_GRAY, 0);
  lv_obj_set_style_text_font(lbl_tmr, getFont(FONT(XS)), 0);
  lv_obj_set_pos(lbl_tmr, 6, 4);

  _lbl_timer = lv_label_create(panAL);
  lv_label_set_text(_lbl_timer, "00:00");
  lv_obj_set_style_text_color(_lbl_timer, C_WHITE, 0);
  lv_obj_set_style_text_font(_lbl_timer, getFont(FONT(XL)), 0);
  lv_obj_align(_lbl_timer, LV_ALIGN_BOTTOM_MID, 0, -4);

  // Vertical divider
  make_panel(scr, 159, 36, 2, 96, C_DIVIDER);

  // Right: RSSI
  lv_obj_t* panAR = make_panel(scr, 161, 36, 159, 96, C_BLACK);

  lv_obj_t* lbl_rssi_h = lv_label_create(panAR);
  lv_label_set_text(lbl_rssi_h, "RSSI");
  lv_obj_set_style_text_color(lbl_rssi_h, C_GRAY, 0);
  lv_obj_set_style_text_font(lbl_rssi_h, getFont(FONT(XS)), 0);
  lv_obj_set_pos(lbl_rssi_h, 6, 4);

  _lbl_rssi = lv_label_create(panAR);
  lv_label_set_text(_lbl_rssi, "--- dB");
  lv_obj_set_style_text_color(_lbl_rssi, lv_palette_main(LV_PALETTE_GREEN), 0);
  lv_obj_set_style_text_font(_lbl_rssi, getFont(FONT(STD)), 0);
  lv_obj_set_pos(_lbl_rssi, 6, 30);

  _bar_rssi = lv_bar_create(panAR);
  lv_bar_set_range(_bar_rssi, 0, 100);
  lv_bar_set_value(_bar_rssi, 0, LV_ANIM_OFF);
  lv_obj_set_size(_bar_rssi, 148, 14);
  lv_obj_align(_bar_rssi, LV_ALIGN_BOTTOM_MID, 0, -8);
  lv_obj_set_style_bg_color(_bar_rssi, lv_color_make(20, 20, 20), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(_bar_rssi, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(_bar_rssi, lv_palette_main(LV_PALETTE_GREEN),
                             LV_PART_INDICATOR);
  lv_obj_set_style_radius(_bar_rssi, 4, LV_PART_MAIN);
  lv_obj_set_style_radius(_bar_rssi, 4, LV_PART_INDICATOR);

  // ── Separator ───────────────────────────────────────────────────────────
  make_panel(scr, 0, 132, 320, 2, C_CYAN);

  // ── Row B (Y:134-199, 66px) ─────────────────────────────────────────────
  // Left: VBAT
  lv_obj_t* panBL = make_panel(scr, 0, 134, 159, 66, C_PANEL);

  lv_obj_t* lbl_bat_h = lv_label_create(panBL);
  lv_label_set_text(lbl_bat_h, "VBAT");
  lv_obj_set_style_text_color(lbl_bat_h, C_GRAY, 0);
  lv_obj_set_style_text_font(lbl_bat_h, getFont(FONT(XS)), 0);
  lv_obj_set_pos(lbl_bat_h, 6, 4);

  _lbl_vbat = lv_label_create(panBL);
  lv_label_set_text(_lbl_vbat, "0.0V");
  lv_obj_set_style_text_color(_lbl_vbat, lv_palette_main(LV_PALETTE_GREEN), 0);
  lv_obj_set_style_text_font(_lbl_vbat, getFont(FONT(STD)), 0);
  lv_obj_set_pos(_lbl_vbat, 6, 28);

  _bar_bat = lv_bar_create(panBL);
  lv_bar_set_range(_bar_bat, 0, 10);
  lv_bar_set_value(_bar_bat, 0, LV_ANIM_OFF);
  lv_obj_set_size(_bar_bat, 145, 10);
  lv_obj_align(_bar_bat, LV_ALIGN_BOTTOM_MID, 0, -5);
  lv_obj_set_style_bg_color(_bar_bat, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(_bar_bat, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(_bar_bat, lv_palette_main(LV_PALETTE_GREEN),
                             LV_PART_INDICATOR);
  lv_obj_set_style_radius(_bar_bat, 4, LV_PART_MAIN);
  lv_obj_set_style_radius(_bar_bat, 4, LV_PART_INDICATOR);

  // Vertical divider
  make_panel(scr, 159, 134, 2, 66, C_BLACK);

  // Right: Op Time
  lv_obj_t* panBR = make_panel(scr, 161, 134, 159, 66, C_PANEL);

  lv_obj_t* lbl_op_h = lv_label_create(panBR);
  lv_label_set_text(lbl_op_h, "OPTIME");
  lv_obj_set_style_text_color(lbl_op_h, C_GRAY, 0);
  lv_obj_set_style_text_font(lbl_op_h, getFont(FONT(XS)), 0);
  lv_obj_set_pos(lbl_op_h, 6, 4);

  _lbl_optime = lv_label_create(panBR);
  lv_label_set_text(_lbl_optime, "00:00:00");
  lv_obj_set_style_text_color(_lbl_optime, C_WHITE, 0);
  lv_obj_set_style_text_font(_lbl_optime, getFont(FONT(STD)), 0);
  lv_obj_set_pos(_lbl_optime, 6, 30);

  // ── Separator ───────────────────────────────────────────────────────────
  make_panel(scr, 0, 200, 320, 3, C_CYAN);

  // ── Footer (Y:203-239, 37px) ────────────────────────────────────────────
  lv_obj_t* ftr = make_panel(scr, 0, 203, 320, 37, C_NAVY);

  _lbl_footer = lv_label_create(ftr);
  lv_label_set_text(_lbl_footer, "");
  lv_obj_set_style_text_color(_lbl_footer, C_YELLOW, 0);
  lv_obj_set_style_text_font(_lbl_footer, getFont(FONT(STD)), 0);
  lv_obj_align(_lbl_footer, LV_ALIGN_CENTER, 0, 0);

  lv_disp_t* md = lv_disp_get_default();
  lv_disp_set_default(_disp);
  lv_scr_load(scr);
  lv_disp_set_default(md);
}

// ---------------------------------------------------------------------------
// Lazy LVGL init — called once from toplcdRefreshEnd() after lv_init()
// ---------------------------------------------------------------------------
static void lvgl_init_secondary()
{
  lv_disp_draw_buf_init(&_draw_buf, _spi_buf, NULL,
                        SPI_LCD_W * SPI_BUF_ROWS);

  lv_disp_drv_init(&_disp_drv);
  _disp_drv.draw_buf    = &_draw_buf;
  _disp_drv.flush_cb    = spi_flush_cb;
  _disp_drv.hor_res     = SPI_LCD_W;
  _disp_drv.ver_res     = SPI_LCD_H;
  _disp_drv.full_refresh = 0;

  lv_disp_t* main_disp = lv_disp_get_default();
  _disp = lv_disp_drv_register(&_disp_drv);
  if (main_disp) lv_disp_set_default(main_disp);

  // Detach from lv_timer_handler() auto-scheduling: mixing the main display's
  // async DMA flush with our synchronous SPI flush in the same timer tick
  // caused hard faults. We call lv_refr_now(_disp) manually instead.
  lv_timer_pause(_lv_disp_get_refr_timer(_disp));

  create_screen();
  _screen_ready = true;
}

#endif  // !BOOT

// ---------------------------------------------------------------------------
// State cache — avoids redundant LVGL calls when values haven't changed
// ---------------------------------------------------------------------------
#ifndef BOOT
static int32_t  _tmr1    = INT32_MIN;
static uint32_t _optime  = UINT32_MAX;
static uint32_t _rssi    = UINT32_MAX;
static uint32_t _vbat    = UINT32_MAX;
static int      _batBars = -1;
static bool     _batWarn = false;
static char     _model_name[11] = {};
#endif
static uint8_t  _blinkCtr    = 0;
static uint8_t  _blink_phase = 0xFF;

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void toplcdInit(void)
{
  if (!TOPLCD_ENABLED) return;
  lcdSpiInit();
  lcdSpiClear(0x0000);
}

void toplcdOff(void)
{
  if (!TOPLCD_ENABLED) return;
  lcdSpiClear(0x0000);
}

void toplcdRefreshStart(void)
{
  if (!TOPLCD_ENABLED) return;
  _blinkCtr++;
  uint8_t phase = (_blinkCtr >> 2) & 1;
#ifndef BOOT
  if (phase != _blink_phase) {
    _blink_phase = phase;
    if (_lbl_timer && _tmr1 < 0)       lv_obj_invalidate(_lbl_timer);
    if (_lbl_vbat  && _batWarn)        lv_obj_invalidate(_lbl_vbat);
  }
#else
  _blink_phase = phase;
#endif
}

void toplcdRefreshEnd(void)
{
  if (!TOPLCD_ENABLED) return;
#ifndef BOOT
  // Lazy LVGL registration — deferred because lv_init() runs after boardInit().
  if (!_disp && lv_is_initialized()) {
    lvgl_init_secondary();
  }
  // Update model name whenever g_model.header.name changes (live, no restart needed)
  if (_screen_ready && _lbl_model) {
    char mdl[11] = {};
    strncpy(mdl, g_model.header.name, 10);
    for (int i = 0; mdl[i]; i++)
      if (mdl[i] >= 'a' && mdl[i] <= 'z') mdl[i] -= 32;
    if (memcmp(mdl, _model_name, sizeof(mdl)) != 0) {
      memcpy(_model_name, mdl, sizeof(mdl));
      lv_label_set_text(_lbl_model, mdl);
      char footer[32];
      snprintf(footer, sizeof(footer), "MODEL: %s", mdl);
      lv_label_set_text(_lbl_footer, footer);
    }
  }
  // Only render once the screen is fully constructed, and throttle to avoid
  // SPI-writes every GUI cycle (caused visible UI stutter when no panel).
  if (_disp && _screen_ready && (++_refresh_cnt % TOPLCD_REFRESH_DIV) == 0) {
    lv_refr_now(_disp);
  }
#endif
}

void setTopFirstTimer(int32_t value)
{
#ifndef BOOT
  if (value == _tmr1 || !_lbl_timer) { _tmr1 = value; return; }
  _tmr1 = value;
  char buf[8];
  bool neg = (value < 0);
  int32_t secs = neg ? -value : value;
  int mins = (int)(secs / 60); if (mins > 99) mins = 99;
  int sec  = (int)(secs % 60);
  snprintf(buf, sizeof(buf), "%02d:%02d", mins, sec);
  lv_label_set_text(_lbl_timer, buf);
  lv_color_t c = (neg && _blink_phase) ? lv_palette_main(LV_PALETTE_RED)
                                        : lv_color_white();
  lv_obj_set_style_text_color(_lbl_timer, c, 0);
#endif
}

void setTopSecondTimer(uint32_t value)
{
#ifndef BOOT
  if (value == _optime || !_lbl_optime) { _optime = value; return; }
  _optime = value;
  char buf[12];
  uint32_t s    = value;
  uint32_t op_s = s % 60; s /= 60;
  uint32_t op_m = s % 60; s /= 60;
  uint32_t op_h = s > 99 ? 99 : s;
  snprintf(buf, sizeof(buf), "%02u:%02u:%02u",
           (unsigned)op_h, (unsigned)op_m, (unsigned)op_s);
  lv_label_set_text(_lbl_optime, buf);
#endif
}

void setTopRssi(uint32_t rssi)
{
#ifndef BOOT
  if (rssi == _rssi || !_lbl_rssi) { _rssi = rssi; return; }
  _rssi = rssi;
  char buf[10];
  snprintf(buf, sizeof(buf), "%3u dB", (unsigned)(rssi > 999 ? 999 : rssi));
  lv_color_t c = rssiColor(rssi);
  lv_label_set_text(_lbl_rssi, buf);
  lv_obj_set_style_text_color(_lbl_rssi, c, 0);
  lv_bar_set_value(_bar_rssi, (int32_t)(rssi > 100 ? 100 : rssi), LV_ANIM_OFF);
  lv_obj_set_style_bg_color(_bar_rssi, c, LV_PART_INDICATOR);
#endif
}

void setTopBatteryState(int state, uint8_t blinking)
{
#ifndef BOOT
  bool warn = (blinking != 0);
  if (state == _batBars && warn == _batWarn) return;
  _batBars = state; _batWarn = warn;
  if (!_bar_bat) return;
  lv_color_t c = batColor(state);
  lv_bar_set_value(_bar_bat, state, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(_bar_bat, c, LV_PART_INDICATOR);
  if (_lbl_vbat) {
    lv_color_t vc = (warn && _blink_phase)
                    ? lv_palette_main(LV_PALETTE_RED) : c;
    lv_obj_set_style_text_color(_lbl_vbat, vc, 0);
  }
#endif
}

void setTopBatteryValue(uint32_t volts)
{
#ifndef BOOT
  if (volts == _vbat || !_lbl_vbat) { _vbat = volts; return; }
  _vbat = volts;
  char buf[16];
  snprintf(buf, sizeof(buf), "%u.%uV",
           (unsigned)(volts / 10), (unsigned)(volts % 10));
  lv_label_set_text(_lbl_vbat, buf);
#endif
}
