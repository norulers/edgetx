/*
 * Copyright (C) EdgeTX
 *
 * Based on code named
 *   opentx - https://github.com/opentx/opentx
 *   th9x - http://code.google.com/p/th9x
 *   er9x - http://code.google.com/p/er9x
 *   gruvin9x - http://code.google.com/p/gruvin9x
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#pragma once

#include "bitmaps.h"
#include "etx_lv_theme.h"
#include "window.h"

// Persistent bottom dock bar on the main view with quick-access navigation buttons.
// Inspired by the FPV dashboard concept: ELRS / SYSTEM / AUDIO / TOOLS
class BottomDock : public Window
{
 public:
  explicit BottomDock(Window* parent);

  // Height of the dock bar — same as the top bar for visual symmetry
  static constexpr coord_t DOCK_H = EdgeTxStyles::MENU_HEADER_HEIGHT;

  // Invisible sentinel object in the encoder group.
  // Focusing it hides any selection highlight (used by RTN/Cancel handlers).
  static lv_obj_t* getFocusSentinel() { return _focusSentinel; }

 protected:
  // Create one dock button at x-offset with icon + label
  void addDockButton(coord_t x, coord_t btnW, EdgeTxIcon icon,
                     const char* label,
                     std::function<uint8_t()> action);

  static lv_obj_t* _focusSentinel;
};
