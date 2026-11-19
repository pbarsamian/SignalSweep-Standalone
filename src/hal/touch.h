// SPDX-License-Identifier: GPL-3.0-or-later
// touch.h — touch HAL. Coordinates returned are SCREEN coordinates (0..SCREEN_W-1, 0..SCREEN_H-1) in the UI's
// orientation. Calibration and axis swap/invert are the board's business, never the core's.
#pragma once
void touch_init();
bool touch_get(int& tx, int& ty);     // true while touched
