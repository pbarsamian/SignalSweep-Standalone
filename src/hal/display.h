// SPDX-License-Identifier: GPL-3.0-or-later
// display.h — display HAL.
//
// The board header supplies `Display`, the drawing object type (cyd: TFT_eSPI). The UI keeps calling the
// same primitives on it (fillRect, drawString, ...). Everything panel-specific (pins, backlight PWM,
// rotation, colour/inversion fixes) lives behind display_init(); the UI does not know any of it.
#pragma once
#include <stdint.h>
#include "board.h"

// Bring up the backlight and the panel; leaves the screen in the UI's native orientation (SCREEN_W x SCREEN_H).
void display_init(Display& gfx, uint8_t brightness);
