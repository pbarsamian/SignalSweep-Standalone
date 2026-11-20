// SPDX-License-Identifier: GPL-3.0-or-later
// CYD alert output: SC8002B speaker on LEDC ch1 (GPIO26) and the active-LOW RGB LED. Bodies moved unchanged from
// src/ui.cpp (ui_alert, hunt_led, hunt_all_off, the hunt click) and src/main.cpp (LED init).
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "hal/board.h"
#include "hal/alert_output.h"

void alert_init() {
    // LED — all off (active LOW)
    pinMode(PIN_LED_R, OUTPUT); digitalWrite(PIN_LED_R, HIGH);
    pinMode(PIN_LED_G, OUTPUT); digitalWrite(PIN_LED_G, HIGH);
    pinMode(PIN_LED_B, OUTPUT); digitalWrite(PIN_LED_B, HIGH);

    // Speaker on LEDC channel 1 (SC8002B amp via PIN_SPEAKER) — needed for
    // both alert beeps and hunt-mode clicks.
    ledcSetup(1, 2000, 8);
    ledcAttachPin(PIN_SPEAKER, 1);
    ledcWrite(1, 0);
}

void alert_fire(ThreatType t, const Settings& s) {
    if (s.speaker_en) {
        uint32_t freq=880;
        switch(t){
            case ThreatType::DRONE:   freq=TONE_DRONE;   break;
            case ThreatType::TRACKER: freq=TONE_TRACKER; break;
            case ThreatType::CAMERA:  freq=TONE_CAMERA;  break;
            case ThreatType::BODYCAM: freq=TONE_BODYCAM; break;
            case ThreatType::ATTACK:  freq=TONE_ATTACK;  break;
            default: return;
        }
        ledcWriteTone(1, freq);
        vTaskDelay(pdMS_TO_TICKS(TONE_MS));
        ledcWriteTone(1, 0);
    }
    if (s.led_en) {
        uint8_t r=HIGH,g=HIGH,b=HIGH;
        switch(t){
            case ThreatType::DRONE:   b=LOW; break;
            case ThreatType::TRACKER: r=LOW; break;
            case ThreatType::CAMERA:  r=LOW; break;
            case ThreatType::BODYCAM: r=LOW; b=LOW; break;
            case ThreatType::ATTACK:  r=LOW; g=LOW; break;
            default: return;
        }
        digitalWrite(PIN_LED_R,r); digitalWrite(PIN_LED_G,g); digitalWrite(PIN_LED_B,b);
        vTaskDelay(pdMS_TO_TICKS(200));
        digitalWrite(PIN_LED_R,HIGH); digitalWrite(PIN_LED_G,HIGH); digitalWrite(PIN_LED_B,HIGH);
    }
}

void alert_click_start() { ledcWriteTone(1, 3000); }
void alert_click_stop()  { ledcWrite(1, 0); }

void alert_hunt_led(bool on, float t) {
    // Approximate the heat colour on the RGB LED (active LOW). t = closeness, 0 far .. 1 close.
    bool lr = on && t > 0.4f;     // red grows with closeness
    bool lg = on && t < 0.6f;     // green fades with closeness
    bool lb = on && t < 0.3f;     // blue only when far
    digitalWrite(PIN_LED_R, lr?LOW:HIGH);
    digitalWrite(PIN_LED_G, lg?LOW:HIGH);
    digitalWrite(PIN_LED_B, lb?LOW:HIGH);
}

void alert_all_off() {
    ledcWrite(1, 0);
    digitalWrite(PIN_LED_R, HIGH); digitalWrite(PIN_LED_G, HIGH); digitalWrite(PIN_LED_B, HIGH);
}

void alert_tick(const Settings&) {}
