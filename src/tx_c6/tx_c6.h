// SPDX-License-Identifier: GPL-3.0-or-later
// tx_c6.h — opt-in transmit module (env c6touch_tx only, -DSS_TX_ENABLED). Evil Portal (captive-portal credential
// capture) and single-shot WiFi/BLE spoof frames, for testing SignalSweep's own detector and captive-portal
// handling against known traffic in a controlled setting. Never compiled into the default passive-scanner build:
// the normal envs (cyd, c6scan, c6touch, c6touch_buzzer) never include src/tx_c6/.
//
// Both features own the radios outright while running (scan_c6_suspend(true)/(false) around them) rather than
// interleaving with the sweep — see scan_c6.h. All calls here are from the UI task.
#pragma once
#include <stdint.h>
#include <stddef.h>

void tx_c6_init();   // call once from setup(), after scan_c6_start()

// ── Evil Portal: open AP + captive-portal DNS redirect + a generic login form ──
bool     tx_c6_portal_start();
void     tx_c6_portal_stop();
bool     tx_c6_portal_active();
uint32_t tx_c6_portal_hits();
void     tx_c6_portal_last(char* out, size_t n);   // last "user / pass" captured, for the UI row
void     tx_c6_portal_poll();                      // call every UI loop iteration; no-op when not active

// ── Single-shot spoof frames: send exactly one, then the radio is handed back to the sweep ────────────────────
// WiFi: one beacon matching the DJI Tello signature (same SSID the demo/bench-test hotspot "TELLO-TEST01" uses),
// so a scanner (this one or another) should raise the same drone alert it would for the real hotspot.
void tx_c6_spoof_wifi_once();
// BLE: advertises an Apple offline-finding long-form payload (same shape as the AirTag demo inject) for ~3 s, then
// stops. Long enough for a nearby scanner to catch it in one BLE slice.
void tx_c6_spoof_ble_once();
