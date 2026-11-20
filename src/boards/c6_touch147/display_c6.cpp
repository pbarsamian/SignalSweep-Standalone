// SPDX-License-Identifier: GPL-3.0-or-later
// C6 display HAL: LovyanGFX panel from lgfx_c6.h, backlight on GPIO23 (LEDC via Light_PWM).
#include <Arduino.h>
#include "hal/board.h"
#include "hal/display.h"

void display_init(Display& gfx, uint8_t brightness) {
    // Both chip selects idle high before either device is initialised (they share SCK/MOSI)
    pinMode(PIN_LCD_CS, OUTPUT); digitalWrite(PIN_LCD_CS, HIGH);
    pinMode(PIN_SD_CS, OUTPUT);  digitalWrite(PIN_SD_CS, HIGH);
    gfx.init();
    gfx.setRotation(BOARD_ROTATION);
    gfx.setBrightness(brightness);
    gfx.fillScreen(0);
}
