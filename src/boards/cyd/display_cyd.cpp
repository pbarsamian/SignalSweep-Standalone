// SPDX-License-Identifier: GPL-3.0-or-later
// CYD display: ILI9341 via TFT_eSPI, backlight on LEDC ch0. The body is the block that used to sit at the top of
// ui_init() in src/ui.cpp, moved unchanged.
#include <Arduino.h>
#include "hal/board.h"
#include "hal/display.h"

void display_init(Display& tft, uint8_t brightness) {
    ledcSetup(0, 5000, 8);
    ledcAttachPin(PIN_TFT_BL, 0);
    ledcWrite(0, brightness);

    tft.init();
    tft.setRotation(3);
    // Panel colour fix — confirmed by boot colour test (MODE 2):
    // rotation-3 MADCTL with BGR bit (0xE8) + display inversion ON.
    tft.writecommand(0x36);
    tft.writedata(0xE8);
    tft.invertDisplay(true);
}

void display_backlight(uint8_t level) { ledcWrite(0, level); }
