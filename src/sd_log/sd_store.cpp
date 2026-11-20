// SPDX-License-Identifier: GPL-3.0-or-later
// sd_store.cpp — see sd_store.h. Row formats and the CSV/JSON text cleaning are the CYD work's, mapped onto Detection.
#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include "hal/board.h"
#include "sd_store.h"
#include "signatures.h"     // CONF_ALERT_MIN
#include "hal/display.h"    // display_bus_release / display_bus_restore

#ifndef FW_VERSION
#define FW_VERSION "v3.21-c6d"
#endif

SdStats g_sd_stats = {};

static bool     s_mounted = false, s_tried = false;
static uint32_t s_try_ms = 0;
static char     s_card[28] = "not checked";

static const char* const TYPES[] = { "drone", "tracker", "camera", "bodycam", "attack", "unknown" };
static const char* const MAC_MODES[] = { "full", "oui", "none" };

const char* sd_card_text() { return s_card; }

// Bus handover. The LCD driver (LovyanGFX) keeps SPI2 claimed, and the Arduino SD driver cannot mount the card while it does
// (measured: SD.begin fails at every clock with the display running, and mounts first time without it). So every SD operation
// is a session: the display lets go of the bus, the card is mounted, the I/O is done, the card is unmounted and the display
// takes the bus back. A session costs a few tens of ms; alerts and exports are rare, so that is acceptable.
// All sessions run in the UI task, which is the only task that draws.
static bool open_session() {
    if (!s_mounted && s_tried && millis() - s_try_ms < 3000) return false;   // do not hammer an empty slot
    s_tried = true;
    s_try_ms = millis();
    display_bus_release();
    SPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
    static const uint32_t FREQ[] = { 10000000, 4000000 };   // second try for a card that is slow to start
    bool ok = false;
    for (size_t i = 0; i < sizeof(FREQ) / sizeof(FREQ[0]) && !ok; i++) {
        bool b = SD.begin(PIN_SD_CS, SPI, FREQ[i]);
        ok = b && SD.cardType() != CARD_NONE;
        if (!ok) { Serial.printf("[sd] mount at %lu Hz failed (begin=%d)\n", (unsigned long)FREQ[i], b); SD.end(); }
    }
    if (!ok) {
        SPI.end();
        display_bus_restore();
        s_mounted = false;
        snprintf(s_card, sizeof(s_card), "no card");
        return false;
    }
    SD.mkdir("/signalsweep");
    snprintf(s_card, sizeof(s_card), "ready %.1f GB", (double)SD.cardSize() / (1024.0 * 1024.0 * 1024.0));
    s_mounted = true;
    return true;
}

static void close_session() {
    SD.end();
    SPI.end();
    pinMode(PIN_SD_CS, OUTPUT); digitalWrite(PIN_SD_CS, HIGH);
    display_bus_restore();
}

struct SdSession {
    bool ok;
    SdSession() : ok(open_session()) {}
    ~SdSession() { if (ok) close_session(); }
};

bool sd_card_ok() { SdSession s; return s.ok; }   // a probe: mounts and unmounts

// Text for a CSV cell or a JSON string: commas, quotes, backslashes and control characters become '_'.
static void clean(char* out, size_t cap, const char* s) {
    size_t j = 0;
    for (; *s && j + 1 < cap; s++) {
        unsigned char c = (unsigned char)*s;
        out[j++] = (c < 0x20 || c == ',' || c == '"' || c == '\\') ? '_' : (char)c;
    }
    out[j] = 0;
}

static const char* radio_text(uint8_t proto) {
    return proto == (PROTO_BLE | PROTO_WIFI) ? "both" : proto == PROTO_BLE ? "ble" : "wifi";
}
static int band_of(uint8_t proto) { return (proto & PROTO_WIFI) ? 2 : 0; }   // 2 = 2.4 GHz, 0 = BLE (no 5 GHz source here)

// ── alerts.csv ───────────────────────────────────────────────────────────────
// The device has no clock: boot_ms is milliseconds since this boot.
void sd_log_alert(const Detection& d) {
    SdSession ses;
    if (!ses.ok) { g_sd_stats.alerts_no_card++; return; }
    const char* path = "/signalsweep/alerts.csv";
    bool fresh = !SD.exists(path);
    File f = SD.open(path, FILE_APPEND);
    if (!f) { s_mounted = false; g_sd_stats.alerts_failed++; return; }
    if (fresh) f.print("boot_ms,type,confidence,strongest,mac,name,ssid,radio,band,channel,rssi,times_heard\r\n");
    char mac[20], name[40], ssid[40];
    d.mac_str(mac);
    clean(name, sizeof(name), d.name);
    clean(ssid, sizeof(ssid), d.ssid);
    int t = (int)d.type; if (t < 0 || t > 5) t = 5;
    size_t w = f.printf("%lu,%s,%u,%u,%s,%s,%s,%s,%d,%d,%d,%u\r\n", (unsigned long)millis(), TYPES[t], d.confidence, d.best_weight,
                        mac, name, ssid, radio_text(d.proto), band_of(d.proto), d.channel, d.rssi, (unsigned)d.seen_count);
    f.close();
    if (w) g_sd_stats.alerts_logged++; else g_sd_stats.alerts_failed++;
}

void sd_dump_alerts() {
    SdSession ses;
    if (!ses.ok) { Serial.printf("[alerts] no card (%s)\n", sd_card_text()); return; }
    File f = SD.open("/signalsweep/alerts.csv", FILE_READ);
    if (!f) { Serial.println("[alerts] no alerts.csv yet"); return; }
    Serial.printf("[alerts] --- begin (%lu bytes) ---\n", (unsigned long)f.size());
    uint8_t buf[128];
    size_t total = 0;
    while (f.available() && total < 65536) {
        size_t n = f.read(buf, sizeof(buf));
        if (!n) break;
        Serial.write(buf, n);
        total += n;
        delay(1);
    }
    f.close();
    Serial.println("\n[alerts] --- end ---");
}

// ── list export, schema v1 (SignalSweep-CYD PROTOCOL.md "Detection record, schema v1") ──
static void mac_text(char* out, size_t cap, const Detection& d, SdMacMode mode) {
    if (mode == SDMAC_NONE) { out[0] = 0; return; }
    char m[20]; d.mac_str(m);
    if (mode == SDMAC_FULL) { snprintf(out, cap, "%s", m); return; }
    snprintf(out, cap, "%.8s:xx:xx:xx", m);                          // AA:BB:CC + masked device part
}

bool sd_list_export(const DetectionRegistry& reg, SdMacMode mode, const char* filter, char* msg, size_t msgsz) {
    int n = reg.count;
    if (n <= 0) { snprintf(msg, msgsz, "nothing to save"); return false; }
    SdSession ses;
    if (!ses.ok) { snprintf(msg, msgsz, "no SD card"); return false; }
    int k = 1;
    char csv[40], json[40];
    for (; k < 1000; k++) {
        snprintf(csv, sizeof(csv), "/signalsweep/list_%03d.csv", k);
        snprintf(json, sizeof(json), "/signalsweep/list_%03d.json", k);
        if (!SD.exists(csv) && !SD.exists(json)) break;
    }
    if (k >= 1000) { snprintf(msg, msgsz, "no free file name"); return false; }
    File fc = SD.open(csv, FILE_WRITE);
    File fj = SD.open(json, FILE_WRITE);
    if (!fc || !fj) {
        if (fc) fc.close();
        if (fj) fj.close();
        s_mounted = false;
        snprintf(msg, msgsz, "cannot create the files");
        return false;
    }
    uint32_t now = millis();
    char flt[64];
    clean(flt, sizeof(flt), filter ? filter : "");
    fc.print("type,confidence,strongest,mac,name,ssid,radio,band,channel,rssi,first_seen_ago_ms,last_seen_ago_ms,times_heard,alerted,muted\r\n");
    fj.printf("{\"schema\":\"signalsweep-detections\",\"version\":1,\"fw\":\"%s\",\"boot_ms\":%lu,\"mac_mode\":\"%s\",\"filter\":\"%s\",\"count\":%d,\"detections\":[",
              FW_VERSION, (unsigned long)now, MAC_MODES[mode], flt, n);
    bool ok = true;
    for (int i = 0; i < n; i++) {
        const Detection& d = reg.items[i];
        char mac[24], name[40], ssid[40];
        mac_text(mac, sizeof(mac), d, mode);
        clean(name, sizeof(name), d.name);
        clean(ssid, sizeof(ssid), d.ssid);
        int t = (int)d.type; if (t < 0 || t > 5) t = 5;
        const char* type = TYPES[t];
        bool alerted = d.best_weight >= CONF_ALERT_MIN;              // same definition as the CYD export: strongest signal reached the alert threshold
        unsigned long first_ago = (unsigned long)(now - d.first_seen_ms), last_ago = (unsigned long)(now - d.timestamp_ms);
        size_t w1 = fc.printf("%s,%u,%u,%s,%s,%s,%s,%d,%d,%d,%lu,%lu,%u,%u,%u\r\n", type, d.confidence, d.best_weight, mac, name, ssid,
                              radio_text(d.proto), band_of(d.proto), d.channel, d.rssi, first_ago, last_ago, (unsigned)d.seen_count,
                              alerted ? 1 : 0, d.muted ? 1 : 0);
        size_t w2 = fj.printf("%s{\"type\":\"%s\",\"confidence\":%u,\"strongest\":%u,\"mac\":\"%s\",\"name\":\"%s\",\"ssid\":\"%s\",\"radio\":\"%s\","
                              "\"band\":%d,\"channel\":%d,\"rssi\":%d,\"first_seen_ago_ms\":%lu,\"last_seen_ago_ms\":%lu,\"times_heard\":%u,"
                              "\"alerted\":%s,\"muted\":%s}",
                              i ? "," : "", type, d.confidence, d.best_weight, mac, name, ssid, radio_text(d.proto), band_of(d.proto),
                              d.channel, d.rssi, first_ago, last_ago, (unsigned)d.seen_count, alerted ? "true" : "false", d.muted ? "true" : "false");
        if (!w1 || !w2) ok = false;
    }
    fj.print("]}\r\n");
    fc.close();
    fj.close();
    if (!ok) { snprintf(msg, msgsz, "SD write failed"); return false; }
    snprintf(msg, msgsz, "list_%03d: %d saved", k, n);
    Serial.printf("[list] saved %s and .json (%d detections, MAC %s, %s)\n", csv, n, MAC_MODES[mode], flt);
    return true;
}
