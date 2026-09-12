/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

// Native ELRS parameter browser — replicates elrs.lua in C++.
// Protocol reference: https://github.com/ExpressLRS/ExpressLRS/blob/master/src/lua/elrs.lua

#include "elrs_param_browser.h"

#include "button.h"
#include "hal/rotary_encoder.h"
#include "crc.h"
#include "edgetx.h"
#include "etx_lv_theme.h"
#include "hal/module_driver.h"
#include "os/task.h"
#include "pulses/modules_helpers.h"
#include "pulses/pulses.h"
#include "static.h"
#include "telemetry/crossfire.h"
#include "telemetry/telemetry.h"
#include "audio.h"
#include "dialog.h"
#include "menu.h"
#include "view_main.h"

#include <cstring>
#include <cstdio>

// Per-row touch data
struct ElrsRowData {
  ElrsParamBrowser* browser;
  int               rowIdx;
};

// ---------------------------------------------------------------------------
// ELRS field/folder name → Chinese translation
// ---------------------------------------------------------------------------
static std::string elrsTranslateName(const std::string& name)
{
  // Lookup table keyed by ELRS firmware field names (English).
  // Translations come from the EdgeTX i18n system (STR_ELRS_*).
  static const struct { const char* en; const char* tr; } kNames[] = {
    { "Packet Rate",       STR_ELRS_PACKET_RATE },
    { "Telem Ratio",       STR_ELRS_TELEM_RATIO },
    { "Switch Mode",       STR_ELRS_SWITCH_MODE },
    { "Link Mode",         STR_ELRS_LINK_MODE },
    { "Model Match",       STR_ELRS_MODEL_MATCH },
    { "TX Power",          STR_ELRS_TX_POWER },
    { "MAX Power",         STR_ELRS_MAX_POWER },
    { "Max Power",         STR_ELRS_MAX_POWER },
    { "Dynamic Power",     STR_ELRS_DYNAMIC_POWER },
    { "Dynamic",           STR_ELRS_DYNAMIC },
    { "Fan Threshold",     STR_ELRS_FAN_THRESHOLD },
    { "Fan Thresh",        STR_ELRS_FAN_THRESH },
    { "VTX Administrator", STR_ELRS_VTX_ADMINISTRATOR },
    { "Band",              STR_ELRS_BAND },
    { "Channel",           STR_ELRS_CHANNEL },
    { "Pwr Lvl",           STR_ELRS_PWR_LVL },
    { "Pitmode",           STR_ELRS_PITMODE },
    { "Send VTX",          STR_ELRS_SEND_VTX },
    { "Send VTx",          STR_ELRS_SEND_VTX },
    { "Bind",              STR_ELRS_BIND },
    { "Loan Model",        STR_ELRS_LOAN_MODEL },
    { "Return Model",      STR_ELRS_RETURN_MODEL },
    { "WiFi Connectivity", STR_ELRS_WIFI_CONNECTIVITY },
    { "Enable WiFi",       STR_ELRS_ENABLE_WIFI },
    { "Enable Rx WiFi",    STR_ELRS_ENABLE_RX_WIFI },
    { "Enable VRx WiFi",   STR_ELRS_ENABLE_VRX_WIFI },
    { "Enable Backpack WiFi", STR_ELRS_ENABLE_BACKPACK_WIFI },
    { "Enable TX WiFi",    STR_ELRS_ENABLE_TX_WIFI },
    { "Backpack WiFi",     STR_ELRS_BACKPACK_WIFI },
    { "Backpack Bind",     STR_ELRS_BACKPACK_BIND },
    { "Backpack",          STR_ELRS_BACKPACK },
    { "DVCA",              STR_ELRS_DVCA },
    { "DVR Rec",           STR_ELRS_DVR_REC },
    { "DVR Srt Dly",       STR_ELRS_DVR_SRT_DLY },
    { "DVR Stp Dly",       STR_ELRS_DVR_STP_DLY },
    { "HT Enable",         STR_ELRS_HT_ENABLE },
    { "HT Start Channel",  STR_ELRS_HT_START_CHANNEL },
    { "Telemetry",         STR_ELRS_TELEMETRY },
    { "Version",           STR_ELRS_VERSION },
    { "RSSI Threshold",    STR_ELRS_RSSI_THRESHOLD },
    { "RSSI",              STR_ELRS_RSSI },
    { "SNR",               STR_ELRS_SNR },
    { "Ant. Mode",         STR_ELRS_ANT_MODE },
    { "UART Inverted",     STR_ELRS_UART_INVERTED },
    { "Uart Inverted",     STR_ELRS_UART_INVERTED },
    { "Force TLM Off",     STR_ELRS_FORCE_TLM_OFF },
    { "Receiver",          STR_ELRS_RECEIVER },
    { "BLE Joystick",      STR_ELRS_BLE_JOYSTICK },
    { "Update Firmware",   STR_ELRS_UPDATE_FIRMWARE },
    { "WiFi",              STR_ELRS_WIFI },
    { "VTX",               STR_ELRS_VTX },
    { "RX Freq",           STR_ELRS_RX_FREQ },
    { "Other Devices",     STR_ELRS_OTHER_DEVICES },
    { "Protocol",          STR_ELRS_PROTOCOL },
    { "Protocol2",         STR_ELRS_PROTOCOL2 },
    { "SBUS failsafe",     STR_ELRS_SBUS_FAILSAFE },
    { "Target SysID",      STR_ELRS_TARGET_SYSID },
    { "Source SysID",      STR_ELRS_SOURCE_SYSID },
    { "Tim Power",         STR_ELRS_TIM_POWER },
    { "Team Race",         STR_ELRS_TEAM_RACE },
  };
  for (const auto& e : kNames) {
    if (name == e.en) return e.tr;
    // Prefix match: e.g. "TX Power (50mW)" finds the translation for "TX Power"
    // and appends the suffix
    size_t len = strlen(e.en);
    if (name.size() > len && name.compare(0, len, e.en) == 0 && name[len] == ' ')
      return std::string(e.tr) + name.substr(len);
  }
  return name;
}

static void elrsBrowser_row_cb(lv_event_t* e)
{
  auto* d = (ElrsRowData*)lv_event_get_user_data(e);
  if (!d || !d->browser || d->browser->deleted()) return;
  d->browser->onRowTouch(d->rowIdx);
}

// ---------------------------------------------------------------------------
// Session-level parameter cache — avoids re-loading on every menu open
// (defined in elrs_param_browser.h, instantiated here)
ElrsParamCache g_elrsCache;

// ---------------------------------------------------------------------------
// Helper: apply ELRS dark-theme to a BaseDialog popup
// LVGL tree: ModalWindow-obj → content-obj → [header-label, form-obj → widgets…]
// ---------------------------------------------------------------------------
static void elrsDarkDialogStyle(lv_obj_t* root)
{
  if (!root || lv_obj_get_child_cnt(root) == 0) return;
  lv_obj_t* content = lv_obj_get_child(root, 0);
  if (!content) return;

  // Dialog box → dark background, thin dark border
  lv_obj_set_style_bg_color(content, lv_color_make(0x18, 0x18, 0x18), 0);
  lv_obj_set_style_bg_opa(content, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(content, lv_color_make(0x44, 0x44, 0x44), 0);

  uint32_t nContent = lv_obj_get_child_cnt(content);

  // First child = header label → green bg + black text (matches selected row)
  if (nContent > 0) {
    lv_obj_t* hdr = lv_obj_get_child(content, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_make(0x00, 0xA0, 0x00), 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(hdr, lv_color_black(), 0);
  }

  // Second child = form → dark bg; style all direct children
  if (nContent > 1) {
    lv_obj_t* frm = lv_obj_get_child(content, 1);
    lv_obj_set_style_bg_color(frm, lv_color_make(0x18, 0x18, 0x18), 0);
    lv_obj_set_style_bg_opa(frm, LV_OPA_COVER, 0);

    for (uint32_t i = 0; i < lv_obj_get_child_cnt(frm); i++) {
      lv_obj_t* w = lv_obj_get_child(frm, i);
      lv_obj_set_style_bg_color(w, lv_color_make(0x18, 0x18, 0x18), 0);
      lv_obj_set_style_bg_opa(w, LV_OPA_COVER, 0);
      lv_obj_set_style_text_color(w, lv_color_white(), 0);
      // Grandchildren (e.g. ConfirmDialog button row container)
      for (uint32_t j = 0; j < lv_obj_get_child_cnt(w); j++) {
        lv_obj_t* gc = lv_obj_get_child(w, j);
        lv_obj_set_style_bg_color(gc, lv_color_make(0x28, 0x28, 0x28), 0);
        lv_obj_set_style_bg_opa(gc, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(gc, lv_color_white(), 0);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Layout constants
// ---------------------------------------------------------------------------

static LAYOUT_VAL_SCALED(TITLE_H,  28)
static LAYOUT_VAL_SCALED(ROW_H,    36)
static LAYOUT_VAL_SCALED(COL2_W,  230)
static LAYOUT_VAL_SCALED(PAD_ROW,   4)

// ---------------------------------------------------------------------------
// Helper: find which module slot runs CRSF
// ---------------------------------------------------------------------------

uint8_t ElrsParamBrowser::crsfModuleIdx()
{
  for (uint8_t m = 0; m < NUM_MODULES; m++) {
    if (moduleState[m].protocol == PROTOCOL_CHANNELS_CROSSFIRE)
      return m;
  }
  return 0xFF;
}

uint8_t ElrsParamBrowser::crsfEndpoint(uint8_t modIdx)
{
  // internal module → endpoint 0, external → TELEMETRY_ENDPOINT_SPORT
  return (modIdx == INTERNAL_MODULE) ? 0 : TELEMETRY_ENDPOINT_SPORT;
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

ElrsParamBrowser::ElrsParamBrowser(Window* parent) :
    Window(parent, {0, 0, LCD_W, LCD_H})
{
  setWindowFlag(OPAQUE);
  etx_solid_bg(lvobj, COLOR_BLACK_INDEX);

  pushLayer();

  buildUI();

  registerTelemetryQueue(&_rxQueue);

  // Restore from session cache if available
  if (g_elrsCache.valid) {
    _deviceId   = g_elrsCache.deviceId;
    _fieldCount = g_elrsCache.fieldCount;
    _fields     = g_elrsCache.fields;
    _state      = State::READY;
    _listDirty  = true;
    _deviceName    = g_elrsCache.devName;
    _deviceVersion = g_elrsCache.devVersion;
    lv_label_set_text(_titleLabel, _deviceName.c_str());
    // Restore _devices list from fields: extract virtual FT_DEVICE entries
    for (const auto& f : _fields) {
      if (f.type == FT_DEVICE && f.id != 0xFD) {
        // This is a cached device entry from a previous "Other Devices" folder
        bool found = false;
        for (auto& d : _devices) {
          if (d.id == f.id) { found = true; break; }
        }
        if (!found) {
          _devices.push_back({f.id, f.name, 0, false, f.strValue});
        }
      }
    }
    rebuildDeviceVirtualFields();
    _hasConnected = true;
    if (_folderLabel)  lv_obj_add_flag(_folderLabel, LV_OBJ_FLAG_HIDDEN);
    // _goodBadLabel already visible from updateStatus()
  } else if (!isModuleCrossfire(INTERNAL_MODULE) && !isModuleCrossfire(EXTERNAL_MODULE)) {
    // Neither module slot is configured as CRSF in the model—show setup prompt
    showModuleSetupMenu();
  } else {
    // Start discovery
    _state       = State::PINGING;
    _pingTimeout = get_tmr10ms() + 200;  // 2 s
    sendPing();
  }
}

ElrsParamBrowser::~ElrsParamBrowser()
{
  // Close any open command dialog to avoid dangling pointer
  if (_cmdDialog) {
    _cmdDialog->setCloseHandler(nullptr);
    _cmdDialog->deleteLater();
    _cmdDialog = nullptr;
  }
  deregisterTelemetryQueue(&_rxQueue);
  for (auto* d : _rowClickData) delete (ElrsRowData*)d;
  _rowClickData.clear();
}

void ElrsParamBrowser::onCancel()
{
  // If a command dialog is open, RTN sends CRSF cancel (lcsCancel=5) to device
  // then closes the dialog — mirroring the Lua script behaviour.
  if (_cmdDialog) {
    uint8_t cancelStatus = 5;
    sendParamWrite(_cmdFieldId, &cancelStatus, 1);
    _cmdDialog->setCloseHandler(nullptr);
    _cmdDialog->deleteLater();
    _cmdDialog  = nullptr;
    _cmdLastStatus = 0;
    _cmdFieldId = 0;
    _listDirty = true;
    return;
  }
  if (_editMode) {
    // Cancel ongoing value edit — reload from device
    _editMode = false;
    Field* f = visibleField(_selectedIdx);
    if (f) {
      _loadQueue.push_back(f->id);
      _state = State::LOADING;
      _fieldTimeout = get_tmr10ms();
    }
    _listDirty = true;
  } else {
    goBack();
  }
}

void ElrsParamBrowser::onClicked()
{
  handleKey(LV_KEY_ENTER);
}

// ---------------------------------------------------------------------------
// Build initial UI widgets
// ---------------------------------------------------------------------------

void ElrsParamBrowser::buildUI()
{
  // Strip LVGL default padding/border/scroll from the outer window so
  // child widgets fill the full screen without being clipped or shifted.
  lv_obj_set_style_pad_all(lvobj, 0, 0);
  lv_obj_set_style_border_width(lvobj, 0, 0);
  lv_obj_clear_flag(lvobj, LV_OBJ_FLAG_SCROLLABLE);

  coord_t y = 0;

  // Title bar
  auto titleBar = lv_obj_create(lvobj);
  lv_obj_set_pos(titleBar, 0, 0);
  lv_obj_set_size(titleBar, LCD_W, TITLE_H);
  etx_solid_bg(titleBar, COLOR_DARKGREY_INDEX);
  lv_obj_clear_flag(titleBar, LV_OBJ_FLAG_CLICKABLE);
  y += TITLE_H;

  _titleLabel = lv_label_create(titleBar);
  lv_obj_align(_titleLabel, LV_ALIGN_LEFT_MID, 4, 0);
  lv_label_set_text(_titleLabel, STR_ELRS_SEARCHING);
  lv_label_set_long_mode(_titleLabel, LV_LABEL_LONG_CLIP);
  lv_obj_set_width(_titleLabel, LCD_W / 2);
  etx_txt_color(_titleLabel, COLOR_WHITE_INDEX);

  // Unused status label (kept hidden)
  _statusLabel = lv_label_create(titleBar);
  lv_label_set_text(_statusLabel, "");
  lv_obj_add_flag(_statusLabel, LV_OBJ_FLAG_HIDDEN);

  // Packet counter label (right side of title bar)
  _goodBadLabel = lv_label_create(titleBar);
  lv_obj_align(_goodBadLabel, LV_ALIGN_RIGHT_MID, -(TITLE_H * 6 + 16), 0);
  lv_label_set_text(_goodBadLabel, "");
  lv_obj_set_style_text_color(_goodBadLabel, lv_color_make(0x9F, 0xC7, 0x6F), 0);  // ELRS green
  lv_obj_add_flag(_goodBadLabel, LV_OBJ_FLAG_HIDDEN);  // hidden until READY

  // Folder name — centered in title bar, updated when entering/leaving folders
  _folderLabel = lv_label_create(titleBar);
  lv_obj_align(_folderLabel, LV_ALIGN_CENTER, 0, 0);
  lv_label_set_text(_folderLabel, "");
  lv_obj_set_style_text_font(_folderLabel, LV_FONT_DEFAULT, 0);
  etx_txt_color(_folderLabel, COLOR_WHITE_INDEX);
  lv_label_set_long_mode(_folderLabel, LV_LABEL_LONG_CLIP);
  lv_obj_set_width(_folderLabel, LCD_W - 2 * (TITLE_H * 4 + 8));
  lv_obj_add_flag(_folderLabel, LV_OBJ_FLAG_HIDDEN);  // hidden until READY

  // Close button (top-right corner)
  auto closeBtn = new TextButton(
      this, {LCD_W - TITLE_H * 2, 0, TITLE_H * 2, TITLE_H}, STR_ELRS_CLOSE,
      [this]() -> uint8_t {
        deleteLater();
        return 0;
      });
  lv_group_remove_obj(closeBtn->getLvObj());
  _closeBtnObj = closeBtn->getLvObj();

  // Reload button
  auto reloadBtn = new TextButton(
      this, {LCD_W - TITLE_H * 4 - 4, 0, TITLE_H * 2, TITLE_H}, STR_ELRS_RELOAD,
      [this]() -> uint8_t { doReload(); return 0; });
  lv_group_remove_obj(reloadBtn->getLvObj());
  _reloadBtnObj = reloadBtn->getLvObj();

  // Default button (333Hz / FULL / 16CH)
  auto defaultBtn = new TextButton(
      this, {LCD_W - TITLE_H * 6 - 8, 0, TITLE_H * 2, TITLE_H}, STR_ELRS_DEFAULTS,
      [this]() -> uint8_t { applyDefaultSettings(); return 0; });
  lv_group_remove_obj(defaultBtn->getLvObj());
  _defaultBtnObj = defaultBtn->getLvObj();

  // Dark style; green when encoder-selected
  for (lv_obj_t* btnObj : {_closeBtnObj, _reloadBtnObj, _defaultBtnObj}) {
    lv_obj_set_style_bg_color(btnObj, lv_color_make(0x18, 0x18, 0x18), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btnObj, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(btnObj, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btnObj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(btnObj, 0, LV_PART_MAIN | LV_STATE_FOCUS_KEY);
    lv_obj_set_style_radius(btnObj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    // Focused = encoder selected -> green highlight (matches selected row)
    lv_obj_set_style_bg_color(btnObj, lv_color_make(0x00, 0xA0, 0x00), LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_text_color(btnObj, lv_color_black(), LV_PART_MAIN | LV_STATE_FOCUSED);
    // Pressed -> slightly brighter dark
    lv_obj_set_style_bg_color(btnObj, lv_color_make(0x28, 0x28, 0x28), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(btnObj, lv_color_white(), LV_PART_MAIN | LV_STATE_PRESSED);
  }

  // Inline load progress bar
  lv_obj_t* progBg = lv_obj_create(lvobj);
  lv_obj_set_pos(progBg, 0, y);
  lv_obj_set_size(progBg, LCD_W, 4);
  lv_obj_set_style_bg_color(progBg, lv_color_make(0x30, 0x30, 0x30), 0);
  lv_obj_set_style_bg_opa(progBg, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(progBg, 0, 0);
  lv_obj_set_style_pad_all(progBg, 0, 0);
  lv_obj_clear_flag(progBg, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(progBg, LV_OBJ_FLAG_CLICKABLE);

  _loadBar = lv_obj_create(progBg);
  lv_obj_set_pos(_loadBar, 0, 0);
  lv_obj_set_size(_loadBar, 0, 4);
  lv_obj_set_style_bg_color(_loadBar, lv_color_make(0x43, 0x61, 0xAA), 0);  // ELRS blue
  lv_obj_set_style_bg_opa(_loadBar, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(_loadBar, 0, 0);
  lv_obj_set_style_pad_all(_loadBar, 0, 0);
  lv_obj_clear_flag(_loadBar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(_loadBar, LV_OBJ_FLAG_HIDDEN);  // hidden until loading starts
  y += 4;
  _list = lv_obj_create(lvobj);
  lv_obj_set_pos(_list, 0, y);
  lv_obj_set_size(_list, LCD_W, LCD_H - y);  // fill to bottom, no status bar
  lv_obj_set_style_bg_color(_list, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(_list, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(_list, 0, 0);
  lv_obj_set_style_pad_all(_list, 0, 0);
  lv_obj_set_style_pad_row(_list, PAD_ROW, 0);
  lv_obj_set_flex_flow(_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(_list, LV_DIR_VER);
}

// ---------------------------------------------------------------------------
// Periodic update (called by LVGL task ~10ms)
// ---------------------------------------------------------------------------

void ElrsParamBrowser::checkEvents()
{
  Window::checkEvents();

  // Poll rotary encoder directly (bypasses LVGL group/editing system)
  {
    rotenc_t cur = rotaryEncoderGetValue();
    if (!_encInitialized) {
      _prevEncVal = cur;
      _encInitialized = true;
    } else {
      int32_t diff = (int32_t)(cur - _prevEncVal);
      if (diff != 0) {
        _prevEncVal = cur;
        if (diff > 0) {
          for (int32_t i = 0; i < diff; i++) handleKey(LV_KEY_RIGHT);
        } else {
          for (int32_t i = 0; i < -diff; i++) handleKey(LV_KEY_LEFT);
        }
      }
    }
  }

  processRxQueue();

  if (_shouldClose) {
    deleteLater();
    return;
  }

  tmr10ms_t now = get_tmr10ms();

  switch (_state) {
    case State::PINGING:
      if (now > _pingTimeout) {
        if (_fields.empty()) {
          // Retry ping
          _pingTimeout = now + 200;
          sendPing();
        }
      }
      break;

    case State::LOADING:
      if (now > _fieldTimeout && !_loadQueue.empty()) {
        uint8_t fid = _loadQueue.back();
        // Running command: poll with lcsQuery=6
        bool isRunningCmd = (_cmdDialog && fid == _cmdFieldId &&
                             fid >= 1 && fid <= (uint8_t)_fields.size() &&
                             _fields[fid - 1].type == FT_COMMAND);
        if (isRunningCmd) {
          uint8_t query = 6;  // lcsQuery
          sendParamWrite(fid, &query, 1);
        } else {
          sendParamRead(fid, _fieldChunk);
        }
        // 0.5s local / 5s RF link
        _fieldTimeout = now + (_handsetId == CRSF_RADIO_ADDR ? 50 : 500);
      }
      if (_loadQueue.empty()) {
        // Command still running — keep polling
        if (_cmdDialog && _cmdFieldId >= 1 && _cmdFieldId <= (uint8_t)_fields.size()) {
          const Field& cf = _fields[_cmdFieldId - 1];
          if (cf.type == FT_COMMAND && (cf.cmdStatus == 1 || cf.cmdStatus == 2)) {
            _loadQueue.push_back(_cmdFieldId);
            // Use device-specified timeout (cmdTimeout in 10ms units), min 25
            uint8_t devTimeout = (cf.cmdTimeout > 0) ? cf.cmdTimeout : 25;
            _fieldTimeout = now + devTimeout;
            break;
          }
        }
        _state = State::READY;
        _listDirty = true;
        // Hide the progress bar
        if (_loadBar) lv_obj_add_flag(_loadBar, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(_titleLabel, _deviceName.c_str());
        if (_folderLabel)  lv_obj_add_flag(_folderLabel, LV_OBJ_FLAG_HIDDEN);
        // _goodBadLabel stays visible, updateStatus() will refresh it
        // Save to session cache so next open skips loading
        g_elrsCache.valid       = true;
        g_elrsCache.deviceId    = _deviceId;
        g_elrsCache.fieldCount  = _fieldCount;
        g_elrsCache.fields      = _fields;
        g_elrsCache.devName     = _deviceName;
        g_elrsCache.devVersion  = _deviceVersion;
        _hasConnected = true;
      }
      break;

    case State::READY:
      // Request link stats (~1s)
      if (_deviceId == CRSF_MODULE_ADDR && now > _linkstatTimeout) {
        uint8_t v = 0;
        sendParamWrite(0, &v, 1);  // fieldId=0, value=0 = request link stats
        _linkstatTimeout = now + 100;  // 100 * 10ms = 1 second
      }
      break;

    default:
      break;
  }

  if (_listDirty) {
    _listDirty = false;
    rebuildList();
  }
}

// ---------------------------------------------------------------------------
// CRSF send helpers
// ---------------------------------------------------------------------------

bool ElrsParamBrowser::crsfPush(uint8_t cmd, const uint8_t* payload, uint8_t len)
{
  if (!outputTelemetryBuffer.isAvailable())
    return false;

  uint8_t modIdx = crsfModuleIdx();
  if (modIdx == 0xFF)
    return false;

  // Frame: MODULE_ADDRESS | length(2+len) | cmd | payload... | CRC
  outputTelemetryBuffer.pushByte(MODULE_ADDRESS);           // 0xEE
  outputTelemetryBuffer.pushByte(2 + len);                  // length
  outputTelemetryBuffer.pushByte(cmd);
  for (uint8_t i = 0; i < len; i++)
    outputTelemetryBuffer.pushByte(payload[i]);
  outputTelemetryBuffer.pushByte(
      crc8(outputTelemetryBuffer.data + 2, 1 + len));

  outputTelemetryBuffer.setDestination(crsfEndpoint(modIdx));
  return true;
}

void ElrsParamBrowser::sendPing()
{
  uint8_t p[] = {CRSF_BROADCAST, CRSF_RADIO_ADDR};
  crsfPush(CRSF_PING_ID, p, 2);
}

// ───────────────────────────────────────────────────────────────────────────
// Background cache loader — runs via lv_timer, no UI, no encoder polling
// ───────────────────────────────────────────────────────────────────────────

// CRSF protocol constants (same as ElrsParamBrowser private statics)
enum { BG_CRSF_BROADCAST = 0x00, BG_CRSF_RADIO_ADDR = 0xEF,
       BG_CRSF_PING_ID = 0x28, BG_CRSF_PARAM_READ_ID = 0x2C,
       BG_CRSF_PARAM_WRITE_ID = 0x2D, BG_CRSF_DEVINFO_ID = 0x29,
       BG_CRSF_PARAM_INFO_ID = 0x2B };

static uint8_t bgFindCrsfModule()
{
  for (uint8_t m = 0; m < NUM_MODULES; m++)
    if (moduleState[m].protocol == PROTOCOL_CHANNELS_CROSSFIRE)
      return m;
  return 0xFF;
}

static uint8_t bgCrsfEndpoint(uint8_t modIdx)
{
  return (modIdx == INTERNAL_MODULE) ? 0 : TELEMETRY_ENDPOINT_SPORT;
}

struct ElrsBgLoader {
  TelemetryQueue rxQueue;
  lv_timer_t*    timer = nullptr;
  enum { S_IDLE, S_PING, S_LOAD } state = S_IDLE;
  tmr10ms_t      timeout = 0;
  uint8_t        fieldCnt = 0, nextFid = 0, loaded = 0, chunk = 0;
  std::vector<uint8_t> chunkBuf;

  void start();
  void tick();
  void finish();
};

static ElrsBgLoader* g_bgLoader = nullptr;

static void elrsBgLoaderTick(lv_timer_t* t)
{
  auto* L = (ElrsBgLoader*)t->user_data;
  if (L) L->tick();
}

void ElrsBgLoader::start()
{
  if (g_elrsCache.valid || g_bgLoader) return;
  uint8_t modIdx = bgFindCrsfModule();
  if (modIdx == 0xFF) return;

  registerTelemetryQueue(&rxQueue);
  g_bgLoader = this;
  state   = S_PING;
  timeout = get_tmr10ms() + 200;
  timer   = lv_timer_create(elrsBgLoaderTick, 30, this);

  // Send PING — force buffer reset to ensure write succeeds during early boot
  outputTelemetryBuffer.reset();
  outputTelemetryBuffer.pushByte(MODULE_ADDRESS);
  uint8_t p[] = {BG_CRSF_BROADCAST, BG_CRSF_RADIO_ADDR};
  outputTelemetryBuffer.pushByte(2 + 2);
  outputTelemetryBuffer.pushByte(BG_CRSF_PING_ID);
  outputTelemetryBuffer.pushByte(p[0]);
  outputTelemetryBuffer.pushByte(p[1]);
  outputTelemetryBuffer.pushByte(crc8(outputTelemetryBuffer.data + 2, 3));
  outputTelemetryBuffer.setDestination(bgCrsfEndpoint(modIdx));
}

void ElrsBgLoader::finish()
{
  if (timer) { lv_timer_del(timer); timer = nullptr; }
  deregisterTelemetryQueue(&rxQueue);
  g_bgLoader = nullptr;
  delete this;
}

void ElrsBgLoader::tick()
{
  if (g_elrsCache.valid) { finish(); return; }

  tmr10ms_t now = get_tmr10ms();

  // Process incoming CRSF frames
  uint8_t length = 0, cmd = 0;
  while (rxQueue.probe(length) && rxQueue.size() >= (uint32_t)length) {
    rxQueue.pop(length);
    rxQueue.pop(cmd);
    uint8_t plen = length - 2;
    std::vector<uint8_t> p(plen);
    for (uint8_t i = 0; i < plen; i++) rxQueue.pop(p[i]);

    if (cmd == BG_CRSF_DEVINFO_ID && state == S_PING) {
      // DEVICE_INFO
      if (plen < 4) continue;
      uint8_t devId = p[1];
      std::string devName;
      size_t off = 2;
      while (off < plen && p[off] != 0) devName += (char)p[off++];
      off++;
      if (off + 12 > plen) continue;
      uint8_t ma = p[off+8], mi = p[off+9], pa = p[off+10];
      char ver[20] = "";
      if (ma||mi||pa) snprintf(ver, sizeof(ver), "v%u.%u.%u", ma, mi, pa);
      uint8_t fc = p[off+12];
      if (fc == 0) continue;

      g_elrsCache.fields.clear();
      g_elrsCache.fields.resize(fc);
      for (uint8_t i = 0; i < fc; i++) g_elrsCache.fields[i].id = i+1;
      g_elrsCache.deviceId = devId;
      g_elrsCache.fieldCount = fc;
      g_elrsCache.devName = devName;
      g_elrsCache.devVersion = ver;

      fieldCnt = fc; nextFid = 1; loaded = 0; chunk = 0;
      chunkBuf.clear();
      state = S_LOAD; timeout = now + 50;

    } else if (cmd == BG_CRSF_PARAM_INFO_ID && state == S_LOAD) {
      // PARAM_INFO
      if (plen < 5) continue;
      uint8_t devId = p[1], fid = p[2], chLeft = p[3];
      if (devId != g_elrsCache.deviceId || fid != nextFid) continue;
      for (uint8_t i = 4; i < plen; i++) chunkBuf.push_back(p[i]);
      if (chLeft > 0) { chunk++; timeout = now + 50; continue; }

      // Parse field (minimal — enough for name, type, value, options)
      if (fid >= 1 && fid <= (uint8_t)g_elrsCache.fields.size()) {
        auto& f = g_elrsCache.fields[fid-1];
        f.loaded = true;
        const uint8_t* d = chunkBuf.data();
        uint16_t dLen = (uint16_t)chunkBuf.size();
        size_t pos = 0;
        if (pos+3 > dLen) goto next_f;
        f.parent = d[pos++];
        f.type = (ElrsParamBrowser::FieldType)(d[pos] & 0x7F);
        f.hidden = (d[pos++] & 0x80) != 0;
        { std::string nm; while (pos<dLen && d[pos]) nm+=(char)d[pos++]; pos++; f.name=nm; }
        if (f.type == ElrsParamBrowser::FT_SELECT) {
          // SELECT layout: options(semicolon+NUL) | value(1) | min(1) | max(1) | default(1) | unit
          // Push ALL entries including blanks to preserve ELRS module indices
          // (matches Lua fieldGetStrOrOpts which does r[#r+1]=opt unconditionally)
          if (pos > dLen) goto next_f;
          f.options.clear();
          std::string cur;
          while (pos < dLen) {
            uint8_t b = d[pos++];
            if (b==0) { f.options.push_back(cur); break; }
            if (b==';') { f.options.push_back(cur); cur.clear(); }
            else cur+=(char)b;
          }
          int realCnt = 0;
          for (const auto& o : f.options) { if (!o.empty()) realCnt++; }
          f.grey = (realCnt <= 1);
          if (pos < dLen) f.value = d[pos++];
          pos += 3;  // skip min(1) + max(1) + default(1)
        } else if (f.type == ElrsParamBrowser::FT_UINT8 || f.type == ElrsParamBrowser::FT_INT8) {
          if (pos + 4 > dLen) goto next_f;
          f.byteSize = 1;
          f.value = d[pos]; pos++;         // value (1 byte)
          pos++;  // min (1 byte)
          pos++;  // max (1 byte)
          pos++;  // default (1 byte)
        } else if (f.type == ElrsParamBrowser::FT_UINT16 || f.type == ElrsParamBrowser::FT_INT16) {
          if (pos + 8 > dLen) goto next_f;
          f.byteSize = 2;
          f.value = ((uint16_t)d[pos]<<8)|d[pos+1]; pos+=2;  // value (2 bytes BE)
          pos+=2;  // min (2 bytes)
          pos+=2;  // max (2 bytes)
          pos+=2;  // default (2 bytes)
        } else if (f.type == ElrsParamBrowser::FT_FLOAT) {
          if (pos + 21 > dLen) goto next_f;
          f.byteSize = 4;
          f.value = ((int32_t)d[pos]<<24)|((int32_t)d[pos+1]<<16)|((int32_t)d[pos+2]<<8)|d[pos+3]; pos+=4;
          pos+=4;  // min (4 bytes)
          pos+=4;  // max (4 bytes)
          pos+=4;  // default (4 bytes)
          // prec(1) + step(4): skip
          pos += 5;
        } else if (f.type == ElrsParamBrowser::FT_INFO || f.type == ElrsParamBrowser::FT_STRING) {
          // Parse string value: null-terminated bytes after the field name
          if (pos < dLen) {
            std::string val;
            while (pos < dLen && d[pos] != 0) val += (char)d[pos++];
            f.strValue = val;
          }
        }
        next_f:
        chunkBuf.clear(); chunk = 0; loaded++;
        if (loaded >= fieldCnt) {
          g_elrsCache.valid = true;
          finish(); return;
        }
      }
      nextFid++; timeout = now + 50;
    }
  }

  // Timeout — advance state machine
  if (now > timeout) {
    if (state == S_PING) {
      timeout = now + 200;
      uint8_t modIdx = bgFindCrsfModule();
      outputTelemetryBuffer.reset();
      outputTelemetryBuffer.pushByte(MODULE_ADDRESS);
      uint8_t p[] = {BG_CRSF_BROADCAST, BG_CRSF_RADIO_ADDR};
      outputTelemetryBuffer.pushByte(2+2);
      outputTelemetryBuffer.pushByte(BG_CRSF_PING_ID);
      outputTelemetryBuffer.pushByte(p[0]); outputTelemetryBuffer.pushByte(p[1]);
      outputTelemetryBuffer.pushByte(crc8(outputTelemetryBuffer.data+2,3));
      outputTelemetryBuffer.setDestination(bgCrsfEndpoint(modIdx));
    } else if (state == S_LOAD && nextFid <= fieldCnt) {
      timeout = now + 50;
      uint8_t modIdx = bgFindCrsfModule();
      outputTelemetryBuffer.reset();
      outputTelemetryBuffer.pushByte(MODULE_ADDRESS);
      uint8_t p[] = {g_elrsCache.deviceId, BG_CRSF_RADIO_ADDR, nextFid, chunk};
      outputTelemetryBuffer.pushByte(2+4);
      outputTelemetryBuffer.pushByte(BG_CRSF_PARAM_READ_ID);
      for (uint8_t i=0;i<4;i++) outputTelemetryBuffer.pushByte(p[i]);
      outputTelemetryBuffer.pushByte(crc8(outputTelemetryBuffer.data+2,5));
      outputTelemetryBuffer.setDestination(bgCrsfEndpoint(modIdx));
    }
  }
}

void ElrsParamBrowser::triggerCacheLoad()
{
  if (g_elrsCache.valid || g_bgLoader) return;

  // Throttle to at most one attempt per second so we don't leak
  // ElrsBgLoader objects when the CRSF module isn't ready yet.
  static tmr10ms_t lastAttempt = 0;
  tmr10ms_t now = get_tmr10ms();
  if (now - lastAttempt < 100) return;
  lastAttempt = now;

  (new ElrsBgLoader())->start();
}

void ElrsParamBrowser::sendParamRead(uint8_t fieldId, uint8_t chunk)
{
  uint8_t p[] = {_deviceId, _handsetId, fieldId, chunk};
  crsfPush(CRSF_PARAM_READ_ID, p, 4);
}

void ElrsParamBrowser::sendParamWrite(uint8_t fieldId, const uint8_t* data, uint8_t len)
{
  // payload: deviceId, handsetId, fieldId, data...
  std::vector<uint8_t> p;
  p.push_back(_deviceId);
  p.push_back(_handsetId);
  p.push_back(fieldId);
  for (uint8_t i = 0; i < len; i++)
    p.push_back(data[i]);
  crsfPush(CRSF_PARAM_WRITE_ID, p.data(), (uint8_t)p.size());
}

// ---------------------------------------------------------------------------
// Telemetry receive processing
// ---------------------------------------------------------------------------

void ElrsParamBrowser::processRxQueue()
{
  uint8_t length = 0, cmd = 0;
  while (_rxQueue.probe(length) && _rxQueue.size() >= (uint32_t)length) {
    _rxQueue.pop(length);
    _rxQueue.pop(cmd);

    // Collect payload (length includes cmd byte, excludes CRC)
    uint8_t payloadLen = (uint8_t)(length - 2);  // length - cmd - implicit CRC
    std::vector<uint8_t> payload(payloadLen);
    for (uint8_t i = 0; i < payloadLen; i++)
      _rxQueue.pop(payload[i]);

    switch (cmd) {
      case CRSF_DEVINFO_ID:
        parseDeviceInfo(payload.data(), payloadLen);
        break;
      case 0x2B:  // CRSF_PARAM_INFO response
        parseParamInfo(payload.data(), payloadLen);
        break;
      case CRSF_ELRS_STATUS_ID:
        parseElrsStatus(payload.data(), payloadLen);
        break;
      default:
        break;
    }
  }
}

// ---------------------------------------------------------------------------
// Parse helpers
// ---------------------------------------------------------------------------

size_t ElrsParamBrowser::readString(const uint8_t* d, size_t off, std::string& out, size_t maxOff)
{
  out.clear();
  while (off < maxOff && d[off] != 0) {
    out += (char)d[off];
    off++;
  }
  return off + 1;  // skip null terminator
}

int32_t ElrsParamBrowser::readInt(const uint8_t* d, size_t off, uint8_t bytes, bool isSigned)
{
  uint32_t v = 0;
  for (uint8_t i = 0; i < bytes; i++)
    v = (v << 8) | d[off + i];

  if (isSigned && bytes == 1 && (v & 0x80))
    return (int8_t)v;
  if (isSigned && bytes == 2 && (v & 0x8000))
    return (int16_t)v;
  return (int32_t)v;
}

size_t ElrsParamBrowser::readOpts(const uint8_t* d, size_t off,
                                  std::vector<std::string>& opts, bool& grey,
                                  size_t maxOff)
{
  opts.clear();  // always replace, never accumulate
  // Keep option strings exactly as the ELRS firmware sends them so that
  // per-option annotations like "(152bps)" or "(1:128)" are preserved.
  // Push ALL entries including blanks — matches Lua fieldGetStrOrOpts
  // which does r[#r+1]=opt unconditionally, preserving ELRS module indices.
  std::string cur;
  while (off < maxOff) {
    uint8_t b = d[off++];
    if (b == 0) {
      opts.push_back(cur);
      break;
    }
    if (b == ';') {
      opts.push_back(cur);
      cur.clear();
    } else {
      cur += (char)b;
    }
  }
  // Count non-blank entries for grey check (single real option = disabled)
  int realCnt = 0;
  for (const auto& o : opts) { if (!o.empty()) realCnt++; }
  grey = (realCnt <= 1);
  return off;
}

// ---------------------------------------------------------------------------
// Parse DEVICE_INFO (0x29)
// payload: dest(1) | deviceId(1) | name(N+1) | serial(4) | hw(4) | fw(4) | fldcnt(1) ...
// ---------------------------------------------------------------------------

void ElrsParamBrowser::parseDeviceInfo(const uint8_t* p, uint8_t len)
{
  if (len < 4) return;

  uint8_t devId = p[1];  // p[0]=dest, p[1]=src=device id

  // name at p[2]
  std::string devName;
  size_t off = readString(p, 2, devName, len);

  if (off + 12 >= len) return;

  // Serial number: 4 bytes — "ELRS" (0x45 0x4C 0x52 0x53) means ExpressLRS device
  bool isElrs = (off + 4 <= len) &&
                (p[off] == 'E' && p[off+1] == 'L' && p[off+2] == 'R' && p[off+3] == 'S');

  // sw_version: 4 bytes big-endian at off+8 (CRSF standard); ELRS encodes as major*1000000+minor*1000+patch
  std::string fwVer;
  {
    // ELRS encodes softwareVer as 4 bytes: [major, minor, patch, suffixFlag]
    // (see ExpressLRS crsf_protocol.h: softwareVer = (major<<24)|(minor<<16)|(patch<<8)|flag)
    uint8_t major = p[off + 8];
    uint8_t minor = p[off + 9];
    uint8_t patch = p[off + 10];
    if (major || minor || patch) {
      char vbuf[20];
      snprintf(vbuf, sizeof(vbuf), "v%u.%u.%u",
               (unsigned)major, (unsigned)minor, (unsigned)patch);
      fwVer = vbuf;
    }
  }

  // skip serial(4)+hw(4)+fw(4)=12 bytes → fldcnt
  uint8_t fldcnt = p[off + 12];

  // Update or add to the device list
  bool found = false;
  for (auto& d : _devices) {
    if (d.id == devId) {
      d.name       = devName;
      d.fieldCount = fldcnt;
      d.isElrs     = isElrs;
      d.fwVersion  = fwVer;
      found = true;
      break;
    }
  }
  if (!found) {
    _devices.push_back({devId, devName, fldcnt, isElrs, fwVer});
  }

  // Non-current device: refresh device list only
  if (devId != _deviceId) {
    rebuildDeviceVirtualFields();
    _listDirty = true;
    return;
  }

  // Ignore repeated DEVINFO while loading/ready
  if ((_state == State::LOADING || _state == State::READY) && fldcnt == _fieldCount) {
    _deviceVersion = fwVer;
    rebuildDeviceVirtualFields();
    return;
  }

  _fieldCount = fldcnt;

  _fields.clear();
  _fields.resize(fldcnt);
  for (uint8_t i = 0; i < fldcnt; i++) {
    _fields[i].id = i + 1;  // 1-based
  }

  // Load queue (stack: field 1 first)
  _loadQueue.clear();
  for (int i = fldcnt; i >= 1; i--)
    _loadQueue.push_back((uint8_t)i);

  _fieldChunk   = 0;
  _chunkBuf.clear();
  _state        = State::LOADING;
  _fieldTimeout = get_tmr10ms();   // load immediately

  // Update title — store device name+version but show loading progress in titleLabel
  _deviceName    = devName;
  _deviceVersion = fwVer;
  if (!_hasConnected) {
    char status[48];
    snprintf(status, sizeof(status), STR_ELRS_LOADING, 0, (unsigned)fldcnt);
    lv_label_set_text(_titleLabel, status);
  }

  // Reset and show the inline progress bar
  if (_loadBar) {
    lv_obj_set_width(_loadBar, 0);
    lv_obj_clear_flag(_loadBar, LV_OBJ_FLAG_HIDDEN);
  }

  // Inject virtual "Other Devices" folder for any other known devices
  rebuildDeviceVirtualFields();
}

// ---------------------------------------------------------------------------
// Parse PARAM_INFO (0x2B)
// payload: dest(1) | src(1) | fieldId(1) | chunksRemain(1) | data...
// ---------------------------------------------------------------------------

void ElrsParamBrowser::parseParamInfo(const uint8_t* p, uint8_t len)
{
  if (len < 5) return;

  uint8_t devId      = p[1];
  uint8_t fieldId    = p[2];
  uint8_t chunksLeft = p[3];

  if (devId != _deviceId) return;

  // Check this is what we requested
  if (_loadQueue.empty() || _loadQueue.back() != fieldId) return;

  const uint8_t* data = p + 4;
  uint8_t dataLen = len - 4;  // single-chunk payload length (fits in uint8_t)

  if (chunksLeft > 0 || _fieldChunk > 0) {
    // Chunked: accumulate
    for (uint8_t i = 0; i < dataLen; i++)
      _chunkBuf.push_back(data[i]);

    if (chunksLeft > 0) {
      _fieldChunk++;
      // Request next chunk immediately
      sendParamRead(fieldId, _fieldChunk);
      _fieldTimeout = get_tmr10ms() + 50;
      return;
    }
    // Last chunk: finalise with accumulated buffer.
    // Use uint16_t to avoid truncation when total > 255 bytes (e.g. many FT_SELECT options).
    uint16_t totalLen = (uint16_t)_chunkBuf.size();
    data = _chunkBuf.data();

    // Find the field slot (1-based ids)
    if (fieldId < 1 || fieldId > (uint8_t)_fields.size()) return;
    Field& f = _fields[fieldId - 1];
    parseParamData(f, data, totalLen);
  } else {
    // Find the field slot (1-based ids)
    if (fieldId < 1 || fieldId > (uint8_t)_fields.size()) return;
    Field& f = _fields[fieldId - 1];
    parseParamData(f, data, dataLen);
  }

  // Done with this field
  _loadQueue.pop_back();
  _fieldChunk = 0;
  _chunkBuf.clear();

  // Find again for post-parse logic (was found inside the if/else above)
  if (fieldId < 1 || fieldId > (uint8_t)_fields.size()) {
    _loadQueue.pop_back();
    _fieldChunk = 0;
    _chunkBuf.clear();
    _fieldTimeout = get_tmr10ms();
    return;
  }
  Field& f = _fields[fieldId - 1];

  // Keep session cache in sync when a single field is refreshed after write
  if (g_elrsCache.valid && fieldId >= 1 && fieldId <= (uint8_t)g_elrsCache.fields.size()) {
    g_elrsCache.fields[fieldId - 1] = f;
  }

  // If this was the command field we're waiting on, run the full Lua-equivalent
  // state machine (see fieldCommandLoad / runPopupPage in elrs.lua).
  if (_cmdDialog && fieldId == _cmdFieldId && f.type == FT_COMMAND) {
    uint8_t prevStatus = _cmdLastStatus;

    if (f.cmdStatus == 0 && prevStatus != 0) {
      // Command finished (status transitioned to idle/done).
      // Lua: popupCompat shows "Stopped!" then calls reloadAllField().
      // Enforce minimum display time so the dialog is visible to the user.
      if (get_tmr10ms() < _cmdMinCloseTime) {
        // Too soon — keep _cmdLastStatus unchanged so the condition still
        // fires on the next poll, re-add to loadQueue and wait.
        _loadQueue.push_back(_cmdFieldId);
        _fieldTimeout = _cmdMinCloseTime;
        _listDirty    = true;
        return;
      }
      _cmdLastStatus = 0;
      _cmdDialog->setCloseHandler(nullptr);
      _cmdDialog->deleteLater();
      _cmdDialog     = nullptr;
      _cmdFieldId    = 0;
      // Reload all fields — matches Lua reloadAllField() after command stops
      reloadAllFields();

    } else {
      _cmdLastStatus = f.cmdStatus;

      if (f.cmdStatus == 3) {
        // Confirmation required: replace running dialog with ConfirmDialog.
        _cmdDialog->setCloseHandler(nullptr);
        _cmdDialog->deleteLater();
        _cmdDialog = nullptr;
        uint8_t  cid     = _cmdFieldId;
        uint8_t  timeout = (f.cmdTimeout > 0) ? f.cmdTimeout : 25;
        std::string fname = elrsTranslateName(f.name);
        std::string finfo = f.cmdInfo.empty() ? STR_ELRS_PRESS_OK_CONFIRM : f.cmdInfo;
        auto* confirmDlg = new ConfirmDialog(
            fname.c_str(), finfo.c_str(),
            // OK: send lcsConfirmed=4, re-open progress dialog
            [this, cid, timeout, fname]() {
              uint8_t st = 4;
              sendParamWrite(cid, &st, 1);
              _cmdFieldId    = cid;
              _cmdLastStatus = 3;  // mark that we came from confirm
              // Re-open a running dialog for the post-confirm phase
              _cmdDialog = new DynamicMessageDialog(
                  fname.c_str(),
                  [this]() -> std::string {
                    if (_cmdFieldId >= 1 && _cmdFieldId <= (uint8_t)_fields.size()) {
                      const Field& cf = _fields[_cmdFieldId - 1];
                      std::string info = cf.cmdInfo;
                      const char spin[] = "|/-\\";
                      char sp = spin[(get_tmr10ms() / 25) % 4];
                      if (info.empty()) info = STR_ELRS_EXECUTING;
                      info += " ["; info += sp; info += "]";
                      return info;
                    }
                    return STR_ELRS_EXECUTING;
                  },
                  STR_ELRS_PRESS_RTN_CANCEL,
                  EdgeTxStyles::STD_FONT_HEIGHT,
                  COLOR_THEME_PRIMARY1_INDEX, CENTERED);
              _cmdDialog->setCloseHandler([this]() {
                // RTN pressed on running dialog — send lcsCancel to device
                if (_cmdFieldId >= 1) {
                  uint8_t cancel = 5;
                  sendParamWrite(_cmdFieldId, &cancel, 1);
                  // Immediately clear local field state so list shows blank
                  Field& cf = _fields[_cmdFieldId - 1];
                  cf.cmdStatus = 0xFF;
                  cf.cmdInfo.clear();
                }
                // Stop polling so device response can't overwrite the cleared state
                _loadQueue.clear();
                _state         = State::READY;
                _cmdDialog     = nullptr;
                _cmdFieldId    = 0;
                _cmdLastStatus = 0;
                _listDirty     = true;
              });
              _loadQueue.push_back(cid);
              _state        = State::LOADING;
              _fieldTimeout = get_tmr10ms() + timeout;
              elrsDarkDialogStyle(_cmdDialog->getLvObj());
            },
            // Cancel: send lcsCancel and clear command state
            [this]() {
              if (_cmdFieldId >= 1) {
                uint8_t cancel = 5;
                sendParamWrite(_cmdFieldId, &cancel, 1);
                // Immediately clear local field state so list shows blank
                Field& cf = _fields[_cmdFieldId - 1];
                cf.cmdStatus = 0xFF;
                cf.cmdInfo.clear();
              }
              // Stop polling so device response can't overwrite the cleared state
              _loadQueue.clear();
              _state         = State::READY;
              _cmdFieldId    = 0;
              _cmdLastStatus = 0;
            });
        elrsDarkDialogStyle(confirmDlg->getLvObj());

      } else if (f.cmdStatus == 0 || f.cmdStatus == 0xFF) {
        // Status already idle when first polled (device ignored or instantly completed).
        // Lua: fieldCommandLoad sets fieldPopup=nil silently.
        // Here we apply the same minimum display time before closing.
        if (get_tmr10ms() < _cmdMinCloseTime) {
          _loadQueue.push_back(_cmdFieldId);
          _fieldTimeout = _cmdMinCloseTime;
          _listDirty    = true;
          return;
        }
        _cmdDialog->setCloseHandler(nullptr);
        _cmdDialog->deleteLater();
        _cmdDialog  = nullptr;
        _cmdFieldId = 0;
      }
      // status 1 or 2: still running — keep polling (leave in LOADING state)
    }
    _listDirty = true;
  }

  // Update loading progress in titleLabel (replaces device name during loading)
  // Use signed arithmetic to avoid underflow when command polling adds extra queue items.
  int loaded = (int)_fieldCount - (int)_loadQueue.size();
  if (loaded < 0) loaded = 0;
  if (loaded > (int)_fieldCount) loaded = (int)_fieldCount;
  if (!_hasConnected) {
    char status[48];
    snprintf(status, sizeof(status), STR_ELRS_LOADING, (unsigned)loaded,
             (unsigned)_fieldCount);
    lv_label_set_text(_titleLabel, status);
  }

  // Update inline progress bar width
  if (_loadBar && _fieldCount > 0) {
    lv_obj_set_width(_loadBar, (coord_t)(LCD_W * loaded / (int)_fieldCount));
    lv_obj_clear_flag(_loadBar, LV_OBJ_FLAG_HIDDEN);
  }

  // Schedule next load
  _fieldTimeout = get_tmr10ms();  // immediate
}

// ---------------------------------------------------------------------------
// Parse a single field's data blob
// layout: parent(1) | type(1) | name(N+1) | <type-specific>
// ---------------------------------------------------------------------------

void ElrsParamBrowser::parseParamData(Field& f, const uint8_t* d, uint16_t len)
{
  if (len < 3) return;

  size_t off = 0;
  f.parent = d[off++];
  uint8_t typeByte = d[off++];
  f.type   = (FieldType)(typeByte & 0x7F);
  // ELRS encodes the hidden bit in the high bit of the type byte (0x80).
  // We ignore it so version/info fields always display, even without RX link.
  off = readString(d, off, f.name);

  f.loaded = true;

  switch (f.type) {
    // ---- Numeric: UINT8, INT8, UINT16, INT16 ----
    case FT_UINT8:
    case FT_INT8: {
      uint8_t sz   = 1;
      bool    sign = (f.type == FT_INT8);
      f.byteSize   = sz;
      f.isSigned   = sign;
      if (off + 4 * sz > len) break;
      f.value  = readInt(d, off + 0 * sz, sz, sign);
      f.minVal = readInt(d, off + 1 * sz, sz, sign);
      f.maxVal = readInt(d, off + 2 * sz, sz, sign);
      f.step   = 1;
      off += 4 * sz;
      if (off < len) readString(d, off, f.unit, len);
      break;
    }
    case FT_UINT16:
    case FT_INT16: {
      uint8_t sz   = 2;
      bool    sign = (f.type == FT_INT16);
      f.byteSize   = sz;
      f.isSigned   = sign;
      if (off + 4 * sz > len) break;
      // Big-endian for 16-bit
      f.value  = readInt(d, off + 0 * sz, sz, sign);
      f.minVal = readInt(d, off + 1 * sz, sz, sign);
      f.maxVal = readInt(d, off + 2 * sz, sz, sign);
      f.step   = 1;
      off += 4 * sz;
      if (off < len) readString(d, off, f.unit, len);
      break;
    }

    // ---- Float ----
    // Lua fieldFloatLoad layout: value(4)+min(4)+max(4)+default(4)+prec(1)+step(4)+unit_str
    // unitoffset=21, prec at off+16, step at off+17
    case FT_FLOAT: {
      if (off + 21 > (size_t)len) break;
      f.byteSize     = 4;
      f.isSigned     = true;
      f.value        = readInt(d, off,      4, true);
      f.minVal       = readInt(d, off + 4,  4, true);
      f.maxVal       = readInt(d, off + 8,  4, true);
      // off+12: default (4 bytes, not used)
      f.decimalPoint = d[off + 16];
      if (f.decimalPoint > 3) f.decimalPoint = 3;  // matches Lua: if field.prec > 3 then field.prec = 3
      f.step         = readInt(d, off + 17, 4, false);  // step as unsigned
      if (f.step <= 0) f.step = 1;
      off += 21;  // past value+min+max+default+prec+step
      if (off < len) readString(d, off, f.unit, len);
      break;
    }

    // ---- Text Select ----
    case FT_SELECT: {
      bool grey = false;
      off = readOpts(d, off, f.options, grey, len);  // bounds-guarded
      f.grey  = grey;
      if (off < len) f.value = d[off++];
      off += 3;  // skip min(1) + max(1) + default(1) — Lua: field.unit = data[offset+4]
      // unit string follows
      if (off < len) off = readString(d, off, f.unit);
      break;
    }

    // ---- String / Info ----
    case FT_STRING:
    case FT_INFO: {
      if (off < len) readString(d, off, f.strValue, len);
      break;
    }

    // ---- Folder: no extra data ----
    case FT_FOLDER:
      break;

    // ---- Command ----
    case FT_COMMAND: {
      if (off + 2 > len) break;
      f.cmdStatus  = d[off++];
      f.cmdTimeout = d[off++];
      if (off < len) readString(d, off, f.cmdInfo, len);
      break;
    }

    // ---- Back / Device ----
    case FT_BACK:
    case FT_DEVICE:
    default:
      break;
  }
}

// ---------------------------------------------------------------------------
// Parse ELRS_STATUS (0x2E)
// payload layout (0-indexed, matches Lua 1-indexed data[]):
//   p[0]=dest  p[1]=src  p[2]=badPkt  p[3]=goodHi  p[4]=goodLo  p[5]=flags  p[6..]=info
// Lua: badPkt=data[3], goodPkt=data[4]*256+data[5], flags=data[6], info=data[7..]
// ---------------------------------------------------------------------------

void ElrsParamBrowser::parseElrsStatus(const uint8_t* p, uint8_t len)
{
  if (len < 7) return;
  _badPkt    = p[2];
  _goodPkt   = ((uint16_t)p[3] << 8) | p[4];
  _elrsFlags = p[5];
  readString(p, 6, _elrsFlagsInfo, len);
  updateStatus();
}

// ---------------------------------------------------------------------------
// Rebuild virtual fields: version row + Other Devices folder
// ------
void ElrsParamBrowser::rebuildDeviceVirtualFields()
{
  // Remove all virtual fields (those beyond _fieldCount)
  if (_fields.size() > _fieldCount)
    _fields.resize(_fieldCount);

  // Current device version entry — shown at root level (like Lua "BIND" row)
  if (!_deviceVersion.empty()) {
    Field f;
    f.id     = 0xFD;
    f.parent = 0;  // root level
    f.type   = FT_DEVICE;
    f.hidden = false;
    f.loaded = true;
    f.name   = "BIND";
    f.strValue = _deviceVersion;
    _fields.push_back(std::move(f));
  }

  // Non-current device entries — shown at root level with version
  // (These are discovered CRSF devices like the RX; the backpack is NOT
  //  a CRSF device — its version comes from an FT_INFO parameter inside
  //  the "Backpack" folder of the TX module.)
  for (const auto& dev : _devices) {
    if (dev.id == _deviceId) continue;
    Field f;
    f.id     = dev.id;
    f.parent = 0;
    f.type   = FT_INFO;
    f.hidden = false;
    f.loaded = true;
    f.name   = dev.name;
    f.strValue = dev.fwVersion.empty() ? "v?.?.?" : dev.fwVersion;
    _fields.push_back(std::move(f));
  }

  // Other Devices folder (only when >1 device)
  if (_devices.size() <= 1) return;

  // Other Devices folder entry
  {
    Field f;
    f.id     = (uint8_t)(_fieldCount + 1);
    f.parent = 0;  // root level
    f.type   = FT_FOLDER;
    f.hidden = false;
    f.loaded = true;
    f.name   = "Other Devices";
    _fields.push_back(std::move(f));
  }

  // Virtual device entries: one per non-current device, child of "Other Devices"
  for (const auto& dev : _devices) {
    if (dev.id == _deviceId) continue;  // current device stays at root level
    Field f;
    f.id     = dev.id;
    f.parent = (uint8_t)(_fieldCount + 1);
    f.type   = FT_DEVICE;
    f.hidden = false;
    f.loaded = true;
    f.name   = dev.name;
    f.strValue = dev.fwVersion;
    _fields.push_back(std::move(f));
  }
}

// ---------------------------------------------------------------------------
// Switch the active device — matches Lua changeDeviceId().
// Looks up the device in _devices, updates _deviceId / _handsetId, resets
// _fields to placeholder entries for the new device and starts loading.
// ---------------------------------------------------------------------------

void ElrsParamBrowser::changeDevice(uint8_t devId)
{
  DeviceEntry* dev = nullptr;
  for (auto& d : _devices) {
    if (d.id == devId) { dev = &d; break; }
  }
  if (!dev) return;

  // Update active device
  _deviceId = devId;
  // handsetId: 0xEF only for the ELRS TX module (devId == 0xEE with ELRS serial)
  // For all other devices (including ELRS RX) use 0xEA — matches Lua:
  //   deviceIsELRS_TX = device.isElrs and devId == 0xEE or nil
  //   handsetId = deviceIsELRS_TX and 0xEF or 0xEA
  bool isElrsTx = dev->isElrs && (devId == CRSF_MODULE_ADDR);
  _handsetId    = isElrsTx ? CRSF_RADIO_ADDR : CRSF_RADIO_ADDR_OTHER;

  // Invalidate session cache (it stores TX module params)
  g_elrsCache.valid = false;

  // Allocate field placeholders for new device
  _fieldCount = dev->fieldCount;
  _fields.clear();
  _fields.resize(_fieldCount);
  for (uint8_t i = 0; i < _fieldCount; i++)
    _fields[i].id = i + 1;

  // Build load queue
  _loadQueue.clear();
  for (int i = _fieldCount; i >= 1; i--)
    _loadQueue.push_back((uint8_t)i);

  _fieldChunk    = 0;
  _chunkBuf.clear();
  _currentFolder = -1;
  _selectedIdx   = 0;
  _editMode      = false;
  _folderName    = "";
  _state         = State::LOADING;
  _fieldTimeout  = get_tmr10ms();
  _listDirty     = true;

  _deviceName = dev->name;
  char status[48];
  snprintf(status, sizeof(status), STR_ELRS_LOADING, 0, (unsigned)_fieldCount);
  lv_label_set_text(_titleLabel, status);

  if (_folderLabel)  lv_label_set_text(_folderLabel, "");
  if (_loadBar) {
    lv_obj_set_width(_loadBar, 0);
    lv_obj_clear_flag(_loadBar, LV_OBJ_FLAG_HIDDEN);
  }

  // Re-inject virtual "Other Devices" entries now that _deviceId changed
  rebuildDeviceVirtualFields();
}



int ElrsParamBrowser::visibleCount() const
{
  int cnt = 0;
  for (const auto& f : _fields) {
    // Match Lua getField: only filter unloaded fields and parent mismatch
    if (!f.loaded) continue;
    // Hide virtual "Other Devices" entries until loading is complete
    if (_state != State::READY && f.id > _fieldCount) continue;
    if (f.parent == (uint8_t)(_currentFolder < 0 ? 0 : _currentFolder))
      cnt++;
  }
  return cnt;
}

ElrsParamBrowser::Field* ElrsParamBrowser::visibleField(int n)
{
  int cnt = 0;
  for (auto& f : _fields) {
    // Match Lua getField: only filter unloaded fields and parent mismatch
    if (!f.loaded) continue;
    // Hide virtual "Other Devices" entries until loading is complete
    if (_state != State::READY && f.id > _fieldCount) continue;
    if (f.parent == (uint8_t)(_currentFolder < 0 ? 0 : _currentFolder)) {
      if (cnt == n) return &f;
      cnt++;
    }
  }
  return nullptr;
}

void ElrsParamBrowser::clampSelection()
{
  int cnt   = visibleCount();
  int total = cnt + 3;  // +3: Default, Reload, Close title-bar buttons
  if (_selectedIdx >= total) _selectedIdx = total - 1;
  if (_selectedIdx < 0)      _selectedIdx = 0;
}

void ElrsParamBrowser::updateStatus()
{
  if (_state != State::READY) return;

  // Matches Lua lcd_title_color():
  //   if titleShowWarn: draw elrsFlagsInfo on left only (no folder, no goodBadPkt)
  //   else: draw deviceName left, currentFolderName center, goodBadPkt right
  const bool warn = (_elrsFlags > 3);

  if (warn) {
    lv_label_set_text(_titleLabel, _elrsFlagsInfo.c_str());
    if (_folderLabel)  lv_obj_add_flag(_folderLabel,  LV_OBJ_FLAG_HIDDEN);
    if (_goodBadLabel) lv_obj_add_flag(_goodBadLabel, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_label_set_text(_titleLabel, _deviceName.c_str());
    if (_folderLabel) {
      lv_label_set_text(_folderLabel, _folderName.c_str());
      lv_obj_add_flag(_folderLabel, LV_OBJ_FLAG_HIDDEN);
    }
    if (_goodBadLabel) {
      lv_obj_clear_flag(_goodBadLabel, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text_fmt(_goodBadLabel, "%u/%u", _badPkt, _goodPkt);
    }
  }
}

void ElrsParamBrowser::doReload()
{
  g_elrsCache.valid = false;
  _fields.clear();
  _devices.clear();     // fresh device discovery on next ping
  _fieldCount    = 0;
  _deviceId      = CRSF_MODULE_ADDR;   // reset to TX module as primary target
  _handsetId     = CRSF_RADIO_ADDR;    // reset to 0xEF (ELRS TX handset)
  _currentFolder = -1;
  _selectedIdx   = 0;
  _editMode      = false;
  _listDirty     = true;
  _state         = State::PINGING;
  _pingTimeout   = get_tmr10ms() + 200;
  _folderName = "";
  if (_folderLabel) lv_label_set_text(_folderLabel, "");
  _linkstatTimeout = 0;
  lv_label_set_text(_titleLabel, STR_ELRS_SEARCHING);
  // If neither module is configured for CRSF in the model, prompt the user
  // (if configured but powered off, proceed to ping so module has a chance to start)
  if (!isModuleCrossfire(INTERNAL_MODULE) && !isModuleCrossfire(EXTERNAL_MODULE)) {
    showModuleSetupMenu();
    return;
  }
  sendPing();
}

// ---------------------------------------------------------------------------
// Apply preferred defaults to ELRS device: 333Hz / FULL / 16CH
// Searches loaded fields by name and writes the closest matching option index.
// ---------------------------------------------------------------------------

void ElrsParamBrowser::applyDefaultSettings()
{
  if (_fields.empty() || _state != State::READY) return;

  // Log all SELECT fields to help diagnose name matching (visible in radio TRACE output)
  for (const Field& f : _fields) {
    if (f.type == FT_SELECT) {
      TRACE("[ELRS] SELECT field id=%u name='%s' value=%d opts=%u",
            f.id, f.name.c_str(), (int)f.value, (unsigned)f.options.size());
    }
  }

  // Map: exact field name sent by ELRS device firmware → substring to find in option.
  // Field names come from TXModuleParameters.cpp in ExpressLRS firmware.
  // Options for SX128X (2.4GHz): "Packet Rate" → "333Hz Full(-105dBm)" contains "333";
  //                               "Switch Mode" → "16ch Rate/2" contains "16ch".
  static const struct { const char* nameMatch; const char* optMatch; } kDefaults[] = {
    { "Packet Rate", "333"  },   // → "333Hz Full(-105dBm)"
    { "Switch Mode", "16ch" },   // → "16ch Rate/2"
  };

  bool anyWritten = false;
  for (const auto& def : kDefaults) {
    for (Field& f : _fields) {
      if (f.type != FT_SELECT) continue;
      // Contains check on field name
      if (f.name.find(def.nameMatch) == std::string::npos) continue;
      for (int i = 0; i < (int)f.options.size(); i++) {
        if (f.options[i].find(def.optMatch) != std::string::npos) {
          if (f.value != i) {
            TRACE("[ELRS] applyDefault: writing field '%s' id=%u → option[%d]='%s'",
                  f.name.c_str(), f.id, i, f.options[i].c_str());
            uint8_t idx = (uint8_t)i;
            sendParamWrite(f.id, &idx, 1);
            f.value   = i;
            anyWritten = true;
          }
          break;
        }
      }
    }
  }

  if (anyWritten) {
    // Reload all fields so display reflects device's confirmed values
    reloadAllFields();
  }
}

// ---------------------------------------------------------------------------
// Row click callback — carries row index as user_data
// ---------------------------------------------------------------------------

void ElrsParamBrowser::onRowTouch(int rowIdx)
{
  if (_state != State::READY) return;

  Field* f = visibleField(rowIdx);
  if (!f) return;

  // Folders, back button, device entries, and commands activate immediately on any tap.
  bool activateImmediately = (f->type == FT_FOLDER ||
                              f->type == FT_BACK   ||
                              f->type == FT_DEVICE ||
                              f->type == FT_COMMAND);

  bool isEditable = (!f->grey &&
                     (f->type == FT_UINT8  || f->type == FT_INT8  ||
                      f->type == FT_UINT16 || f->type == FT_INT16 ||
                      f->type == FT_FLOAT  || f->type == FT_SELECT));

  if (activateImmediately) {
    _selectedIdx = rowIdx;
    _editMode    = false;
    activateField(f);
  } else if (isEditable) {
    if (rowIdx != _selectedIdx) {
      // First tap: select the row
      _selectedIdx = rowIdx;
      _editMode    = false;
      _listDirty   = true;
    } else {
      // Second tap on already-selected editable field: open popup menu
      _editMode = false;
      openFieldPopup(f);
    }
  } else if (f->type == FT_INFO || f->type == FT_STRING) {
    // Version/info rows are read-only — select but don't activate
    _selectedIdx = rowIdx;
    _editMode    = false;
    _listDirty   = true;
  } else {
    _selectedIdx = rowIdx;
    _editMode    = false;
    _listDirty   = true;
  }
}

// Commit the current field value to the device, then reload related fields.
void ElrsParamBrowser::commitField(Field* f)
{
  if (!f) return;
  uint8_t valBytes[4] = {};
  uint8_t sz = f->byteSize;
  int32_t v  = f->value;
  for (uint8_t i = 0; i < sz; i++)
    valBytes[i] = (uint8_t)((v >> (8 * (sz - 1 - i))) & 0xFF);
  sendParamWrite(f->id, valBytes, sz);
  reloadRelatedFields(f);
  _editMode  = false;
  _listDirty = true;
}

// Open a popup Menu to edit the field value via touch.
// FT_SELECT  → list of option strings.
// Numeric    → list of values in range (max 60 shown, window around current if wider).
// Apply FPV dark style to a standard EdgeTX Menu popup so it matches the
// dark ELRS browser background. Traverses the LVGL object tree:
//   Menu(ModalWindow) → content panel → [header, body(table)]
static void applyDarkMenuStyle(Menu* menu)
{
  lv_obj_t* overlay = menu->getLvObj();
  // content panel is the first (and only) child of the modal overlay
  lv_obj_t* panel = lv_obj_get_child(overlay, 0);
  if (!panel) return;

  // Panel: dark bg, border outline
  lv_obj_set_style_bg_color(panel, lv_color_make(0x18, 0x18, 0x18), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_outline_width(panel, 1, LV_PART_MAIN);
  lv_obj_set_style_outline_color(panel, lv_color_make(0x40, 0x40, 0x40), LV_PART_MAIN);

  uint32_t childCnt = lv_obj_get_child_cnt(panel);
  for (uint32_t i = 0; i < childCnt; i++) {
    lv_obj_t* child = lv_obj_get_child(panel, i);
    if (i == 0) {
      // Header title bar
      lv_obj_set_style_bg_color(child, lv_color_make(0x22, 0x22, 0x22), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(child, LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_set_style_text_color(child, lv_color_make(0xFF, 0x8C, 0x00), LV_PART_MAIN);
    }
    // Body (i>=1) — TextButton styles are set in menu.cpp addLine(),
    // no LV_PART_ITEMS needed for plain Window body
  }
}

void ElrsParamBrowser::openFieldPopup(Field* f)
{
  if (!f || f->grey) return;

  auto menu = new Menu();
  applyDarkMenuStyle(menu);
  menu->setTitle(elrsTranslateName(f->name).c_str());

  if (f->type == FT_SELECT) {
    int32_t curVal = f->value;
    int selIdx = 0, menuIdx = 0;
    for (int i = 0; i < (int)f->options.size(); i++) {
      // Skip blank option entries — Lua skips options where #value == 0
      if (f->options[i].empty()) continue;
      // Use the option string exactly as the ELRS firmware sends it.
      // The firmware embeds per-option annotations (e.g. "1:128 (152bps)",
      // "Std (1:128)") so we must not strip or replace them.
      std::string label = f->options[i];
      int32_t idx = i;
      menu->addLineBuffered(
          label,
          [this, f, idx]() { f->value = idx; commitField(f); },
          [curVal, idx]() -> bool { return curVal == idx; });
      if (i == (int)curVal) selIdx = menuIdx;
      menuIdx++;
    }
    menu->updateLines();
    menu->select(selIdx);

  } else {
    // Numeric: determine value window
    int32_t step   = (f->step > 0) ? f->step : 1;
    int32_t total  = (f->maxVal - f->minVal) / step;
    int32_t startV = (total <= 60) ? f->minVal
                                   : std::max(f->minVal, f->value - 20 * step);
    int32_t endV   = (total <= 60) ? f->maxVal
                                   : std::min(f->maxVal, f->value + 20 * step);

    int32_t curVal = f->value;
    uint8_t dp     = f->decimalPoint;
    double  scale  = 1.0;
    for (uint8_t k = 0; k < dp; k++) scale *= 10.0;
    std::string unit = f->unit;
    bool isFloat = (f->type == FT_FLOAT);

    int selIdx = 0, idx = 0;
    for (int32_t v = startV; v <= endV; v += step, idx++) {
      char buf[32];
      if (isFloat)
        snprintf(buf, sizeof(buf), "%.*f", (int)dp, v / scale);
      else
        snprintf(buf, sizeof(buf), "%d", (int)v);
      std::string label = buf;
      if (!unit.empty()) { label += ' '; label += unit; }

      int32_t val = v;
      menu->addLineBuffered(
          label,
          [this, f, val]() { f->value = val; commitField(f); },
          [curVal, val]() -> bool { return curVal == val; });
      if (v == curVal) selIdx = idx;
    }
    menu->updateLines();
    menu->select(selIdx);
  }

  menu->setCloseHandler([this]() {
    _editMode  = false;
    _listDirty = true;
  });
}

// ---------------------------------------------------------------------------
// Build / rebuild the LVGL list
// ---------------------------------------------------------------------------

void ElrsParamBrowser::rebuildList()
{
  // Clamp selection first so all subsequent logic uses a valid index.
  clampSelection();

  lv_obj_clean(_list);
  // Free previous click-data allocations
  for (auto* d : _rowClickData) delete (ElrsRowData*)d;
  _rowClickData.clear();

  int cnt = visibleCount();
  for (int i = 0; i < cnt; i++) {
    Field* f = visibleField(i);
    if (!f) continue;

    bool selected = (i == _selectedIdx);

    // Clickable row
    lv_obj_t* row = lv_obj_create(_list);
    lv_obj_set_size(row, LCD_W, ROW_H);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_pad_all(row, 2, 0);
    // Yellow = editing, green = selected, black = normal
    lv_color_t rowBg;
    if (selected && _editMode)   rowBg = lv_color_make(0xCC, 0xAA, 0x00);
    else if (selected)           rowBg = lv_color_make(0x00, 0xA0, 0x00);
    else                         rowBg = lv_color_make(0x18, 0x18, 0x18);
    lv_obj_set_style_bg_color(row, rowBg, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    // Press-state: slightly brighter so finger position is confirmed on touch
    lv_color_t pressedBg = selected ? lv_color_make(0x00, 0xFF, 0x40) : lv_color_make(0x28, 0x28, 0x28);
    if (selected && _editMode) pressedBg = lv_color_make(0xFF, 0xEE, 0x00);
    lv_obj_set_style_bg_color(row, pressedBg, LV_STATE_PRESSED);
    // Enable touch but prevent row from stealing encoder focus
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    auto* cd = new ElrsRowData{this, i};
    _rowClickData.push_back(cd);
    lv_obj_add_event_cb(row, elrsBrowser_row_cb, LV_EVENT_CLICKED, cd);

    // Scroll this row into view if selected.
    // Force layout recalculation first so positions are accurate after
    // lv_obj_clean() rebuilt the list from scratch.
    if (selected) {
      lv_obj_update_layout(_list);
      lv_obj_scroll_to_view(row, LV_ANIM_OFF);
    }

    // Name label (left)
    lv_obj_t* nameLabel = lv_label_create(row);
    lv_obj_align(nameLabel, LV_ALIGN_LEFT_MID, 4, 0);
    lv_label_set_long_mode(nameLabel, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(nameLabel, LCD_W - COL2_W - 8);
    lv_label_set_text(nameLabel, elrsTranslateName(f->name).c_str());
    lv_obj_set_style_text_color(nameLabel,
        selected ? lv_color_black() : lv_color_white(), 0);

    // Value label (right)
    char valBuf[64] = "";
    switch (f->type) {
      case FT_UINT8: case FT_INT8: case FT_UINT16: case FT_INT16:
        if (f->unit.empty())
          snprintf(valBuf, sizeof(valBuf), "%d", (int)f->value);
        else
          snprintf(valBuf, sizeof(valBuf), "%d %s", (int)f->value, f->unit.c_str());
        break;
      case FT_FLOAT: {
        double scale = 1.0;
        for (uint8_t k = 0; k < f->decimalPoint; k++) scale *= 10.0;
        snprintf(valBuf, sizeof(valBuf), "%.*f %s",
                 (int)f->decimalPoint, f->value / scale, f->unit.c_str());
        break;
      }
      case FT_SELECT:
        if (f->value >= 0 && f->value < (int32_t)f->options.size()) {
          // Show the option string exactly as the firmware sends it.
          snprintf(valBuf, sizeof(valBuf), "%s", f->options[f->value].c_str());
        }
        break;
      case FT_INFO: case FT_STRING:
        if (!f->strValue.empty())
          snprintf(valBuf, sizeof(valBuf), "%s", f->strValue.c_str());
        else
          snprintf(valBuf, sizeof(valBuf), "---");
        break;
      case FT_FOLDER:
        snprintf(valBuf, sizeof(valBuf), LV_SYMBOL_RIGHT); break;
      case FT_DEVICE:
        // Show version for BIND row, and for backpack/other device entries
        if (!f->strValue.empty())
          snprintf(valBuf, sizeof(valBuf), "%s", f->strValue.c_str());
        else
          snprintf(valBuf, sizeof(valBuf), LV_SYMBOL_RIGHT);
        break;
      case FT_COMMAND:
        // Show live command status / info from device response
        if (!f->cmdInfo.empty())
          snprintf(valBuf, sizeof(valBuf), "%s", f->cmdInfo.c_str());
        else if (f->cmdStatus == 0 || f->cmdStatus == 0xFF)
          valBuf[0] = '\0';  // don't show [RUN] in list
        else if (f->cmdStatus == 1 || f->cmdStatus == 2)
          snprintf(valBuf, sizeof(valBuf), "[...]");  // status 1=pending, 2=executing
        else
          snprintf(valBuf, sizeof(valBuf), "[%u]", (unsigned)f->cmdStatus);
        break;
      case FT_BACK:
        snprintf(valBuf, sizeof(valBuf), STR_ELRS_BACK); break;
      default: break;
    }

    // Value label (right)
    if (valBuf[0]) {
      lv_obj_t* valLabel = lv_label_create(row);
      lv_obj_align(valLabel, LV_ALIGN_RIGHT_MID, -4, 0);
      lv_label_set_long_mode(valLabel, LV_LABEL_LONG_CLIP);
      lv_label_set_text(valLabel, valBuf);
      lv_obj_set_style_text_color(valLabel,
          selected ? lv_color_black() : lv_color_make(0xAA, 0xAA, 0xAA), 0);
      lv_obj_set_width(valLabel, COL2_W);
    }

    // Hide display-only rows at root level to match Lua title-bar behaviour
    if (_currentFolder < 0 && f->type == FT_DEVICE && f->name == "BIND") {
      lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_size(row, 0, 0);
      lv_obj_set_style_pad_all(row, 0, 0);
      lv_obj_set_style_border_width(row, 0, 0);
    }
    if (_currentFolder < 0 && f->name == "Bad/Good") {
      lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_size(row, 0, 0);
      lv_obj_set_style_pad_all(row, 0, 0);
      lv_obj_set_style_border_width(row, 0, 0);
    }
  }

  if (cnt == 0 && _state == State::READY) {
    lv_obj_t* empty = lv_label_create(_list);
    lv_label_set_text(empty, STR_EMPTY);
    lv_obj_set_style_text_color(empty, lv_color_make(0x88, 0x88, 0x88), 0);
  }

  // Highlight title-bar Default / Reload / Close buttons when encoder selects them
  // Use LV_STATE_FOCUSED to trigger the theme's built-in focus highlight
  if (_defaultBtnObj) {
    if (_selectedIdx == cnt)
      lv_obj_add_state(_defaultBtnObj, LV_STATE_FOCUSED);
    else
      lv_obj_clear_state(_defaultBtnObj, LV_STATE_FOCUSED);
  }
  if (_reloadBtnObj) {
    if (_selectedIdx == cnt + 1)
      lv_obj_add_state(_reloadBtnObj, LV_STATE_FOCUSED);
    else
      lv_obj_clear_state(_reloadBtnObj, LV_STATE_FOCUSED);
  }
  if (_closeBtnObj) {
    if (_selectedIdx == cnt + 2)
      lv_obj_add_state(_closeBtnObj, LV_STATE_FOCUSED);
    else
      lv_obj_clear_state(_closeBtnObj, LV_STATE_FOCUSED);
  }

  clampSelection();
  updateStatus();
}

// ---------------------------------------------------------------------------
// Navigation / editing
// ---------------------------------------------------------------------------

void ElrsParamBrowser::openFolder(Field* f)
{
  _currentFolder = (int16_t)f->id;
  _folderName    = elrsTranslateName(f->name);
  _editMode      = false;
  _listDirty     = true;

  // Skip BACK item at index 0 so user lands on first real setting
  Field* first = visibleField(0);
  if (first && first->type == FT_BACK && visibleCount() > 1)
    _selectedIdx = 1;
  else
    _selectedIdx = 0;

  // Update center folder label; _titleLabel (device name) stays unchanged
  if (_folderLabel) lv_label_set_text(_folderLabel, _folderName.c_str());
}

void ElrsParamBrowser::goBack()
{
  if (_currentFolder < 0) {
    // Already at root — close browser
    deleteLater();
    return;
  }

  // Find current folder field to get its parent
  int16_t parentFolder = -1;
  std::string parentName;  // empty = root level
  for (auto& f : _fields) {
    if (f.id == (uint8_t)_currentFolder) {
      parentFolder = (f.parent == 0) ? -1 : (int16_t)f.parent;
      if (parentFolder >= 0) {
        for (auto& pf : _fields) {
          if (pf.id == (uint8_t)parentFolder) {
            parentName = pf.name;
            break;
          }
        }
      }
      break;
    }
  }

  _currentFolder = parentFolder;
  _folderName    = parentName;  // "" at root, parent folder name otherwise
  _selectedIdx   = 0;
  _editMode      = false;
  _listDirty     = true;
  // Update center folder label; _titleLabel (device name) stays unchanged
  if (_folderLabel) lv_label_set_text(_folderLabel, _folderName.c_str());
}

// Rebuild the full load queue for the current device without re-pinging
// (equivalent to reloadAllField() in elrs.lua)
void ElrsParamBrowser::reloadAllFields()
{
  _loadQueue.clear();
  for (int i = _fieldCount; i >= 1; i--)
    _loadQueue.push_back((uint8_t)i);
  _fieldChunk = 0;
  _chunkBuf.clear();
  _state        = State::LOADING;
  _fieldTimeout = get_tmr10ms();
  _listDirty    = true;
  // Reset inline progress bar
  if (_loadBar) {
    lv_obj_set_width(_loadBar, 0);
    lv_obj_clear_flag(_loadBar, LV_OBJ_FLAG_HIDDEN);
  }
  // No status text during reload — progress bar is sufficient
}

// After editing a field, reload its siblings and any COMMAND fields in the
// same folder (equivalent to reloadRelatedFields() in elrs.lua).
void ElrsParamBrowser::reloadRelatedFields(Field* f)
{
  if (!f) return;
  uint8_t folderParent = (uint8_t)(_currentFolder < 0 ? 0 : _currentFolder);
  // Build reload queue in reverse order so the field itself reloads last
  // (stack semantics — loadQ.back() is processed first)
  for (int i = _fieldCount; i >= 1; i--) {
    if (i < 1 || i > (int)_fields.size()) continue;
    const Field& rel = _fields[i - 1];
    if (!rel.loaded)            continue;
    if (rel.id == f->id)        continue;  // handled below
    // Skip non-data types: FOLDER, BACK, DEVICE
    if (rel.type == FT_FOLDER || rel.type == FT_BACK || rel.type == FT_DEVICE) continue;
    bool sameSiblings = (rel.parent == f->parent);
    bool isCmdInFolder = (rel.type == FT_COMMAND && rel.parent == folderParent);
    if (sameSiblings || isCmdInFolder)
      _loadQueue.push_back((uint8_t)i);
  }
  _loadQueue.push_back(f->id);  // reload edited field last (processed first)
  _fieldChunk   = 0;
  _chunkBuf.clear();
  _state        = State::LOADING;
  _fieldTimeout = get_tmr10ms() + 20;  // brief delay for EEPROM commit
  // Push linkstat request out so it doesn't race with the reload response
  // (matches Lua: linkstatTimeout = fieldTimeout + 100)
  _linkstatTimeout = _fieldTimeout + 100;
  _listDirty    = true;
}

void ElrsParamBrowser::activateField(Field* f)
{
  if (!f) return;

  switch (f->type) {
    case FT_FOLDER:
      openFolder(f);
      break;

    case FT_BACK:
      goBack();
      break;

    case FT_DEVICE:
      // BIND row (id=0xFD) is display-only, don't change device
      if (f->id == 0xFD) break;
      changeDevice(f->id);
      break;

    case FT_COMMAND: {
      // Skip if already in confirmed/cancel state
      if (f->cmdStatus >= 4 && f->cmdStatus <= 5) break;

      // Send command start (lcsStart=1) to device
      uint8_t status = 1;
      sendParamWrite(f->id, &status, 1);
      f->cmdStatus     = 1;
      _cmdFieldId      = f->id;
      // prevStatus=1: handles immediate status=0 response (e.g. Bind done in 1 frame)
      _cmdLastStatus   = 1;
      // Keep the dialog visible for at least 1 second so the user sees it
      _cmdMinCloseTime = get_tmr10ms() + 100;

      // Bind sound (one-shot)
      if (f->name == "Bind") {
        AUDIO_PLAY(AU_SPECIAL_SOUND_CHEEP);
      }

      // Command dialog with spinner
      if (_cmdDialog == nullptr) {
        _cmdDialog = new DynamicMessageDialog(
            elrsTranslateName(f->name).c_str(),
            [this]() -> std::string {
              if (_cmdFieldId >= 1 && _cmdFieldId <= (uint8_t)_fields.size()) {
                const Field& cf = _fields[_cmdFieldId - 1];
                std::string info = cf.cmdInfo;
                if (cf.cmdStatus == 1 || cf.cmdStatus == 2) {
                  // Spinner
                  const char spin[] = "|/-\\";
                  char sp = spin[(get_tmr10ms() / 25) % 4];
                  if (info.empty()) info = STR_ELRS_RUNNING;
                  info += " [";
                  info += sp;
                  info += "]";
                }
                return info;
              }
              return STR_ELRS_WAITING;
            },
            STR_ELRS_PRESS_RTN_CANCEL,
            EdgeTxStyles::STD_FONT_HEIGHT,
            COLOR_THEME_PRIMARY1_INDEX, CENTERED);
        _cmdDialog->setCloseHandler([this]() {
          // RTN: send lcsCancel=5 to device
          if (_cmdFieldId >= 1) {
            uint8_t cancel = 5;
            sendParamWrite(_cmdFieldId, &cancel, 1);
            // Immediately clear local field state so list shows blank
            Field& cf = _fields[_cmdFieldId - 1];
            cf.cmdStatus = 0xFF;
            cf.cmdInfo.clear();
          }
          // Stop polling
          _loadQueue.clear();
          _state         = State::READY;
          _cmdDialog     = nullptr;
          _cmdFieldId    = 0;
          _cmdLastStatus = 0;
          _listDirty     = true;
        });
        elrsDarkDialogStyle(_cmdDialog->getLvObj());
      }

      // Re-read field to get device response
      uint8_t devTimeout = (f->cmdTimeout > 0) ? f->cmdTimeout : 25;
      _loadQueue.push_back(f->id);
      _state        = State::LOADING;
      _fieldTimeout = get_tmr10ms() + devTimeout;
      _listDirty    = true;
      break;
    }

    case FT_UINT8: case FT_INT8: case FT_UINT16: case FT_INT16:
    case FT_FLOAT: case FT_SELECT:
      if (!f->grey) openFieldPopup(f);
      break;

    case FT_INFO:
    case FT_STRING:
    default:
      break;
  }
}

void ElrsParamBrowser::incrFieldValue(Field* f, int step)
{
  if (!f || !_editMode) return;

  switch (f->type) {
    case FT_UINT8:
    case FT_INT8:
    case FT_UINT16:
    case FT_INT16:
    case FT_FLOAT:
      f->value = std::max(f->minVal, std::min(f->maxVal, f->value + step));
      break;
    case FT_SELECT:
      if (!f->options.empty()) {
        int32_t cur = f->value;
        int32_t sz  = (int32_t)f->options.size();
        int32_t next = cur;
        do {
          next = next + step;
          if (next < 0) next = sz - 1;
          if (next >= sz) next = 0;
          // Skip blank entries — matches Lua incrField() which checks
          // #field.values[newval+1] ~= 0
          if (!f->options[next].empty()) {
            f->value = next;
            break;
          }
        } while (next != cur);
      }
      break;
    default:
      break;
  }
  _listDirty = true;
}

void ElrsParamBrowser::handleKey(uint32_t key)
{
  if (_state != State::READY) return;

  Field* f = visibleField(_selectedIdx);
  int cnt   = visibleCount();
  int total = cnt + 3;  // +3: Default, Reload and Close title-bar buttons

  switch (key) {
    // Rotary encoder in edit mode sends LV_KEY_LEFT (CCW) / LV_KEY_RIGHT (CW)
    // Key matrix may send LV_KEY_UP / LV_KEY_DOWN — support both.
    case LV_KEY_RIGHT:
    case LV_KEY_DOWN:
      if (_editMode) {
        incrFieldValue(f, +1);
      } else if (total > 0) {
        _selectedIdx = (_selectedIdx + 1) % total;
        _listDirty = true;
      }
      break;

    case LV_KEY_LEFT:
    case LV_KEY_UP:
      if (_editMode) {
        incrFieldValue(f, -1);
      } else if (total > 0) {
        _selectedIdx = (_selectedIdx - 1 + total) % total;
        _listDirty = true;
      }
      break;

    case LV_KEY_ENTER:
      // Warning mode: ENTER clears ELRS flags
      if (_elrsFlags > 0x1F) {
        _elrsFlags = 0;
        uint8_t clearCmd[] = {_deviceId, _handsetId, 0x2E, 0x00};
        crsfPush(CRSF_PARAM_WRITE_ID, clearCmd, sizeof(clearCmd));
        _listDirty = true;
        break;
      }
      if (_selectedIdx == cnt)     { applyDefaultSettings(); break; }
      if (_selectedIdx == cnt + 1) { doReload(); break; }
      if (_selectedIdx == cnt + 2) { deleteLater(); break; }
      activateField(f);
      break;

    default:
      break;
  }
}

// ---------------------------------------------------------------------------
// showModuleSetupMenu
// ---------------------------------------------------------------------------

void ElrsParamBrowser::showModuleSetupMenu()
{
  lv_label_set_text(_titleLabel, STR_ELRS_NO_CRSF);

  auto menu = new Menu();
  applyDarkMenuStyle(menu);
  menu->setTitle(STR_ELRS_CONFIGURE_CRSF);
  menu->setCancelHandler([this]() { _shouldClose = true; });

#if defined(HARDWARE_INTERNAL_MODULE)
  menu->addLine(STR_ELRS_INTERNAL_CRSF, [this]() {
    setModuleType(INTERNAL_MODULE, MODULE_TYPE_CROSSFIRE);
    g_eeGeneral.internalModuleBaudrate = CROSSFIRE_INDEX_TO_STORE(3);  // 1.87M
    storageDirty(EE_GENERAL);
    storageDirty(EE_MODEL);
    doReload();
  });
#endif

#if defined(HARDWARE_EXTERNAL_MODULE)
  menu->addLine(STR_ELRS_EXTERNAL_CRSF, [this]() {
    setModuleType(EXTERNAL_MODULE, MODULE_TYPE_CROSSFIRE);
    g_model.moduleData[EXTERNAL_MODULE].crsf.telemetryBaudrate =
        CROSSFIRE_INDEX_TO_STORE(3);  // 1.87M
    storageDirty(EE_MODEL);
    doReload();
  });
#endif

  menu->addLine(STR_CANCEL, [this]() {
    deleteLater();
  });
}
