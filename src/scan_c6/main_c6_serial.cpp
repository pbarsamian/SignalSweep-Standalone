// SPDX-License-Identifier: GPL-3.0-or-later
// SignalSweep C6 — Phase 3 firmware: scanners only, results on the serial port (no display, no touch yet).
// env: c6scan.  Type 'h' on the serial monitor for the test commands.

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <esp_event.h>
#include <esp_wifi.h>
#include <SD.h>
#include <SPI.h>

#include "detection.h"
#include "settings.h"
#include "signatures.h"
#include "focus.h"
#include "scan_c6.h"

#define FW_VERSION "v3.21-c6d"

// ── Shared globals (the CYD's main.cpp owns these there) ─────────────────────
DetectionRegistry g_reg;
SemaphoreHandle_t g_reg_mutex;
QueueHandle_t     g_queue;
Settings          g_cfg;

static bool s_trace = true;     // print every scored match
static bool s_verbose = true;   // 5 s status lines

// ── Match trace: rule, score and outcome for every scored match ──────────────
static int rule_index(const char* name) {
    for (size_t i = 0; i < SIGNATURE_COUNT; i++)
        if (strcmp(SIGNATURES[i].name, name) == 0) return (int)i;
    return -1;   // a protocol detector (Remote ID, AirTag, tracker network, Penguin, IE hint), not a table rule
}

static void trace_post(const Detection& d, int conf, int best, PostStatus st, bool alert) {
    if (!s_trace) return;
    // Repeated updates for one device would flood the port: one line per MAC per 2 s
    if (st == POST_UPDATE) {
        static uint8_t  macs[8][6];
        static uint32_t last[8];
        static uint8_t  next = 0;
        uint32_t now = millis();
        for (int i = 0; i < 8; i++)
            if (memcmp(macs[i], d.mac, 6) == 0 && last[i]) {
                if (now - last[i] < 2000) return;
                last[i] = now;
                goto print;
            }
        memcpy(macs[next], d.mac, 6); last[next] = now; next = (next + 1) & 7;
    }
print:
    char m[20]; d.mac_str(m);
    int idx = rule_index(d.name);
    const char* what = st == POST_LOW ? "below list threshold" : st == POST_LOCK ? "registry busy, dropped"
                     : st == POST_NEW ? "NEW" : "update";
    Serial.printf("[match] %s rule=%s%d '%s' conf=%d best=%d rssi=%d ch=%d %s -> %s%s\n",
        d.proto == PROTO_WIFI ? "WiFi" : "BLE ",
        idx >= 0 ? "#" : "protocol", idx >= 0 ? idx : 0, d.name, conf, best, d.rssi, d.channel, m, what,
        alert ? " ALERT" : "");
}

// ── Reports ───────────────────────────────────────────────────────────────────
static void print_registry() {
    if (xSemaphoreTake(g_reg_mutex, pdMS_TO_TICKS(100)) != pdTRUE) { Serial.println("[reg] busy"); return; }
    Serial.printf("[reg] %d entries\n", g_reg.count);
    uint32_t now = millis();
    for (int i = 0; i < g_reg.count; i++) {
        const Detection& d = g_reg.items[i];
        char m[20]; d.mac_str(m);
        Serial.printf("  #%d %s %-5s '%s' conf=%u best=%u rssi=%d (%d..%d) seen=%u last=%us ago %s%s\n", i, m,
            d.proto == PROTO_WIFI ? "WiFi" : d.proto == PROTO_BLE ? "BLE" : "both", d.name, d.confidence, d.best_weight,
            d.rssi, d.rssi_min, d.rssi_max, (unsigned)d.seen_count, (unsigned)((now - d.timestamp_ms) / 1000),
            d.alerted ? "alerted " : "", d.ssid[0] ? d.ssid : d.dev_name);
    }
    xSemaphoreGive(g_reg_mutex);
}

static void print_status() {
    ScanActivity live, last; bool ble; uint8_t ch;
    scan_c6_activity(live, last, ble, ch);
    Serial.printf("[slot] %s%s ch%u | activity this slice: wifi=%u (beacons %u, best %d dBm) ble=%u (best %d dBm) | last slice: wifi=%u ble=%u\n",
        ble ? "BLE" : "WiFi", scan_c6_mode() == SCAN_SWEEP ? "" : " (forced)", ch,
        (unsigned)live.wifi_frames, (unsigned)live.wifi_beacons, live.wifi_best_rssi, (unsigned)live.ble_adverts, live.ble_best_rssi,
        (unsigned)last.wifi_frames, (unsigned)last.ble_adverts);
    stats_print();
    FocusProfile fp;
    if (focus_copy(fp) && fp.active)
        Serial.printf("[focus] %s ch%d frames=%u adverts=%u data=%u peers=%u\n", fp.radio == FOCUS_WIFI ? "WiFi" : "BLE",
            fp.channel, (unsigned)fp.frames, (unsigned)fp.adv_count, (unsigned)fp.data_frames, (unsigned)fp.peer_count);
}

static void print_help() {
    Serial.println("SignalSweep C6 " FW_VERSION " serial test commands:");
    Serial.println("  h help   i info (heap, stacks, queues, timing, frame-type counts)   r registry   v 5 s status on/off   t match trace on/off");
    Serial.println("  s sweep (6 s BLE / 6 s WiFi)   b BLE only   w WiFi only   y frame-type survey on/off (use with w)");
    Serial.println("  1 inject BLE 'Penguin-...'   2 inject WiFi beacon 'TELLO-TEST01'   3 inject Apple long-form BLE advert");
    Serial.println("  f focus on first registry entry   F focus on last   g end focus");
}

static void handle_char(char c) {
    switch (c) {
        case 'h': case '?': print_help(); break;
        case 'i': scan_c6_print_info(); stats_print(); break;
        case 'r': print_registry(); break;
        case 'v': s_verbose = !s_verbose; Serial.printf("[cmd] status lines %s\n", s_verbose ? "on" : "off"); break;
        case 't': s_trace = !s_trace; Serial.printf("[cmd] match trace %s\n", s_trace ? "on" : "off"); break;
        case 's': scan_c6_set_mode(SCAN_SWEEP);     Serial.println("[cmd] sweep"); break;
        case 'b': scan_c6_set_mode(SCAN_BLE_ONLY);  Serial.println("[cmd] BLE only"); break;
        case 'w': scan_c6_set_mode(SCAN_WIFI_ONLY); Serial.println("[cmd] WiFi only"); break;
        case 'y': {
            bool on = !scan_c6_surveying();
            scan_c6_survey(on);
            Serial.printf("[cmd] frame-type survey %s (counts only; nothing is matched). 'i' prints the type counts\n", on ? "ON" : "off");
            break;
        }
        case 'd': {   // diagnostic: mount the TF card with no display driver running (GPIO1 SCK, 2 MOSI, 3 MISO, 4 CS)
            SPI.begin(1, 3, 2, 4);
            static const uint32_t F[] = { 10000000, 4000000, 400000 };
            for (size_t i = 0; i < 3; i++) {
                bool b = SD.begin(4, SPI, F[i]);
                Serial.printf("[sd-diag] %lu Hz: begin=%d cardType=%d size=%llu MB\n", (unsigned long)F[i], b, b ? SD.cardType() : -1, b ? SD.cardSize() / (1024ULL * 1024ULL) : 0ULL);
                if (b) { SD.end(); break; }
                SD.end();
            }
            break;
        }
        case '1': case '2': case '3': scan_c6_inject_demo(c); break;
        case 'f': case 'F': {   // f: first registry entry, F: last
            uint8_t mac[6]; uint8_t radio = FOCUS_NONE, ch = 0; bool ok = false;
            if (xSemaphoreTake(g_reg_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                if (g_reg.count > 0) {
                    const Detection& t = g_reg.items[c == 'f' ? 0 : g_reg.count - 1];
                    memcpy(mac, t.mac, 6);
                    radio = (t.proto & PROTO_WIFI) ? FOCUS_WIFI : FOCUS_BLE;
                    ch = (uint8_t)t.channel;
                    ok = true;
                }
                xSemaphoreGive(g_reg_mutex);
            }
            if (ok) focus_start(mac, radio, ch); else Serial.println("[cmd] registry is empty, nothing to focus");
            break;
        }
        case 'g': focus_stop(); Serial.println("[cmd] focus ended"); break;
        default: break;
    }
}

// ── Setup / loop ──────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(1500);   // let the USB CDC port enumerate so the boot lines are not lost
    Serial.println("[SignalSweep C6 " FW_VERSION "] boot (scanners only, serial output)");

    settings_load(g_cfg);
    focus_init();
    g_reg_mutex = xSemaphoreCreateMutex();
    g_queue     = xQueueCreate(16, sizeof(int));
    g_post_trace = trace_post;

    // Arduino-ESP32 3.x does not create the ESP-IDF default event loop for a sketch that never uses WiFi.h;
    // esp_wifi_init() posts events to it ("failed to post WiFi event=43 ret=259" in the Phase 2 bring-up).
    esp_err_t e = esp_event_loop_create_default();
    Serial.printf("[boot] default event loop: %s\n", e == ESP_OK ? "created" : esp_err_to_name(e));

    scan_c6_start(g_cfg);
    print_help();
}

void loop() {
    while (Serial.available()) handle_char((char)Serial.read());

    // alerts the registry queued (the UI will drain this in Phase 4)
    int idx;
    while (xQueueReceive(g_queue, &idx, 0) == pdTRUE) {
        if (xSemaphoreTake(g_reg_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            if (idx >= 0 && idx < g_reg.count) {
                char m[20]; g_reg.items[idx].mac_str(m);
                Serial.printf("[ALERT] %s '%s' %s rssi=%d\n", g_reg.items[idx].proto == PROTO_WIFI ? "WiFi" : "BLE",
                    g_reg.items[idx].name, m, g_reg.items[idx].rssi);
            }
            xSemaphoreGive(g_reg_mutex);
        }
    }

    static uint32_t s_status_ms = 0;
    if (s_verbose && millis() - s_status_ms >= 5000) { s_status_ms = millis(); print_status(); }
    delay(20);
}
