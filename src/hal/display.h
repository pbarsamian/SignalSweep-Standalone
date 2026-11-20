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

// Set the backlight level (0..255) at any time after display_init(); used by alert patterns.
void display_backlight(uint8_t level);

// Shared SPI host (C6: LCD and TF slot on SPI2). release: the display driver lets go of the bus so another driver can
// use it; restore: it takes the bus back. UI task only, never inside a drawing transaction. No-ops on the CYD.
void display_bus_release();
void display_bus_restore();
