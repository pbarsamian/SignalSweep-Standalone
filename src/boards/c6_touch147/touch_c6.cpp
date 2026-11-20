// SPDX-License-Identifier: GPL-3.0-or-later
// C6 touch HAL: AXS5106L over I2C (touch_axs5106l.h), raw portrait coordinates -> screen coordinates with the
// fitted constants in board_c6_touch147.h (docs/port/BRINGUP_RESULTS.md), plus press/move/release events.
#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include "hal/board.h"
#include "hal/touch.h"
#include "hal/touch_events.h"
#include "touch_axs5106l.h"

// The controller raises INT once per frame while a finger is down and sends nothing on release that this driver
// reads, so a release is "no frame for RELEASE_MS". (Frames were seen every 10-20 ms in the Phase 2 capture.)
#define RELEASE_MS 70

static bool     s_down = false;
static int      s_x = 0, s_y = 0;
static uint32_t s_last_ms = 0;

void touch_init() {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    bool ok = axs::begin(PIN_TP_RST, PIN_TP_INT);
    Serial.printf("[touch] AXS5106L %s\n", ok ? "ready" : "ID read FAILED");
}

static void map_point(const axs::Point& p, int& sx, int& sy) {
    float rx = TOUCH_SWAP_XY ? p.y : p.x;
    float ry = TOUCH_SWAP_XY ? p.x : p.y;
    sx = (int)lroundf(TOUCH_SX_A * rx + TOUCH_SX_B);
    sy = (int)lroundf(TOUCH_SY_A * ry + TOUCH_SY_B);
    // TOUCH_INVERT_X/Y are 0 on this board and are already part of the fit; not applied again.
    sx = constrain(sx, 0, SCREEN_W - 1);
    sy = constrain(sy, 0, SCREEN_H - 1);
}

bool touch_poll(TouchEvent& e) {
    axs::Point p[1];
    uint32_t now = millis();
    if (axs::read(p, 1) >= 1) {
        map_point(p[0], s_x, s_y);
        e.kind = s_down ? TE_MOVE : TE_DOWN;
        s_down = true;
        s_last_ms = now;
        e.x = (int16_t)s_x; e.y = (int16_t)s_y; e.ms = now;
        return true;
    }
    if (s_down && now - s_last_ms > RELEASE_MS) {
        s_down = false;
        e.kind = TE_UP; e.x = (int16_t)s_x; e.y = (int16_t)s_y; e.ms = now;
        return true;
    }
    return false;
}

bool touch_get(int& tx, int& ty) {
    tx = s_x; ty = s_y;
    return s_down;
}
