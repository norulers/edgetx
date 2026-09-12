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

#include "window.h"
#include "telemetry/telemetry.h"

#include <string>
#include <vector>

class DynamicMessageDialog;  // forward declaration
class ConfirmDialog;         // forward declaration

// Native C++ ELRS parameter browser.
//
// Replicates elrs.lua behaviour using the same CRSF parameter protocol:
//   PING (0x28) -> DEVICE_INFO (0x29) -> PARAM_READ (0x2C) -> PARAM_INFO (0x2B)
//   PARAM_WRITE (0x2D) for edits / commands
//
// The window registers a TelemetryQueue on construction and deregisters on
// destruction so it does not interfere with the normal telemetry path.

class ElrsParamBrowser : public Window
{
 public:
  explicit ElrsParamBrowser(Window* parent);
  ~ElrsParamBrowser() override;

  // CRSF addresses used by ELRS
  static constexpr uint8_t CRSF_MODULE_ADDR  = 0xEE;
  static constexpr uint8_t CRSF_RADIO_ADDR   = 0xEF;  // ELRS handset id
  static constexpr uint8_t CRSF_BROADCAST    = 0x00;

  // CRSF frame types
  static constexpr uint8_t CRSF_PING_ID        = 0x28;
  static constexpr uint8_t CRSF_DEVINFO_ID     = 0x29;
  static constexpr uint8_t CRSF_PARAM_READ_ID  = 0x2C;
  static constexpr uint8_t CRSF_PARAM_WRITE_ID = 0x2D;
  static constexpr uint8_t CRSF_ELRS_STATUS_ID = 0x2E;

  // handsetId for non-ELRS-TX devices (RADIO_TRANSMITTER address)
  static constexpr uint8_t CRSF_RADIO_ADDR_OTHER = 0xEA;

  // ELRS field types (matches elrs.lua type numbering, 0-based, +1 in lua)
  enum FieldType : uint8_t {
    FT_UINT8   = 0,
    FT_INT8    = 1,
    FT_UINT16  = 2,
    FT_INT16   = 3,
    FT_FLOAT   = 8,
    FT_SELECT  = 9,
    FT_STRING  = 10,
    FT_FOLDER  = 11,
    FT_INFO    = 12,
    FT_COMMAND = 13,
    FT_BACK    = 14,
    FT_DEVICE  = 15,  // virtual: a discovered CRSF device entry in "Other Devices"
  };

  // A discovered CRSF device (populated by parseDeviceInfo responses)
  struct DeviceEntry {
    uint8_t     id;
    std::string name;
    uint8_t     fieldCount;
    bool        isElrs;      // serial number == "ELRS"
    std::string fwVersion;   // e.g. "v3.4.0" decoded from DEVICE_INFO sw_version
  };

  struct Field {
    uint8_t    id       = 0;
    uint8_t    parent   = 0;      // 0 = root (ELRS device field id)
    FieldType  type     = FT_INFO;
    bool       hidden   = false;
    bool       grey     = false;  // disabled (e.g. single-option select)
    bool       loaded   = false;

    std::string name;
    std::string unit;

    // Integer / float fields
    int32_t  value   = 0;
    int32_t  minVal  = 0;
    int32_t  maxVal  = 0;
    int32_t  step    = 1;
    uint8_t  byteSize = 1;  // 1 or 2 or 4
    bool     isSigned = false;

    // Float precision
    uint8_t  decimalPoint = 0;

    // Select options (FT_SELECT)
    std::vector<std::string> options;

    // String / info value (FT_STRING, FT_INFO)
    std::string strValue;

    // Command state (FT_COMMAND)
    uint8_t cmdStatus  = 0;
    uint8_t cmdTimeout = 0;
    std::string cmdInfo;
  };

 public:
  void handleKey(uint32_t key);     // called by encoder polling
  void onRowTouch(int rowIdx);      // called by LVGL row click callback
  void openFieldPopup(Field* f);    // opens popup Menu editor for touch
  void commitField(Field* f);       // commits field value + reloads
  void showModuleSetupMenu();       // prompts user to enable CRSF when no module is configured
  void onCancel() override;         // RTN/back button handler
  void onClicked() override;        // ENT button handler

 protected:
  void checkEvents() override;

 private:
  // ------------------------------------------------------------------
  // Protocol state
  // ------------------------------------------------------------------
  enum class State { IDLE, PINGING, LOADING, READY };
  State   _state          = State::IDLE;

  uint8_t _deviceId       = CRSF_MODULE_ADDR;
  uint8_t _handsetId      = CRSF_RADIO_ADDR;  // 0xEF for ELRS TX, 0xEA for other devices
  uint8_t _fieldCount     = 0;

  std::vector<Field>       _fields;
  std::vector<uint8_t>     _loadQueue;      // stack of field IDs to load
  std::vector<DeviceEntry> _devices;        // all discovered CRSF devices

  // Current open folder (-1 = root).
  // int16_t avoids sign overflow when ELRS field IDs exceed 127.
  int16_t _currentFolder  = -1;

  // Timeout tracker (in 10ms units from get_tmr10ms())
  tmr10ms_t _fieldTimeout = 0;
  tmr10ms_t _pingTimeout  = 0;

  // Chunked receive buffer
  uint8_t           _fieldChunk = 0;
  std::vector<uint8_t> _chunkBuf;

// Link status
  uint16_t  _goodPkt      = 0;
  uint16_t  _badPkt       = 0;
  uint8_t   _elrsFlags    = 0;
  std::string _elrsFlagsInfo;
  tmr10ms_t _linkstatTimeout = 0;

  // ------------------------------------------------------------------
  // Telemetry receive queue
  // ------------------------------------------------------------------
  TelemetryQueue _rxQueue;

  // ------------------------------------------------------------------
  // UI state
  // ------------------------------------------------------------------
  lv_obj_t* _list        = nullptr;
  lv_obj_t* _titleLabel  = nullptr;
  lv_obj_t* _statusLabel = nullptr;
  lv_obj_t* _folderLabel = nullptr;  // center of title bar: current folder name

  int  _selectedIdx  = 0;
  bool _editMode     = false;
  bool _listDirty    = true;   // rebuild needed
  bool _hasConnected = false;  // true after first successful device connect
  bool _shouldClose  = false;  // set by cancelHandler to close on next tick
  std::string _deviceName;    // saved device name for title bar
  std::string _deviceVersion; // e.g. "v3.4.0" from DEVICE_INFO sw_version
  std::string _folderName;    // current folder name ("" at root)

  // Per-row touch click data (heap-allocated, freed on each rebuildList)
  std::vector<void*> _rowClickData;

  // Active command dialog (nullptr when no command running)
  DynamicMessageDialog* _cmdDialog      = nullptr;
  uint8_t               _cmdFieldId     = 0;   // field being waited on
  uint8_t               _cmdLastStatus  = 0;  // previous cmdStatus for transition detection
  tmr10ms_t             _cmdMinCloseTime = 0; // earliest time the dialog may auto-close

  // Parameter loading progress dialog (nullptr when not loading)
  lv_obj_t*             _loadBar       = nullptr;
  lv_obj_t*             _goodBadLabel  = nullptr;  // bad/good   C/- in title bar
  lv_obj_t*             _reloadBtnObj  = nullptr;
  lv_obj_t*             _closeBtnObj   = nullptr;  // title bar Close button (for encoder highlight)
  lv_obj_t*             _defaultBtnObj = nullptr;  // title bar Default button

  // Rotary encoder polling state
  int32_t _prevEncVal      = 0;
  bool    _encInitialized  = false;

  // ------------------------------------------------------------------
  // Protocol helpers
  // ------------------------------------------------------------------
  bool crsfPush(uint8_t cmd, const uint8_t* payload, uint8_t len);
  void sendPing();
  void sendParamRead(uint8_t fieldId, uint8_t chunk);
  void sendParamWrite(uint8_t fieldId, const uint8_t* data, uint8_t len);

  // ------------------------------------------------------------------
  // Telemetry parsing
  // ------------------------------------------------------------------
  void processRxQueue();
  void parseDeviceInfo(const uint8_t* payload, uint8_t len);
  void parseParamInfo(const uint8_t* payload, uint8_t len);
  void parseParamData(Field& f, const uint8_t* d, uint16_t len);
  void parseElrsStatus(const uint8_t* payload, uint8_t len);
  void changeDevice(uint8_t devId);         // switch active device and reload its params
  void rebuildDeviceVirtualFields();        // inject/refresh "Other Devices" virtual entries

  // Parse helpers
  static size_t  readString(const uint8_t* d, size_t off, std::string& out, size_t maxOff = SIZE_MAX);
  static int32_t readInt(const uint8_t* d, size_t off, uint8_t bytes, bool isSigned);
  static size_t  readOpts(const uint8_t* d, size_t off,
                          std::vector<std::string>& opts, bool& grey, size_t maxOff = SIZE_MAX);

  // ------------------------------------------------------------------
  // UI helpers
  // ------------------------------------------------------------------
  void buildUI();          // first-time widget creation
  void rebuildList();      // refresh list rows from _fields
  void doReload();         // full re-discovery from device
  void applyDefaultSettings();  // write 333Hz / FULL / 16CH to device
  void updateStatus();
  void reloadAllFields();                // re-queue all fields (no re-ping)
  void reloadRelatedFields(Field* f);    // after editing: reload siblings+commands

  // Navigation
  int    visibleCount() const;
  Field* visibleField(int n);   // non-const: returns pointer into _fields
  void   clampSelection();
  void   openFolder(Field* f);
  void   goBack();
  void   activateField(Field* f);
  void   incrFieldValue(Field* f, int step);

  // Module detection
  static uint8_t crsfModuleIdx();  // INTERNAL=0 or EXTERNAL=1; 0xFF if none
  static uint8_t crsfEndpoint(uint8_t modIdx);

 public:
  // Background cache loading (used by FPV dashboard power popup)
  static void triggerCacheLoad();
};

// Session-level parameter cache — avoids re-loading on every menu open.
// Shared with layout_fpv_dash.cpp for power popup quick access.
struct ElrsParamCache {
  bool    valid       = false;
  uint8_t deviceId    = 0;
  uint8_t fieldCount  = 0;
  std::string devName;
  std::string devVersion;
  std::vector<ElrsParamBrowser::Field> fields;
};

extern ElrsParamCache g_elrsCache;
