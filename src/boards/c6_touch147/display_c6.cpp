// SPDX-License-Identifier: GPL-3.0-or-later
// C6 display HAL: LovyanGFX panel from lgfx_c6.h, backlight on GPIO23 (LEDC via Light_PWM).
#include <Arduino.h>
#include "hal/board.h"
#include "hal/display.h"

static Display* s_gfx = nullptr;

void display_init(Display& gfx, uint8_t brightness) {
    s_gfx = &gfx;
    // Both chip selects idle high before either device is initialised (they share SCK/MOSI)
    pinMode(PIN_LCD_CS, OUTPUT); digitalWrite(PIN_LCD_CS, HIGH);
    pinMode(PIN_SD_CS, OUTPUT);  digitalWrite(PIN_SD_CS, HIGH);
    gfx.init();
    gfx.setRotation(BOARD_ROTATION);
    gfx.setBrightness(brightness);
    gfx.fillScreen(0);
}

void display_backlight(uint8_t level) { if (s_gfx) s_gfx->setBrightness(level); }

void display_bus_release() {
    if (!s_gfx) return;
    s_gfx->endWrite();
    s_gfx->waitDMA();
    static_cast<lgfx::Panel_Device*>(s_gfx->getPanel())->releaseBus();
}

void display_bus_restore() {
    if (!s_gfx) return;
    static_cast<lgfx::Panel_Device*>(s_gfx->getPanel())->initBus();
}
