/*
 * Copyright (C) EdgeTX
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

#if defined(COLORLCD) && defined(UI_SCREENS_SEPARATE_STORAGE)

// Keeps this firmware's main-view configuration (custom screens, top bar
// widgets, current view) out of the shared MODELS/*.yml, so the same SD card
// can be swapped between this firmware and stock EdgeTX without either side
// overwriting the other's UI setup.
//
// Our configuration lives in /SCREENS/<model file>; the `screens:` /
// `topbarData:` / `view:` keys of the model file keep whatever stock EdgeTX
// last wrote there.

// Called once the model yaml has been parsed: stashes the stock configuration
// and loads ours from the companion file.
void uiScreensLoad(const char* filename);

// Drops the stashed stock configuration, so a model switch never carries the
// previous model's screens over.
void uiScreensClearStock();

// Bracket the model yaml write: the first call saves our configuration to the
// companion file and swaps the stock one back in, the second call restores it.
void uiScreensBeginModelWrite(const char* filename);
void uiScreensEndModelWrite();

// Removes the companion file of a model that no longer exists.
void uiScreensDelete(const char* filename);

// Keeps a duplicated model's UI configuration in sync with its source.
void uiScreensCopy(const char* srcFilename, const char* destFilename);

#else

inline void uiScreensLoad(const char*) {}
inline void uiScreensClearStock() {}
inline void uiScreensBeginModelWrite(const char*) {}
inline void uiScreensEndModelWrite() {}
inline void uiScreensDelete(const char*) {}
inline void uiScreensCopy(const char*, const char*) {}

#endif
