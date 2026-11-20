// SPDX-License-Identifier: GPL-3.0-or-later
// SignalSweep (C6) — scanning core. See docs/port/TASKS.md for the task layout and the reasoning.
//
//   WiFi task / NimBLE host task:  callbacks copy the frame into a pool slot / the advert into a queue and return.
//   "scanwork" (prio 1):           takes items from both queues and runs the matching (the CYD's callback bodies).
//   "scanctl"  (prio 3):           time-slices BLE/WiFi, hops channels, runs focus mode and registry upkeep.
//
// Callbacks never take g_reg_mutex, never format strings and never call into the registry, so the radio tasks
// are held for a memcpy and a queue send instead of a full rule walk.

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <esp_wifi.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <NimBLEDevice.h>

#include "scan_c6.h"
#include "scanner_wifi.h"
#include "detection.h"
#include "signatures.h"
#include "focus.h"

extern DetectionRegistry g_reg;
extern SemaphoreHandle_t g_reg_mutex;
extern Settings          g_cfg;

ScanC6Stats g_scan_stats = {};

// ── Shared state ──────────────────────────────────────────────────────────────
struct WifiRaw {
    uint8_t  type;       // wifi_promiscuous_pkt_type_t
    int8_t   rssi;
    uint8_t  ch;
    uint16_t sig_len;    // real frame length
    uint16_t len;        // bytes copied
    uint8_t  data[WIFI_RAW_MAX];
};
static WifiRaw        s_pool[WIFI_POOL];
static QueueHandle_t  s_free;     // indices of idle pool slots
static QueueHandle_t  s_wq;       // indices of filled pool slots
static QueueHandle_t  s_bq;       // BleAdv by value
static QueueSetHandle_t s_set;
static TaskHandle_t   s_worker_h, s_control_h;

static volatile ScanMode s_mode = SCAN_SWEEP;
static volatile bool     s_survey = false;
static bool              s_wifi_ready = false;
static volatile bool     s_ble_active = true;
static volatile uint8_t  s_channel = 0;

static ScanActivity s_act_live, s_act_last;

// ── WiFi callback: copy and go ────────────────────────────────────────────────
static bool wifi_enqueue(uint8_t type, const uint8_t* frame, int sig_len, int rssi, int ch) {
    uint8_t slot;
    if (xQueueReceive(s_free, &slot, 0) != pdTRUE) { g_scan_stats.wifi_qdrop++; return false; }
    WifiRaw& r = s_pool[slot];
    int cap = (type == WIFI_PKT_DATA) ? WIFI_DATA_COPY : WIFI_RAW_MAX;
    int n = sig_len < cap ? sig_len : cap;
    memcpy(r.data, frame, n);
    r.type = type; r.rssi = (int8_t)rssi; r.ch = (uint8_t)ch;
    r.sig_len = (uint16_t)sig_len; r.len = (uint16_t)n;
    if (n < sig_len && type != WIFI_PKT_DATA) g_scan_stats.wifi_trunc++;
    if (xQueueSend(s_wq, &slot, 0) != pdTRUE) {
        xQueueSend(s_free, &slot, 0);
        g_scan_stats.wifi_qdrop++;
        return false;
    }
    return true;
}

static void wifi_cb(void* buf, wifi_promiscuous_pkt_type_t type) {
    auto* pkt = (wifi_promiscuous_pkt_t*)buf;
    int len  = pkt->rx_ctrl.sig_len;
    int rssi = pkt->rx_ctrl.rssi;
    g_scan_stats.wifi_cb++;
    if ((unsigned)type < 6) g_scan_stats.wifi_type[type]++;
    if (len > g_scan_stats.wifi_len_max) g_scan_stats.wifi_len_max = (uint16_t)len;
    if (type == WIFI_PKT_MGMT && len >= 1) g_scan_stats.mgmt_sub[(pkt->payload[0] >> 4) & 0x0F]++;
    if (s_survey) return;                                   // survey counts only
    if (type != WIFI_PKT_MGMT && type != WIFI_PKT_DATA) return;
    if (type == WIFI_PKT_MGMT) {
        if (len < 24) return;
        s_act_live.wifi_frames++;
        if (((pkt->payload[0] >> 4) & 0x0F) == 8) s_act_live.wifi_beacons++;
        if (rssi > s_act_live.wifi_best_rssi) s_act_live.wifi_best_rssi = (int8_t)rssi;
    }
    wifi_enqueue(type, pkt->payload, len, rssi, pkt->rx_ctrl.channel);
}

// ── BLE callback: copy and go ─────────────────────────────────────────────────
static bool ble_enqueue(const BleAdv& a) {
    if (xQueueSend(s_bq, &a, 0) != pdTRUE) { g_scan_stats.ble_qdrop++; return false; }
    return true;
}

class SweepCallbacks : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* d) override {
        g_scan_stats.ble_cb++;
        BleAdv a;
        const uint8_t* v = d->getAddress().getVal();      // NimBLE keeps the address least-significant byte first
        for (int i = 0; i < 6; i++) a.mac[i] = v[5 - i];
        a.addr_type = d->getAddress().getType();
        a.adv_type  = d->getAdvType();
        a.rssi      = d->getRSSI();
        const std::vector<uint8_t>& pl = d->getPayload();
        size_t n = pl.size();
        a.truncated = n > BLE_ADV_MAX;
        if (a.truncated) { n = BLE_ADV_MAX; g_scan_stats.ble_trunc++; }
        a.len = (uint8_t)n;
        memcpy(a.payload, pl.data(), n);

        // Once: confirm the byte order against NimBLE's own printed form (docs/port/BRINGUP_RESULTS.md, Phase 3)
        if (g_scan_stats.mac_check == 0) {
            char want[18];
            snprintf(want, sizeof(want), "%02x:%02x:%02x:%02x:%02x:%02x",
                     a.mac[0], a.mac[1], a.mac[2], a.mac[3], a.mac[4], a.mac[5]);
            std::string s = d->getAddress().toString();
            for (auto& c : s) c = (char)tolower((unsigned char)c);
            g_scan_stats.mac_check = (s == want) ? 1 : 2;
        }

        s_act_live.ble_adverts++;
        if (a.rssi > s_act_live.ble_best_rssi) s_act_live.ble_best_rssi = a.rssi;
        ble_enqueue(a);
    }
};
static SweepCallbacks s_cb;

static void ble_start() {
    NimBLEDevice::init("");
    NimBLEDevice::setPower(0);   // dBm. Only the active-scan requests are transmitted
    NimBLEScan* scan = NimBLEDevice::getScan();
    // Same scan settings as the CYD / upstream: duplicates on (RSSI keeps updating), active scan (names arrive in
    // scan responses and the name rules need them), 100/50 interval/window, maxResults 0 so rotating random MACs
    // are not retained forever. 2.x still stores each device until its callbacks are sent, to merge the scan response.
    scan->setScanCallbacks(&s_cb, true);
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(50);
    scan->setMaxResults(0);
    scan->start(0, false, true);
    Serial.println("[ble] init OK");
}

static void ble_full_duty(bool full) {
    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->stop();
    scan->setInterval(100);
    scan->setWindow(full ? 100 : 50);
    scan->start(0, false, true);
}

// ── Radio switching (control task only) ───────────────────────────────────────
static void radio_to_wifi() {
    NimBLEDevice::getScan()->stop();
    if (!s_wifi_ready) { scanner_wifi_init(g_cfg, wifi_cb); s_wifi_ready = true; }
    esp_wifi_set_promiscuous(true);
}

static void radio_to_ble(bool full_duty) {
    if (s_wifi_ready) esp_wifi_set_promiscuous(false);
    ble_full_duty(full_duty);   // (re)starts the scan with the right window
}

static void end_slice() {
    s_act_last = s_act_live;
    s_act_live = ScanActivity();
}

// ── Worker: the matching ──────────────────────────────────────────────────────
static void worker_task(void*) {
    for (;;) {
        QueueSetMemberHandle_t m = xQueueSelectFromSet(s_set, pdMS_TO_TICKS(1000));
        if (m == nullptr) continue;
        if (m == (QueueSetMemberHandle_t)s_wq) {
            uint8_t slot;
            if (xQueueReceive(s_wq, &slot, 0) != pdTRUE) continue;
            WifiRaw& r = s_pool[slot];
            int64_t t0 = esp_timer_get_time();
            wifi_process_frame(r.type, r.data, r.len, r.sig_len, r.rssi, r.ch);
            uint32_t us = (uint32_t)(esp_timer_get_time() - t0);
            g_scan_stats.wifi_done++;
            g_scan_stats.wifi_us_total += us;
            if (us > g_scan_stats.wifi_us_max) g_scan_stats.wifi_us_max = us;
            xQueueSend(s_free, &slot, 0);
        } else if (m == (QueueSetMemberHandle_t)s_bq) {
            BleAdv a;
            if (xQueueReceive(s_bq, &a, 0) != pdTRUE) continue;
            int64_t t0 = esp_timer_get_time();
            ble_process_advert(a);
            uint32_t us = (uint32_t)(esp_timer_get_time() - t0);
            g_scan_stats.ble_done++;
            g_scan_stats.ble_us_total += us;
            if (us > g_scan_stats.ble_us_max) g_scan_stats.ble_us_max = us;
        }
    }
}

// ── Control: slicing, focus, upkeep (the CYD's scan_task, minus the scope) ────
static void control_task(void*) {
    ble_start();
    vTaskDelay(pdMS_TO_TICKS(200));

    uint32_t slot_start = millis(), act_start = slot_start;
    bool ble_active = true;
    uint8_t focus_mode = FOCUS_NONE;   // what the radio is currently doing for focus
    s_ble_active = true;

    for (;;) {
        uint32_t now = millis();
        uint8_t want = g_focus.active ? g_focus.radio : FOCUS_NONE;

        if (want != focus_mode) {
            // ── Enter / change / leave focus ─────────────────────────────────
            if (focus_mode == FOCUS_WIFI && s_wifi_ready) scanner_wifi_data_filter(false);
            if (want == FOCUS_WIFI) {
                if (ble_active) radio_to_wifi();
                ble_active = false;
                scanner_wifi_data_filter(true);   // peers/traffic need data frames
            }
            else if (want == FOCUS_BLE)  { radio_to_ble(true); ble_active = true; }
            else {
                // focus ended: resume the sweep, BLE slot first
                if (!ble_active) { radio_to_ble(false); ble_active = true; }
                else ble_full_duty(false);
            }
            focus_mode = want;
            slot_start = now;
        } else if (focus_mode == FOCUS_NONE) {
            // ── Normal sweep: alternate BLE / WiFi slices (or a forced single radio for bench tests)
            bool want_ble = ble_active;
            if (s_mode == SCAN_BLE_ONLY) want_ble = true;
            else if (s_mode == SCAN_WIFI_ONLY) want_ble = false;
            else if (now - slot_start >= SCAN_SLICE_MS) want_ble = !ble_active;
            if (want_ble != ble_active) {
                if (want_ble) radio_to_ble(false); else radio_to_wifi();
                ble_active = want_ble;
                slot_start = now;
                end_slice();
                act_start = now;
            }
        }
        if (now - act_start >= SCAN_SLICE_MS) { end_slice(); act_start = now; }
        s_ble_active = ble_active;

        if (!ble_active) {
            int lock = (focus_mode == FOCUS_WIFI) ? g_focus.channel : 0;
            s_channel = (uint8_t)scanner_wifi_hop(lock);
            vTaskDelay(pdMS_TO_TICKS(WIFI_HOP_MS));
        } else {
            vTaskDelay(pdMS_TO_TICKS(50));
        }

        if (xSemaphoreTake(g_reg_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            // Focused WiFi target heard transmitting: keep its entry alive and
            // its RSSI current (matters for pivoted peers, which never re-match a rule)
            if (g_focus.active && g_focus.radio == FOCUS_WIFI && g_focus.last_tx_ms &&
                millis() - g_focus.last_tx_ms < 5000) {
                for (int i = 0; i < g_reg.count; i++)
                    if (memcmp(g_reg.items[i].mac, g_focus.mac, 6) == 0) {
                        g_reg.items[i].timestamp_ms = g_focus.last_tx_ms;
                        g_reg.items[i].rssi = g_focus.last_rssi;
                        break;
                    }
            }
            g_reg.prune(millis());
            xSemaphoreGive(g_reg_mutex);
        }
    }
}

// ── Public API ────────────────────────────────────────────────────────────────
void scan_c6_start(const Settings& cfg) {
    (void)cfg;   // live settings are read from g_cfg
    s_free = xQueueCreate(WIFI_POOL, sizeof(uint8_t));
    s_wq   = xQueueCreate(WIFI_POOL, sizeof(uint8_t));
    s_bq   = xQueueCreate(BLE_QUEUE_DEPTH, sizeof(BleAdv));
    s_set  = xQueueCreateSet(WIFI_POOL + BLE_QUEUE_DEPTH);
    for (uint8_t i = 0; i < WIFI_POOL; i++) xQueueSend(s_free, &i, 0);
    xQueueAddToSet(s_wq, s_set);
    xQueueAddToSet(s_bq, s_set);
    xTaskCreate(worker_task,  "scanwork", 6144, nullptr, 1, &s_worker_h);
    xTaskCreate(control_task, "scanctl",  4096, nullptr, 3, &s_control_h);
}

void scan_c6_set_mode(ScanMode m) { s_mode = m; }
ScanMode scan_c6_mode() { return s_mode; }

void scan_c6_survey(bool on) {
    s_survey = on;
    if (!s_wifi_ready) return;
    wifi_promiscuous_filter_t f = { .filter_mask = on
        ? (uint32_t)WIFI_PROMIS_FILTER_MASK_ALL
        : (uint32_t)(g_focus.active && g_focus.radio == FOCUS_WIFI
                     ? (WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA)
                     : WIFI_PROMIS_FILTER_MASK_MGMT) };
    esp_wifi_set_promiscuous_filter(&f);
}
bool scan_c6_surveying() { return s_survey; }

bool scan_c6_inject_ble(const BleAdv& a) { return ble_enqueue(a); }
bool scan_c6_inject_wifi(uint8_t type, const uint8_t* frame, int len, int rssi, int ch) {
    return wifi_enqueue(type, frame, len, rssi, ch);
}

void scan_c6_activity(ScanActivity& live, ScanActivity& last, bool& ble_active, uint8_t& channel) {
    live = s_act_live; last = s_act_last; ble_active = s_ble_active; channel = s_channel;
}

void scan_c6_print_info() {
    const ScanC6Stats& s = g_scan_stats;
    Serial.printf("[heap] free=%u min=%u largest=%u\n",
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    Serial.printf("[stack] worker free-min=%u B (of 6144), control free-min=%u B (of 4096)\n",
        (unsigned)(s_worker_h ? uxTaskGetStackHighWaterMark(s_worker_h) : 0),
        (unsigned)(s_control_h ? uxTaskGetStackHighWaterMark(s_control_h) : 0));
    TaskHandle_t ui = xTaskGetHandle("ui");
    if (ui) Serial.printf("[stack] ui free-min=%u B\n", (unsigned)uxTaskGetStackHighWaterMark(ui));
    Serial.printf("[queue] wifi pool free=%u/%d waiting=%u | ble waiting=%u/%d\n",
        (unsigned)uxQueueMessagesWaiting(s_free), WIFI_POOL, (unsigned)uxQueueMessagesWaiting(s_wq),
        (unsigned)uxQueueMessagesWaiting(s_bq), BLE_QUEUE_DEPTH);
    Serial.printf("[wifi] cb=%u done=%u qdrop=%u trunc=%u (>%d B) longest=%u B | worker avg=%u us max=%u us\n",
        (unsigned)s.wifi_cb, (unsigned)s.wifi_done, (unsigned)s.wifi_qdrop, (unsigned)s.wifi_trunc, WIFI_RAW_MAX,
        (unsigned)s.wifi_len_max, s.wifi_done ? (unsigned)(s.wifi_us_total / s.wifi_done) : 0, (unsigned)s.wifi_us_max);
    Serial.printf("[ble]  cb=%u done=%u qdrop=%u trunc=%u | worker avg=%u us max=%u us | mac order check: %s\n",
        (unsigned)s.ble_cb, (unsigned)s.ble_done, (unsigned)s.ble_qdrop, (unsigned)s.ble_trunc,
        s.ble_done ? (unsigned)(s.ble_us_total / s.ble_done) : 0, (unsigned)s.ble_us_max,
        s.mac_check == 1 ? "OK" : s.mac_check == 2 ? "MISMATCH" : "no advert yet");
    Serial.printf("[types] callback frame types: mgmt=%u ctrl=%u data=%u misc=%u data_mpdu=%u data_ampdu=%u\n",
        (unsigned)s.wifi_type[0], (unsigned)s.wifi_type[1], (unsigned)s.wifi_type[2],
        (unsigned)s.wifi_type[3], (unsigned)s.wifi_type[4], (unsigned)s.wifi_type[5]);
    Serial.print("[mgmt subtypes]");
    for (int i = 0; i < 16; i++) if (s.mgmt_sub[i]) Serial.printf(" %d:%u", i, (unsigned)s.mgmt_sub[i]);
    Serial.println();
}

// ── Synthetic inputs: the whole worker path without needing a transmitter ────
static void inject_ble_penguin() {
    BleAdv a{};
    static const uint8_t mac[6] = {0xC8, 0x11, 0x22, 0x33, 0x44, 0x55};
    memcpy(a.mac, mac, 6); a.addr_type = 1; a.adv_type = 0; a.rssi = -55;
    const char* name = "Penguin-0123456789";
    uint8_t n = (uint8_t)strlen(name), p = 0;
    a.payload[p++] = 2; a.payload[p++] = 0x01; a.payload[p++] = 0x06;      // flags
    a.payload[p++] = (uint8_t)(n + 1); a.payload[p++] = 0x09;              // complete local name
    memcpy(&a.payload[p], name, n); p += n;
    a.len = p;
    Serial.printf("[inject] BLE advert, name '%s' -> %s\n", name, scan_c6_inject_ble(a) ? "queued" : "queue full");
}

static void inject_ble_airtag() {
    BleAdv a{};
    static const uint8_t mac[6] = {0xE2, 0x01, 0x02, 0x03, 0x04, 0x05};
    memcpy(a.mac, mac, 6); a.addr_type = 1; a.adv_type = 0; a.rssi = -60;
    uint8_t p = 0;
    a.payload[p++] = 2; a.payload[p++] = 0x01; a.payload[p++] = 0x06;
    a.payload[p++] = 15; a.payload[p++] = 0xFF;                            // mfg data: Apple, offline-finding long form
    a.payload[p++] = 0x4C; a.payload[p++] = 0x00; a.payload[p++] = 0x12; a.payload[p++] = 0x19;
    for (int i = 0; i < 10; i++) a.payload[p++] = 0;
    a.len = p;
    Serial.printf("[inject] BLE advert, Apple 0x12/0x19 long form -> %s\n", scan_c6_inject_ble(a) ? "queued" : "queue full");
}

static void inject_wifi_tello() {
    uint8_t f[96] = {0};
    int p = 0;
    f[p++] = 0x80; f[p++] = 0x00; p += 2;                                  // beacon, duration
    for (int i = 0; i < 6; i++) f[p++] = 0xFF;                             // addr1: broadcast
    static const uint8_t src[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    memcpy(&f[p], src, 6); p += 6;                                         // addr2
    memcpy(&f[p], src, 6); p += 6;                                         // addr3
    p += 2;                                                                // sequence
    p += 8;                                                                // timestamp
    f[p++] = 0x64; f[p++] = 0x00;                                          // beacon interval
    f[p++] = 0x01; f[p++] = 0x04;                                          // capabilities
    const char* ssid = "TELLO-TEST01";
    f[p++] = 0; f[p++] = (uint8_t)strlen(ssid); memcpy(&f[p], ssid, strlen(ssid)); p += strlen(ssid);
    f[p++] = 1; f[p++] = 1; f[p++] = 0x82;                                 // rates
    f[p++] = 3; f[p++] = 1; f[p++] = 6;                                    // DS parameter set: channel 6
    p += 4;                                                                // FCS
    Serial.printf("[inject] WiFi beacon, SSID '%s' (%d bytes) -> %s\n", ssid, p,
        scan_c6_inject_wifi(WIFI_PKT_MGMT, f, p, -50, 6) ? "queued" : "queue full");
}


void scan_c6_inject_demo(char which) {
    if (which == '1') inject_ble_penguin();
    else if (which == '2') inject_wifi_tello();
    else if (which == '3') inject_ble_airtag();
}
