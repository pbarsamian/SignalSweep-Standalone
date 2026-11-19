// SPDX-License-Identifier: GPL-3.0-or-later
// alert_output.h — how a board expresses an alert. The core says "this threat type", "hunt closeness";
// the board decides tone / LED / backlight pulse / nothing. All calls are made from the UI task.
#pragma once
#include "detection.h"
#include "settings.h"

void alert_init();                                       // outputs off, speaker/LED hardware ready
void alert_fire(ThreatType t, const Settings& cfg);      // one alert; may block the UI task (as v3.21 did)
void alert_click_start();                                // hunt-mode click on
void alert_click_stop();                                 // hunt-mode click off
void alert_hunt_led(bool on, float closeness);           // closeness 0 (far) .. 1 (on top of it)
void alert_all_off();                                    // tone and LED off
