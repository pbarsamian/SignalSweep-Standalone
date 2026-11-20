// SPDX-License-Identifier: GPL-3.0-or-later
// C6 alert output. PHASE 4 PLACEHOLDER: the board has no speaker, RGB LED or LDR, so alerts and hunt feedback are
// drawn by the UI (header flash, hunt ring). Phase 5 adds backlight pulse patterns and the optional GPIO7 buzzer here
// (compile-time C6_HAS_BUZZER, open question Q2 in docs/port/AUDIT.md).
#include <Arduino.h>
#include "hal/board.h"
#include "hal/alert_output.h"

void alert_init() {}
void alert_fire(ThreatType, const Settings&) {}
void alert_click_start() {}
void alert_click_stop() {}
void alert_hunt_led(bool, float) {}
void alert_all_off() {}
