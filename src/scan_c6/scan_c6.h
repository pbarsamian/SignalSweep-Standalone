// SPDX-License-Identifier: GPL-3.0-or-later
// SignalSweep (C6) — scanning core: radio callbacks that only copy, a worker task that matches, a control task
// that time-slices the radio. Task layout, priorities, stack sizes and queue depths: docs/port/TASKS.md.
#pragma once
#include <stdint.h>
#include "settings.h"
#include "ble_adv.h"

// ── Tunables ──────────────────────────────────────────────────────────────────
#define SCAN_SLICE_MS      6000   // length of one BLE or WiFi slice in the sweep (CYD: BLE_SLOT_MS / WIFI_SLOT_MS, both 6000)
#define WIFI_HOP_MS          40   // dwell per WiFi channel while sweeping (CYD scanner_wifi_tick: 40 ms)
#define WIFI_RAW_MAX        512   // bytes of a management frame kept for matching (longer frames are cut, see TASKS.md)
#define WIFI_DATA_COPY       64   // data frames (focus only) need the header, not the body
#define WIFI_POOL            12   // frame buffers shared by the WiFi callback and the worker
#define BLE_QUEUE_DEPTH      24   // advertisements waiting for the worker

enum ScanMode : uint8_t { SCAN_SWEEP = 0, SCAN_BLE_ONLY, SCAN_WIFI_ONLY };   // BLE_ONLY/WIFI_ONLY: bench testing

struct ScanC6Stats {
    volatile uint32_t wifi_cb, wifi_qdrop, wifi_trunc;   // frames offered / dropped (pool or queue full) / cut to WIFI_RAW_MAX
    volatile uint32_t ble_cb, ble_qdrop, ble_trunc;
    volatile uint32_t wifi_done, ble_done;               // processed by the worker
    volatile uint32_t wifi_us_total, wifi_us_max;        // worker time per WiFi frame (esp_timer microseconds)
    volatile uint32_t ble_us_total, ble_us_max;
    volatile uint32_t wifi_type[6];                      // callback's frame type: 0 mgmt 1 ctrl 2 data 3 misc 4 data-mpdu 5 data-ampdu
    volatile uint32_t mgmt_sub[16];                      // management subtype histogram (first byte >> 4)
    volatile uint16_t wifi_len_max;                      // longest sig_len seen
    volatile uint8_t  mac_check;                         // 0 not yet, 1 callback MAC matches NimBLE's string, 2 MISMATCH
};
extern ScanC6Stats g_scan_stats;

// What the radios have been doing: a small summary the UI can draw instead of a spectrum (docs/port/UI_CUTS.md).
struct ScanActivity {
    uint32_t wifi_frames = 0;     // management frames seen in the slice
    uint32_t wifi_beacons = 0;
    uint32_t ble_adverts = 0;
    int8_t   wifi_best_rssi = -127;
    int8_t   ble_best_rssi  = -127;
};
// live = the slice in progress, last = the slice that just ended (0s until one has ended)
void scan_c6_activity(ScanActivity& live, ScanActivity& last, bool& ble_active, uint8_t& channel);

// Creates the queues, the worker and the control task and starts scanning. Call once, after g_reg / g_reg_mutex /
// g_queue / g_cfg exist and the default event loop has been created.
void scan_c6_start(const Settings& cfg);
void scan_c6_set_mode(ScanMode m);
ScanMode scan_c6_mode();

// Frame-type survey: widen the promiscuous filter to every type and only count (nothing is queued).
void scan_c6_survey(bool on);
bool scan_c6_surveying();

// Test injection: put a frame/advert through exactly the path a radio callback uses.
bool scan_c6_inject_ble(const BleAdv& a);
bool scan_c6_inject_wifi(uint8_t type, const uint8_t* frame, int len, int rssi, int ch);

void scan_c6_print_info();   // heap, stack high-water marks, queue fill, timing
