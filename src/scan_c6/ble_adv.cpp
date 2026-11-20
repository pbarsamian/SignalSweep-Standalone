// SPDX-License-Identifier: GPL-3.0-or-later
// SignalSweep (C6) — BLE advertisement matching on raw payload bytes.
//
// This is the same rule logic as src/scanner_ble.cpp (the CYD's NimBLE-1.4 scanner), which in turn is ported from
// upstream B0z0dcl0wn/SignalSweep firmware/src/mode_watchers_watch.cpp (GPL-3.0-or-later). It differs only in where
// the fields come from: the CYD asks a NimBLEAdvertisedDevice, this file walks the advertisement's AD structures
// itself. Reason: NimBLE-Arduino 2.x devices cannot be copied out of the scan callback (they sit in an intrusive
// list), and the C6 callback must only copy bytes and return (docs/port/TASKS.md). The two files are deliberately
// kept in step; unify them after the CYD hardware tests (docs/port/AUDIT.md, Phase 3 notes).
//
// Known differences from the CYD scanner, all in how the fields are read:
//   - service UUIDs of 32 bits are expanded to the 128-bit form, so one equal to the Bluetooth base UUID with a
//     zero high half also matches a 16-bit rule (NimBLE 1.4 treated those as unmatched).
//   - the device name is the Complete Local Name, else the Shortened one.
//   - the MAC comes in as bytes from the callback (checked once against NimBLE's string form, see scan_c6.cpp).

#include <Arduino.h>
#include "ble_adv.h"
#include "detection.h"
#include "settings.h"
#include "signatures.h"
#include "odid_util.h"
#include "focus.h"

extern Settings g_cfg;

// ~1 KB; file-static to keep it off the worker's stack (upstream does the same)
static ODID_UAS_Data s_bleUas;

// ── AD structure walking ──────────────────────────────────────────────────────
// Finds the index-th AD structure of `type`; data/dlen exclude the length and type bytes.
static bool ad_find(const BleAdv& a, uint8_t type, int index, const uint8_t*& data, uint8_t& dlen) {
    size_t off = 0;
    int seen = 0;
    while (off + 1 < a.len) {
        uint8_t l = a.payload[off];
        if (l == 0 || off + 1 + l > a.len) break;
        if (a.payload[off + 1] == type && seen++ == index) {
            data = &a.payload[off + 2];
            dlen = (uint8_t)(l - 1);
            return true;
        }
        off += l + 1;
    }
    return false;
}

static bool ad_have(const BleAdv& a, uint8_t type) {
    const uint8_t* d; uint8_t n;
    return ad_find(a, type, 0, d, n);
}

static bool ad_name(const BleAdv& a, char* out, size_t cap) {
    const uint8_t* d; uint8_t n;
    if (!ad_find(a, 0x09, 0, d, n) && !ad_find(a, 0x08, 0, d, n)) { out[0] = 0; return false; }
    size_t c = n < cap - 1 ? n : cap - 1;
    memcpy(out, d, c);
    out[c] = 0;
    return true;
}

// First manufacturer-specific data field: company ID (LE) then data
static bool ad_mfg(const BleAdv& a, const uint8_t*& d, uint8_t& n) { return ad_find(a, 0xFF, 0, d, n); }

// Service data for a 16-bit UUID; returns a pointer to the bytes after the UUID
static bool ad_svcdata16(const BleAdv& a, uint16_t uuid, const uint8_t*& d, uint8_t& n) {
    const uint8_t* p; uint8_t pl;
    for (int i = 0; ad_find(a, 0x16, i, p, pl); i++) {
        if (pl >= 2 && (uint16_t)(p[0] | (p[1] << 8)) == uuid) { d = p + 2; n = (uint8_t)(pl - 2); return true; }
    }
    return false;
}

// ── Service UUIDs ─────────────────────────────────────────────────────────────
struct Uuid { uint8_t bits; uint8_t b[16]; };   // b little-endian, as on air; bits/8 bytes used
#define MAX_UUIDS 16

static int uuid_list(const BleAdv& a, Uuid* out, int cap) {
    int n = 0;
    size_t off = 0;
    while (off + 1 < a.len) {
        uint8_t l = a.payload[off];
        if (l == 0 || off + 1 + l > a.len) break;
        uint8_t t = a.payload[off + 1];
        int bits = (t == 0x02 || t == 0x03) ? 16 : (t == 0x04 || t == 0x05) ? 32 : (t == 0x06 || t == 0x07) ? 128 : 0;
        if (bits) {
            int step = bits / 8;
            for (int p = 0; p + step <= l - 1 && n < cap; p += step) {
                out[n].bits = (uint8_t)bits;
                memset(out[n].b, 0, 16);
                memcpy(out[n].b, &a.payload[off + 2 + p], step);
                n++;
            }
        }
        off += l + 1;
    }
    return n;
}

static const char BASE_TAIL[] = "-0000-1000-8000-00805f9b34fb";

// Canonical lowercase 36-character form (16/32-bit UUIDs expanded on the Bluetooth base UUID)
static void uuid_str(const Uuid& u, char out[37]) {
    if (u.bits == 16) snprintf(out, 37, "0000%02x%02x%s", u.b[1], u.b[0], BASE_TAIL);
    else if (u.bits == 32) snprintf(out, 37, "%02x%02x%02x%02x%s", u.b[3], u.b[2], u.b[1], u.b[0], BASE_TAIL);
    else snprintf(out, 37, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                  u.b[15], u.b[14], u.b[13], u.b[12], u.b[11], u.b[10], u.b[9], u.b[8],
                  u.b[7], u.b[6], u.b[5], u.b[4], u.b[3], u.b[2], u.b[1], u.b[0]);
}

// Upstream uuidMatches(): exact match, never substring
static bool uuid_matches(const Uuid& u, const char* rule) {
    char want[48];
    size_t k = 0;
    while (*rule == ' ' && *rule) rule++;
    for (; rule[k] && k < sizeof(want) - 1; k++) want[k] = (char)tolower((unsigned char)rule[k]);
    while (k && want[k - 1] == ' ') k--;
    want[k] = 0;
    const char* w = (want[0] == '0' && want[1] == 'x') ? want + 2 : want;
    char have[37];
    if (strlen(w) == 4) {
        uint16_t v = (uint16_t)strtoul(w, NULL, 16);
        if (u.bits == 16) return (uint16_t)(u.b[0] | (u.b[1] << 8)) == v;
        uuid_str(u, have);
        char full[37];
        snprintf(full, sizeof(full), "0000%s%s", w, BASE_TAIL);
        return strcmp(have, full) == 0;
    }
    if (u.bits != 128) return false;
    uuid_str(u, have);
    return strcmp(have, w) == 0;
}

// ── Upstream matchDeviceAgainstRule() ───────────────────────────────────────
static int match_rule(const BleAdv& a, const Signature& s) {
    if (s.ssid[0]) return 0;                 // SSID rules are Wi-Fi only
    if (sig_gated_off(s, g_cfg)) return 0;   // attack gear while Attack is off
    int w = 0;
    if (s.oui[0]) {
        if (!oui_matches(a.mac, s.oui)) return 0;
        w += W_OUI;
    }
    if (s.mfg[0]) {
        const uint8_t* m; uint8_t n;
        if (!ad_mfg(a, m, n) || n < 2) return 0;
        uint16_t id = m[0] | (m[1] << 8);
        if (id != parse_mfg_id(s.mfg)) return 0;
        w += W_MFG;
    }
    if (s.dev[0]) {
        char name[32];
        if (!ad_name(a, name, sizeof(name))) return 0;
        for (char* c = name; *c; c++) *c = (char)tolower((unsigned char)*c);
        char want[32];
        size_t k = 0;
        for (; s.dev[k] && k < sizeof(want) - 1; k++) want[k] = (char)tolower((unsigned char)s.dev[k]);
        want[k] = 0;
        if (!strstr(name, want)) return 0;
        w += W_NAME;
    }
    if (s.uuid[0]) {
        Uuid u[MAX_UUIDS];
        int n = uuid_list(a, u, MAX_UUIDS);
        if (n == 0) return 0;
        bool hit = false;
        for (int i = 0; i < n && !hit; i++) hit = uuid_matches(u[i], s.uuid);
        if (!hit) return 0;
        w += W_UUID;
    }
    if (w == 0) return 0;
    if (s.weight > 0) w = s.weight;
    return w > 100 ? 100 : w;
}

// ── Upstream bleIsAirtag(): long form only ──────────────────────────────────
// Every iPhone/iPad/Mac sends the 2-byte 0x12 status ping; only an item
// separated from its owner sends the 25-byte (0x19) offline-finding payload.
static bool ble_is_airtag(const BleAdv& a) {
    const uint8_t* m; uint8_t n;
    if (!ad_mfg(a, m, n) || n < 4) return false;
    uint16_t company = m[0] | (m[1] << 8);
    return company == 0x004C && m[2] == 0x12 && m[3] == 0x19;
}

// ── Upstream bleTrackerNetwork(): FMDN / Tile / Samsung service data ───────
static const char* ble_tracker_network(const BleAdv& a) {
    const uint8_t* d; uint8_t n;
    if (!ad_have(a, 0x16)) return nullptr;
    if (ad_svcdata16(a, 0xFEAA, d, n) && n >= 1) {
        if (d[0] == 0x41) return "Google Find My (unwanted)";
        if (d[0] == 0x40) return "Google Find My Tracker";
        return nullptr;   // ordinary Eddystone beacon
    }
    if (ad_svcdata16(a, 0xFEED, d, n) && n > 0) return "Tile Tracker";
    if (ad_svcdata16(a, 0xFD5A, d, n) && n > 0) return "Samsung SmartTag";
    if (ad_svcdata16(a, 0xFD59, d, n) && n > 0) return "Samsung SmartTag (setup)";
    return nullptr;
}

// ── Upstream blePenguin(): Flock solar battery ──────────────────────────────
static bool is_ten_digits(const char* s, size_t len, size_t from) {
    if (len != from + 10) return false;
    for (size_t i = from; i < len; i++) if (s[i] < '0' || s[i] > '9') return false;
    return true;
}
static bool ble_penguin(const BleAdv& a) {
    char name[32];
    ad_name(a, name, sizeof(name));
    size_t nl = strlen(name);
    if (is_ten_digits(name, nl, 8) && strncasecmp(name, "penguin-", 8) == 0) return true;
    const uint8_t* m; uint8_t n;
    if (!ad_mfg(a, m, n) || n < 2 || m[0] != 0xC8 || m[1] != 0x09) return false;
    if (is_ten_digits(name, nl, 0)) return true;
    for (size_t i = 2; i + 2 < n; i++)
        if (m[i] == 'T' && m[i+1] == 'N' && m[i+2] >= '0' && m[i+2] <= '9') return true;
    return false;
}

// ── Upstream bleDecodeRemoteId(): UUID 0xFFFA service data ──────────────────
static bool ble_decode_remote_id(const BleAdv& a, ODID_UAS_Data& out) {
    const uint8_t* p = a.payload;
    size_t len = a.len;
    if (len < 4) return false;
    size_t off = 0;
    while (off + 1 < len) {
        uint8_t adLen = p[off];
        if (adLen == 0 || off + 1 + adLen > len) break;
        uint8_t adType = p[off + 1];
        if (adType == 0x16 && adLen >= 3) {
            uint16_t uuid = p[off + 2] | (p[off + 3] << 8);
            if (uuid == 0xFFFA) {
                // ASTM F3411 BT4 framing: UUID, app code 0x0D, counter, message
                memset(&out, 0, sizeof(out));
                if (adLen >= 5 + ODID_MESSAGE_SIZE && p[off + 4] == 0x0D)
                    odid_decode_into(out, &p[off + 6], adLen - 5);
                return true;   // presence alone = a drone broadcasting Remote ID
            }
        }
        off += adLen + 1;
    }
    return false;
}

// ── Descriptive fields for the info view (no effect on scoring) ────────────
static uint16_t uuid16_of(const Uuid& u, bool& is128) {
    is128 = false;
    if (u.bits == 16) return (uint16_t)(u.b[0] | (u.b[1] << 8));
    if (u.bits == 32 && u.b[2] == 0 && u.b[3] == 0) return (uint16_t)(u.b[0] | (u.b[1] << 8));
    is128 = true;   // genuine 128-bit, or a 32-bit UUID that is not a 16-bit alias
    return 0;
}

static void add_svc(Detection& d, uint16_t v) {
    for (int k = 0; k < d.svc_count; k++) if (d.svc[k] == v) return;
    if (d.svc_count < MAX_SVC) d.svc[d.svc_count++] = v;
}

static void capture_fields(const BleAdv& a, Detection& d) {
    // Address type. Random sub-type from the two MSBs of the address.
    if (a.addr_type == 0 /* BLE_ADDR_PUBLIC */) {
        d.addr_type = ADDR_BLE_PUBLIC;
    } else {
        uint8_t top = d.mac[0] >> 6;
        d.addr_type = top == 3 ? ADDR_BLE_RANDOM_STATIC
                    : top == 1 ? ADDR_BLE_RPA
                    : top == 0 ? ADDR_BLE_NRPA : ADDR_UNKNOWN;
    }
    char name[32];
    if (ad_name(a, name, sizeof(name))) snprintf(d.dev_name, sizeof(d.dev_name), "%s", name);
    const uint8_t* p; uint8_t n;
    if (ad_find(a, 0x0A, 0, p, n) && n >= 1) d.tx_power = (int8_t)p[0];
    if (ad_find(a, 0x19, 0, p, n) && n >= 2) { d.has_appearance = true; d.appearance = p[0] | (p[1] << 8); }
    // HCI LE advertising report event types: 0 ADV_IND, 1 ADV_DIRECT_IND are
    // connectable; 2 SCAN_IND, 3 NONCONN_IND, 4 SCAN_RSP are not.
    d.connectable = (a.adv_type == 0 || a.adv_type == 1);
    if (ad_mfg(a, p, n) && n >= 2) {
        d.company = p[0] | (p[1] << 8);
        if (d.company == 0x004C && n >= 3) {
            d.apple_type = p[2];
            if (d.apple_type == 0x12 && n >= 4) d.apple_info = p[3];
            else if (d.apple_type == 0x10 && n >= 5) d.apple_info = p[4];
        }
    }
    bool is128;
    Uuid u[MAX_UUIDS];
    int cnt = uuid_list(a, u, MAX_UUIDS);
    for (int i = 0; i < cnt; i++) {
        uint16_t v = uuid16_of(u[i], is128);
        if (is128) d.svc_128 = true; else add_svc(d, v);
    }
    // Service-data UUIDs (AD 0x16 16-bit, 0x20 32-bit, 0x21 128-bit)
    for (int i = 0; ad_find(a, 0x16, i, p, n); i++)
        if (n >= 2) add_svc(d, (uint16_t)(p[0] | (p[1] << 8)));
    for (int i = 0; ad_find(a, 0x20, i, p, n); i++)
        if (n >= 4) {
            Uuid x{}; x.bits = 32; memcpy(x.b, p, 4);
            uint16_t v = uuid16_of(x, is128);
            if (is128) d.svc_128 = true; else add_svc(d, v);
        }
    if (ad_have(a, 0x21)) d.svc_128 = true;
}

void ble_process_advert(const BleAdv& a) {
    g_stats.ble_adv++;

    // Focus: deep-collect every advert from the target, matched or not
    if (focus_target(a.mac, FOCUS_BLE)) focus_note_ble(a.payload, a.len);

    int confidence = 0, best = 0;
    const char* rule = nullptr;
    const char* category = nullptr;

    for (size_t i = 0; i < SIGNATURE_COUNT; i++) {
        int w = match_rule(a, SIGNATURES[i]);
        if (w > 0) {
            confidence += w;
            if (w > best) { best = w; rule = SIGNATURES[i].name; category = SIGNATURES[i].category; }
        }
    }

    // Protocol detectors (upstream order): drone, AirTag, tracker net, Penguin
    bool drone = false, decoded = false;
    const char* net = ble_tracker_network(a);
    if (ble_decode_remote_id(a, s_bleUas)) {
        drone = true; decoded = odid_useful(s_bleUas);
        confidence += W_DRONE;
        if (W_DRONE > best) { best = W_DRONE; rule = "Remote ID Drone"; category = "Drone"; }
    } else if (ble_is_airtag(a)) {
        confidence += W_TRACKER;
        if (W_TRACKER > best) { best = W_TRACKER; rule = "Apple Find My Tracker"; category = "Tracker"; }
    } else if (net) {
        confidence += W_TRACKER;
        if (W_TRACKER > best) { best = W_TRACKER; rule = net; category = "Tracker"; }
    } else if (ble_penguin(a)) {
        confidence += W_PENGUIN;
        if (W_PENGUIN > best) { best = W_PENGUIN; rule = "Flock Battery (Penguin)"; category = "Flock Safety"; }
    }

    if (!rule) return;
    g_stats.ble_hit++;
    ThreatType t = threat_from_category(category);
    if (t == ThreatType::UNKNOWN || !type_enabled(t, g_cfg)) { g_stats.ble_drop++; return; }

    Detection d{};
    d.type         = t;
    d.timestamp_ms = millis();
    d.rssi         = a.rssi;
    d.channel      = 0;   // 0 = BLE (adv channel not reported by NimBLE)
    d.proto        = PROTO_BLE;
    memcpy(d.mac, a.mac, 6);
    snprintf(d.name, sizeof(d.name), "%s", rule);
    capture_fields(a, d);
    if (drone && decoded) odid_apply(d, s_bleUas);

    post_detection(d, confidence, best);
}
