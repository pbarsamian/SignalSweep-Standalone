// SPDX-License-Identifier: GPL-3.0-or-later
// C6 alert output (Phase 5). The board has no speaker, RGB LED or LDR, so:
//   - alerts are backlight pulse patterns, one pattern per threat type (settings: led_en),
//   - hunt-mode "LED" pulses flash the backlight (led_en) and hunt "clicks" use the optional buzzer (speaker_en),
//   - an optional passive buzzer on GPIO7 (PIN_FREE_IO7, the only free pin) is compiled in only with -DC6_HAS_BUZZER=1.
// Patterns run as a small state machine advanced by alert_tick() from the UI task, so nothing here blocks
// (the CYD's alert_fire() blocks for the tone). NEEDS HARDWARE TEST: the buzzer path has not been run (no buzzer fitted).
#include <Arduino.h>
#include "hal/board.h"
#include "hal/display.h"
#include "hal/alert_output.h"

#ifndef C6_HAS_BUZZER
#define C6_HAS_BUZZER 0
#endif

#define BL_HIGH 255
#define BL_LOW   12     // "off" phase of a pulse: dim, not black, so the screen stays readable

struct Pattern { uint8_t pulses; uint16_t on_ms, off_ms; };
// By threat type (ThreatType order: DRONE, TRACKER, CAMERA, BODYCAM, ATTACK, UNKNOWN). Distinct rhythms, ~1 s long.
static const Pattern PATTERNS[] = {
    {3,  90,  90},   // drone: three quick
    {2, 200, 150},   // tracker: two medium
    {1, 600,   0},   // camera: one long
    {2,  90,  90},   // bodycam: two quick
    {5,  60,  60},   // attack: five rapid
    {1, 150,   0},   // unknown
};

static uint8_t  s_base = 200;          // the user's brightness, restored when a pattern ends
static bool     s_run = false;
static Pattern  s_pat = {0, 0, 0};
static int      s_pulse = 0;           // pulses started so far
static bool     s_on = false;
static uint32_t s_edge = 0;
static uint16_t s_tone = 0;

static void tone_on(uint16_t hz) {
#if C6_HAS_BUZZER
    if (hz) tone(PIN_FREE_IO7, hz);
#else
    (void)hz;
#endif
}
static void tone_off() {
#if C6_HAS_BUZZER
    noTone(PIN_FREE_IO7);
#endif
}

void alert_init() {
#if C6_HAS_BUZZER
    pinMode(PIN_FREE_IO7, OUTPUT);
    digitalWrite(PIN_FREE_IO7, LOW);
#endif
}

static uint16_t tone_for(ThreatType t) {
    switch (t) {
        case ThreatType::DRONE:   return TONE_DRONE;
        case ThreatType::TRACKER: return TONE_TRACKER;
        case ThreatType::CAMERA:  return TONE_CAMERA;
        case ThreatType::BODYCAM: return TONE_BODYCAM;
        case ThreatType::ATTACK:  return TONE_ATTACK;
        default:                  return 0;
    }
}

void alert_fire(ThreatType t, const Settings& cfg) {
    s_base = cfg.brightness;
    if (!cfg.led_en && !(C6_HAS_BUZZER && cfg.speaker_en)) return;
    int i = (int)t; if (i < 0 || i > 5) i = 5;
    s_pat = PATTERNS[i];
    s_tone = (C6_HAS_BUZZER && cfg.speaker_en) ? tone_for(t) : 0;
    s_pulse = 0; s_on = false; s_edge = 0; s_run = true;      // first pulse starts on the next alert_tick()
}

void alert_tick(const Settings& cfg) {
    s_base = cfg.brightness;
    if (!s_run) return;
    uint32_t now = millis();
    if (!s_on) {
        if (s_pulse >= s_pat.pulses) {                          // pattern finished
            s_run = false;
            tone_off();
            display_backlight(s_base);
            return;
        }
        if (s_pulse > 0 && now - s_edge < s_pat.off_ms) return;
        s_on = true; s_edge = now; s_pulse++;
        if (cfg.led_en) display_backlight(BL_HIGH);
        tone_on(s_tone);
    } else if (now - s_edge >= s_pat.on_ms) {
        s_on = false; s_edge = now;
        tone_off();
        display_backlight(cfg.led_en && s_pulse < s_pat.pulses ? BL_LOW : s_base);
    }
}

// Hunt mode: the UI calls these on the pulse cadence of the target's signal strength
void alert_click_start() { tone_on(2000); }
void alert_click_stop()  { tone_off(); }
void alert_hunt_led(bool on, float closeness) {
    (void)closeness;
    display_backlight(on ? BL_HIGH : s_base);
}
void alert_all_off() {
    s_run = false;
    tone_off();
    display_backlight(s_base);
}
