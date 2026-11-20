// SPDX-License-Identifier: GPL-3.0-or-later
// ui_model.cpp — row builders for the detail screens. Moved unchanged out of src/ui.cpp (Phase 4) so the CYD and
// C6 UIs share them. Colours are the CYD palette's RGB565 values (TFT_eSPI constants written out, so this file needs no display library).
#include <Arduino.h>
#include <stdarg.h>
#include <math.h>
#include "ui_model.h"
#include "vendor.h"
#include "settings.h"

#define C_WHITE   0xFFFF
#define C_SIGNAL  0x07E0   // TFT_GREEN
#define C_ACCENT  0x07FF   // TFT_CYAN
#define C_ATTACK  0xFD20
#define C_MDGREY  0x7BCF
#define C_TRACKER 0xFDA0   // TFT_ORANGE (unused here, kept with the palette)

static const char* TNAME[] = {"Drone","Tracker","Camera","BodyCam","Attack","?"};

DRow s_rows[48];
int  s_nrows = 0;

static char* addrow(const char* k) {
    if (s_nrows >= (int)(sizeof(s_rows)/sizeof(s_rows[0]))) return nullptr;
    s_rows[s_nrows].k = k;
    s_rows[s_nrows].v[0] = 0;
    return s_rows[s_nrows++].v;
}
#define ROW(k, ...) do { char* _v = addrow(k); if (_v) snprintf(_v, 40, __VA_ARGS__); } while (0)

static void fmt_ago(char* out, size_t n, uint32_t ms) {
    uint32_t s = ms / 1000;
    if (s < 60)        snprintf(out, n, "%us ago", (unsigned)s);
    else if (s < 3600) snprintf(out, n, "%um %us ago", (unsigned)(s/60), (unsigned)(s%60));
    else               snprintf(out, n, "%uh %um ago", (unsigned)(s/3600), (unsigned)((s/60)%60));
}

static const char* addr_type_str(uint8_t t) {
    switch (t) {
        case ADDR_BLE_PUBLIC:        return "BLE public (IEEE)";
        case ADDR_BLE_RANDOM_STATIC: return "BLE random static";
        case ADDR_BLE_RPA:           return "BLE private (rotating)";
        case ADDR_BLE_NRPA:          return "BLE random (non-resolvable)";
        case ADDR_WIFI_UNIVERSAL:    return "WiFi universal (IEEE)";
        case ADDR_WIFI_LOCAL:        return "WiFi local / randomized";
        default:                     return "Unknown";
    }
}

static const char* wifi_sec_str(uint8_t s) {
    switch (s) {
        case SEC_OPEN:      return "Open (no encryption)";
        case SEC_WEP:       return "WEP";
        case SEC_WPA:       return "WPA (TKIP)";
        case SEC_WPA2:      return "WPA2-Personal";
        case SEC_WPA2_WPA3: return "WPA2/WPA3 transition";
        case SEC_WPA3:      return "WPA3-Personal (SAE)";
        case SEC_WPA2_ENT:  return "WPA2-Enterprise";
        case SEC_WPA3_ENT:  return "WPA3-Enterprise 192";
        case SEC_OWE:       return "Enhanced Open (OWE)";
        default:            return nullptr;
    }
}

void build_rows(const Detection& d, uint32_t now) {
    s_nrows = 0;
    char mac[20]; d.mac_str(mac);
    char t[40];

    // ── Identity
    ROW("Type", "%s", TNAME[(int)d.type]);
    ROW("Confidence", "%d%% %s%s", d.confidence,
        d.confidence>=75?"Confirmed":(d.confidence>=45?"Likely":"Possible"),
        d.corroborated?" (+BLE/WiFi)":"");
    ROW("MAC", "%s", mac);
    ROW("Address", "%s", addr_type_str(d.addr_type));
    if (d.addr_type == ADDR_BLE_PUBLIC || d.addr_type == ADDR_WIFI_UNIVERSAL) {
        const char* v = vendor_for_mac(d.mac);
        if (v) ROW("Vendor", "%s", v);
        else   ROW("Vendor", "%s", vendor_db_full() ? "Not in IEEE registry" : "Unknown (run vendor tool)");
    } else {
        ROW("Vendor", "n/a (random address)");
    }
    if (d.company >= 0) {
        const char* c = vendor_for_company((uint16_t)d.company);
        if (c) ROW("BT company", "%s (0x%04X)", c, (unsigned)d.company);
        else   ROW("BT company", "0x%04X", (unsigned)d.company);
    }
    if (d.dev_name[0])   ROW("Device name", "%s", d.dev_name);
    if (d.ssid[0])       ROW("SSID", "%s", d.ssid);
    if (d.hidden_ssid)   ROW("SSID", "(hidden network)");
    if (d.probe_ssid[0]) ROW("Probing for", "%s", d.probe_ssid);
    if (d.apple_type != 0xFF) ROW("Apple", "%s", apple_role(d.apple_type, d.apple_info));
    if (d.has_appearance) {
        const char* ap = ble_appearance(d.appearance);
        if (ap) ROW("Appearance", "%s", ap); else ROW("Appearance", "0x%04X", d.appearance);
    }

    // ── Signal
    ROW("RSSI", "%d dBm  (%d .. %d)", d.rssi, d.rssi_min, d.rssi_max);
    if (d.tx_power != TXP_NONE) {
        ROW("TX power", "%+d dBm", d.tx_power);
        // Log-distance model: RSSI@1m ~ TX - 41 dB (2.4 GHz free space), n = 2.5
        float dist = powf(10.0f, ((d.tx_power - 41) - d.rssi) / 25.0f);
        if (dist < 1.0f) ROW("Est. distance", "< 1 m (rough)");
        else             ROW("Est. distance", "~%.0f m (rough)", dist);
    }
    if (d.channel >= 1 && d.channel <= 14)
        ROW("Channel", "WiFi ch %d (%d MHz)", d.channel, d.channel == 14 ? 2484 : 2407 + 5 * d.channel);
    else
        ROW("Channel", "BLE");
    ROW("Heard via", "%s", d.proto == (PROTO_BLE|PROTO_WIFI) ? "BLE + WiFi" :
                           d.proto == PROTO_WIFI ? "WiFi" : "BLE");

    // ── Timing
    fmt_ago(t, sizeof(t), now - d.first_seen_ms); ROW("First seen", "%s", t);
    fmt_ago(t, sizeof(t), now - d.timestamp_ms);  ROW("Last seen", "%s", t);
    ROW("Times heard", "%u", (unsigned)d.seen_count);

    // ── BLE details
    if (d.proto & PROTO_BLE) {
        ROW("Connectable", "%s", d.connectable ? "Yes" : "No (broadcast only)");
        for (int i = 0; i < d.svc_count; i++) {
            const char* n = name_for_uuid16(d.svc[i]);
            if (n) ROW("Service", "0x%04X %s", d.svc[i], n); else ROW("Service", "0x%04X", d.svc[i]);
        }
        if (d.svc_128) ROW("Service", "+ custom 128-bit UUID");
    }

    // ── WiFi details
    if (d.proto & PROTO_WIFI) {
        const char* fr = d.wifi_frame == WF_BEACON ? "Beacon (access point)" :
                         d.wifi_frame == WF_PROBE_RESP ? "Probe response (AP)" :
                         d.wifi_frame == WF_PROBE_REQ ? "Probe request (client)" :
                         d.wifi_frame == WF_NAN ? "NAN action (Remote ID)" : nullptr;
        if (fr) ROW("Frame", "%s", fr);
        const char* sec = wifi_sec_str(d.wifi_sec);
        if (sec) ROW("Security", "%s", sec);
        if (d.wifi_frame == WF_BEACON || d.wifi_frame == WF_PROBE_RESP || d.wifi_std) {
            const char* st = (d.wifi_std & WSTD_BE) ? "802.11be (WiFi 7)" :
                             (d.wifi_std & WSTD_AX) ? "802.11ax (WiFi 6)" :
                             (d.wifi_std & WSTD_AC) ? "802.11ac (WiFi 5)" :
                             (d.wifi_std & WSTD_N)  ? "802.11n (WiFi 4)" : "802.11b/g";
            ROW("Standard", "%s", st);
        }
        if (d.country[0]) ROW("Country", "%s", d.country);
        if (d.beacon_int) ROW("Beacon int.", "%u TU (%u ms)", d.beacon_int,
                              (unsigned)((d.beacon_int * 1024UL) / 1000));
    }

    // ── Drone (ASTM F3411 Remote ID)
    if (d.has_remote_id) {
        const RemoteID& r = d.remote_id;
        if (r.uas_id[0])      ROW("UAS ID", "%s", r.uas_id);
        if (r.uas_id[0])      ROW("UA type", "%s", odid_ua_type(r.ua_type));
        if (r.operator_id[0]) ROW("Operator ID", "%s", r.operator_id);
        if (r.loc_valid) {
            if (r.lat != 0 || r.lon != 0) ROW("Drone pos", "%.5f, %.5f", r.lat, r.lon);
            if (r.height_agl > -1000) ROW("Height AGL", "%.0f m", r.height_agl);
            if (r.alt_geo > -1000)    ROW("Altitude", "%.0f m (WGS84)", r.alt_geo);
            if (r.speed_ms < 254)     ROW("Speed", "%.1f m/s (%.0f km/h)", r.speed_ms, r.speed_ms * 3.6f);
            if (r.vspeed_ms < 62)     ROW("Vert. speed", "%+.1f m/s", r.vspeed_ms);
            if (r.heading < 360)      ROW("Heading", "%.0f deg", r.heading);
        }
        if (r.op_valid) ROW("Pilot pos", "%.5f, %.5f", r.op_lat, r.op_lon);
    }

    ROW("Muted", "%s", d.muted ? "Yes" : "No");
}


// Append WPS / chipset / timing identity gathered by focus mode
void add_focus_rows(const Detection& d) {
    static FocusProfile f;   // large now (peer table): keep it off the task stack
    if (!focus_copy(f) || !f.active || memcmp(f.mac, d.mac, 6) != 0) {
        ROW("Focus", "engaging radio...");
        return;
    }
    if (f.radio == FOCUS_WIFI) {
        if (f.has_wps) {
            if (f.wps_manuf[0])     ROW("WPS maker", "%s", f.wps_manuf);
            if (f.wps_model[0])     ROW("WPS model", "%s", f.wps_model);
            if (f.wps_model_num[0]) ROW("WPS model #", "%s", f.wps_model_num);
            if (f.wps_devname[0])   ROW("WPS name", "%s", f.wps_devname);
            if (f.wps_serial[0])    ROW("WPS serial", "%s", f.wps_serial);
            if (f.wps_state)        ROW("WPS state", "%s", f.wps_state==2?"configured":"unconfigured");
        }
        if (f.ht_streams)   ROW("Spatial streams", "%dx%d", f.ht_streams, f.ht_streams);
        if (f.vie_count) {
            // Name a couple of well-known vendor-IE chipset OUIs
            for (int i = 0; i < f.vie_count; i++) {
                const uint8_t* o = f.vie[i];
                const char* who = nullptr;
                if (o[0]==0x00&&o[1]==0x50&&o[2]==0xF2) who = "Microsoft/WPA";
                else if (o[0]==0x00&&o[1]==0x17&&o[2]==0xF2) who = "Apple";
                else if (o[0]==0x00&&o[1]==0x10&&o[2]==0x18) who = "Broadcom";
                else if (o[0]==0x00&&o[1]==0x03&&o[2]==0x7F) who = "Atheros/Qualcomm";
                else if (o[0]==0x50&&o[1]==0x6F&&o[2]==0x9A) who = "Wi-Fi Alliance";
                if (who) { ROW("Vendor IE", "%s", who); }
            }
        }
        ROW("Frames (focus)", "%u b=%u pr=%u", (unsigned)f.frames,
            (unsigned)f.beacons, (unsigned)f.probe_resps);
    } else {
        ROW("Adverts (focus)", "%u", (unsigned)f.adv_count);
        if (f.adv_int_min) ROW("Advert interval", "~%u ms", (unsigned)f.adv_int_min);
    }
}


LRow s_lrows[48];
int  s_nl = 0;

static LRow* lrow(uint8_t kind, uint16_t color, const char* fmt, ...) {
    if (s_nl >= (int)(sizeof(s_lrows)/sizeof(s_lrows[0]))) return nullptr;
    LRow& r = s_lrows[s_nl++];
    memset(&r, 0, sizeof(r));
    r.kind = kind; r.color = color; r.rssi = -100;
    va_list ap; va_start(ap, fmt);
    vsnprintf(r.text, sizeof(r.text), fmt, ap);
    va_end(ap);
    return &r;
}

static void lrow_peer(uint16_t color, uint8_t radio, uint8_t ch, int rssi,
                      const uint8_t mac[6], const char* tag, const char* extra) {
    char m[20];
    snprintf(m, sizeof(m), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    LRow* r = lrow(LR_PEER, color, "%s %s %s", tag, m, extra);
    if (!r) return;
    r->radio = radio; r->channel = ch; r->rssi = (int8_t)constrain(rssi, -127, 0);
    memcpy(r->mac, mac, 6);
}

static void fmt_bytes(char* out, size_t n, uint32_t b) {
    if (b < 1000)         snprintf(out, n, "%uB", (unsigned)b);
    else if (b < 1000000) snprintf(out, n, "%uK", (unsigned)(b / 1000));
    else                  snprintf(out, n, "%.1fM", b / 1e6f);
}

// Same device family: octets 0-4 equal (ignoring the locally-administered bit),
// last octet close — how multi-radio / multi-SSID gear allocates base MACs.
static bool same_block(const uint8_t* a, const uint8_t* b) {
    if ((a[0] | 0x02) != (b[0] | 0x02)) return false;
    for (int i = 1; i < 5; i++) if (a[i] != b[i]) return false;
    return abs((int)a[5] - (int)b[5]) <= 16;
}

static const char* short_name(const Detection& e) {
    if (e.ssid[0]) return e.ssid;
    if (e.dev_name[0]) return e.dev_name;
    return e.name;
}

void build_links(const Detection& d, const DetectionRegistry& reg,
                        const FocusProfile& f, bool have) {
    s_nl = 0;
    bool wifi = have && f.radio == FOCUS_WIFI;

    // ── Observed: seen directly on the air
    lrow(LR_HEAD, C_SIGNAL, "OBSERVED");
    bool any = false;
    if ((d.proto & (PROTO_BLE|PROTO_WIFI)) == (PROTO_BLE|PROTO_WIFI)) {
        lrow(LR_INFO, C_WHITE, "Same MAC heard on BLE and WiFi"); any = true;
    }
    if (d.probe_ssid[0]) { lrow(LR_INFO, C_WHITE, "Probes for: %s", d.probe_ssid); any = true; }
    if (wifi) {
        for (int i = 0; i < f.peer_count; i++) {
            const FocusPeer& p = f.peers[i];
            if (p.role == PEER_UNKNOWN) continue;
            char v[24] = "";
            const char* ven = vendor_for_mac(p.mac);
            if (ven && !(p.mac[0] & 0x02)) snprintf(v, sizeof(v), "%s", ven);
            lrow_peer(C_ACCENT, FOCUS_WIFI, f.channel, p.rssi, p.mac,
                      p.role == PEER_AP ? "AP" : "STA", v);
            any = true;
        }
        if (!any) lrow(LR_INFO, C_MDGREY, "No AP/client links seen yet");
    } else if (have) {
        if (!any) lrow(LR_INFO, C_MDGREY, "Nothing observable yet");
        lrow(LR_INFO, C_MDGREY, "BLE pairings/connections hop channels");
        lrow(LR_INFO, C_MDGREY, "this radio can't follow: not visible");
    } else {
        lrow(LR_INFO, C_MDGREY, "Collecting from target...");
    }

    // ── Inferred: circumstantial, from other detections in the registry
    lrow(LR_HEAD, C_ATTACK, "INFERRED");
    bool inf = false;
    for (int i = 0; i < reg.count; i++) {
        const Detection& e = reg.items[i];
        if (memcmp(e.mac, d.mac, 6) == 0) continue;
        bool sib = same_block(e.mac, d.mac);
        bool probes = d.ssid[0] && e.probe_ssid[0] && strcmp(e.probe_ssid, d.ssid) == 0;
        if (!sib && !probes) continue;
        lrow_peer(C_ATTACK, (e.proto & PROTO_WIFI) ? FOCUS_WIFI : FOCUS_BLE,
                  (uint8_t)e.channel, e.rssi, e.mac, sib ? "SIB" : "PRB", short_name(e));
        inf = true;
    }
    if (!inf) lrow(LR_INFO, C_MDGREY, "Nothing inferred yet");

    // ── Correlated: co-travel over time (needs history on the SD card)
    lrow(LR_HEAD, C_MDGREY, "CORRELATED");
    lrow(LR_INFO, C_MDGREY, "Needs session history on SD (planned)");
}

void build_traffic(const FocusProfile& f, bool have) {
    s_nl = 0;
    if (!have) { lrow(LR_INFO, C_MDGREY, "Collecting from target..."); return; }
    if (f.radio != FOCUS_WIFI) {
        lrow(LR_INFO, C_MDGREY, "BLE target: adverts only. Connections");
        lrow(LR_INFO, C_MDGREY, "hop channels and can't be followed.");
        lrow(LR_INFO, C_WHITE,  "Adverts heard: %u", (unsigned)f.adv_count);
        return;
    }
    lrow(LR_HEAD, C_SIGNAL, "data %u  mgmt %u  peers %u",
         (unsigned)f.data_frames, (unsigned)f.frames, f.peer_count);
    // Peers by total frames, busiest first (indices only; table is a copy)
    uint8_t order[FOCUS_MAX_PEERS];
    for (int i = 0; i < f.peer_count; i++) order[i] = i;
    for (int i = 1; i < f.peer_count; i++) {          // insertion sort
        uint8_t k = order[i]; int j = i - 1;
        auto tot = [&](uint8_t x) { return f.peers[x].to_frames + f.peers[x].from_frames; };
        while (j >= 0 && tot(order[j]) < tot(k)) { order[j+1] = order[j]; j--; }
        order[j+1] = k;
    }
    for (int n = 0; n < f.peer_count; n++) {
        const FocusPeer& p = f.peers[order[n]];
        char b[10], x[40];
        fmt_bytes(b, sizeof(b), p.to_bytes + p.from_bytes);
        snprintf(x, sizeof(x), ">%u <%u %s%s", (unsigned)p.to_frames, (unsigned)p.from_frames, b,
                 p.qos_frames ? " Q" : "");
        char r[8] = "";
        if (p.from_frames) snprintf(r, sizeof(r), " %d", p.rssi);
        char e[48]; snprintf(e, sizeof(e), "%s%s", x, r);
        char m[20];
        snprintf(m, sizeof(m), "%02X:%02X:%02X:%02X:%02X:%02X",
                 p.mac[0], p.mac[1], p.mac[2], p.mac[3], p.mac[4], p.mac[5]);
        LRow* row = lrow(LR_PEER, C_WHITE, "%s %s", m, e);
        if (!row) break;
        row->radio = FOCUS_WIFI; row->channel = f.channel;
        row->rssi = p.rssi; memcpy(row->mac, p.mac, 6);
    }
    if (f.peer_count == 0) {
        lrow(LR_INFO, C_MDGREY, "No peers yet. Needs a busy target:");
        lrow(LR_INFO, C_MDGREY, "an AP with clients, or an active client.");
    }
}

