// SPDX-License-Identifier: GPL-3.0-or-later
// SignalSweep (C6) UI — 320x172 landscape, capacitive touch, swipe navigation (no tab bar).
// Decisions: docs/port/UI_CUTS.md. Pages: HOME (activity ring + radio bars), LIST, STATUS, SETTINGS; tapping a list row
// opens the detail view (INFO, LINKS, TRAFF, HUNT), which swipes the same way and has a BACK corner. Touch targets are
// at least ~50 px. Rows are composed in a 320x50 sprite and pushed (no flicker, 32 KB); the HOME/HUNT ring uses a 112x112 sprite.
// The row builders (INFO / LINKS / TRAFF) are shared with the CYD UI through ui_model.h.

#include <Arduino.h>
#include <math.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <esp_heap_caps.h>
#include "hal/board.h"
#include "hal/display.h"
#include "hal/touch.h"
#include "hal/touch_events.h"
#include "hal/alert_output.h"
#include "detection.h"
#include "settings.h"
#include "focus.h"
#include "ui_model.h"
#include "ui.h"
#include "scan_c6/scan_c6.h"
#include "hal/power.h"
#include "sd_log/sd_store.h"

#define UI_VERSION "v3.21-c6d"

extern DetectionRegistry g_reg;
extern SemaphoreHandle_t g_reg_mutex;

// ── Layout ────────────────────────────────────────────────────────────────────
#define HDR_H    24
#define CONT_Y   HDR_H
#define CONT_H   (SCREEN_H - HDR_H)     // 148
#define LIST_RH  50                     // list / peer row height (touch target)
#define INFO_RH  30
#define SWIPE_MIN_DX 60                 // px of horizontal travel for a page swipe
#define TAP_SLOP     14                 // px a tap may wander
#define DRAG_START   10                 // px before a vertical drag starts scrolling

// ── Palette (RGB565) ──────────────────────────────────────────────────────────
static const uint16_t BLACK = 0x0000, WHITE = 0xFFFF, PANEL = 0x1082, PANEL2 = 0x0841, BORDER = 0x4208, GRID = 0x2104,
                      LTGREY = 0xD69A, MDGREY = 0x7BCF, SIGNAL = 0x07E0, ACCENT = 0x07FF, RED = 0xF800, ORANGE = 0xFDA0;
static const uint16_t TCOL[] = {0x3DFF, 0xFDA0, 0xF800, 0xF81F, 0xFD20, 0x4208};   // drone azure, tracker, camera, bodycam, attack, ?
static const char* TNAME[] = {"Drone", "Tracker", "Camera", "BodyCam", "Attack", "?"};

enum Page { P_HOME = 0, P_LIST, P_STATUS, P_SET, P_COUNT };
static const char* PAGE_NAME[P_COUNT] = {"HOME", "DETECTIONS", "STATUS", "SETTINGS"};
enum DPage { DP_INFO = 0, DP_LINKS, DP_TRAFF, DP_HUNT, DP_COUNT };
static const char* DP_NAME[DP_COUNT] = {"INFO", "LINKS", "TRAFFIC", "HUNT"};

static Display             tft;
static lgfx::LGFX_Sprite   rowspr(&tft);
static lgfx::LGFX_Sprite   ringspr(&tft);
static bool                s_have_sprites = false;

static int      s_page = P_HOME;
static bool     s_detail = false;
static int      s_dpage = DP_INFO;
static int      s_scroll = 0;        // LIST scroll, px
static int      s_dscroll = 0;       // detail scroll, px
static bool     s_force = true;      // redraw the content area from scratch
static uint32_t s_page_req_ms = 0;   // set by go_page, cleared when the redraw has finished (latency log)
static bool     s_hdr_dirty = true;
static int      s_last_count = -1;
static Settings* s_cfg = nullptr;

// detail target: tracked by MAC so pruning cannot swap the device shown
static uint8_t   s_dmac[6];
static Detection s_dlast;
static bool      s_dgone = false;

// alert toast (shown in the header)
static char      s_toast[40] = "";
static uint16_t  s_toast_col = RED;
static uint32_t  s_toast_until = 0, s_toast_start = 0;
static uint32_t  s_flash_t0 = 0;       // full-content colour flash on alert: two short flashes
static uint16_t  s_flash_col = RED;
static bool      s_flash_on = false;
static char      s_last_alert[32] = "";
static uint16_t  s_last_alert_col = MDGREY;

// ── Small helpers ─────────────────────────────────────────────────────────────
static void txt(lgfx::LGFXBase& g, const char* s, int x, int y, int font, uint16_t fg, uint16_t bg,
                lgfx::textdatum_t datum = lgfx::top_left) {
    g.setTextFont(font);
    g.setTextDatum(datum);
    g.setTextColor(fg, bg);
    g.drawString(s, x, y);
}

static void fit(lgfx::LGFXBase& g, char* s, int font, int maxw) {
    g.setTextFont(font);
    int n = strlen(s);
    while (n > 3 && g.textWidth(s) > maxw) { s[--n] = 0; s[n - 1] = '.'; s[n - 2] = '.'; }
}

static const Detection* find_mac(const DetectionRegistry& r, const uint8_t* mac) {
    for (int i = 0; i < r.count; i++) if (memcmp(r.items[i].mac, mac, 6) == 0) return &r.items[i];
    return nullptr;
}

static void purge_type(ThreatType t) {   // disabling a type removes its entries now (as the CYD UI does)
    if (xSemaphoreTake(g_reg_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return;
    for (int j = 0; j < g_reg.count; ) {
        if (g_reg.items[j].type == t) {
            for (int k = j; k < g_reg.count - 1; k++) {
                g_reg.items[k] = g_reg.items[k + 1];
                memcpy(g_reg.history[k], g_reg.history[k + 1], RSSI_HISTORY);
            }
            g_reg.count--;
        } else j++;
    }
    xSemaphoreGive(g_reg_mutex);
}

static void signal_bars(lgfx::LGFXBase& g, int x, int y, int bars, uint16_t col) {   // 5 bars, 5 px apart, up to 18 px tall
    for (int i = 0; i < 5; i++) {
        int h = 4 + i * 3;
        g.fillRect(x + i * 6, y + 18 - h, 4, h, i < bars ? col : GRID);
    }
}

// ── Header ────────────────────────────────────────────────────────────────────
static void dots(int cx, int y, int n, int active) {
    int w = (n - 1) * 14, x0 = cx - w / 2;
    for (int i = 0; i < n; i++) {
        if (i == active) tft.fillCircle(x0 + i * 14, y, 4, ACCENT);
        else             tft.fillCircle(x0 + i * 14, y, 3, BORDER);
    }
}

static void draw_header(const DetectionRegistry& reg) {
    uint32_t now = millis();
    bool toast = now < s_toast_until;
    if (toast) {
        bool on = (now - s_toast_start) > 1400 || ((now - s_toast_start) / 220) % 2 == 0;   // blink for the first 1.4 s
        uint16_t bg = on ? s_toast_col : BLACK;
        tft.fillRect(0, 0, SCREEN_W, HDR_H, bg);
        txt(tft, s_toast, SCREEN_W / 2, HDR_H / 2, 2, on ? WHITE : s_toast_col, bg, lgfx::middle_center);
        return;
    }
    tft.fillRect(0, 0, SCREEN_W, HDR_H, BLACK);
    tft.drawFastHLine(0, HDR_H - 1, SCREEN_W, ACCENT);
    if (s_detail) {
        uint16_t tc = TCOL[(int)s_dlast.type];
        txt(tft, "< BACK", 6, HDR_H / 2, 2, ACCENT, BLACK, lgfx::middle_left);
        char nm[32];
        snprintf(nm, sizeof(nm), "%s", s_dlast.ssid[0] ? s_dlast.ssid : s_dlast.dev_name[0] ? s_dlast.dev_name : s_dlast.name);
        fit(tft, nm, 2, 130);
        txt(tft, nm, 78, HDR_H / 2, 2, tc, BLACK, lgfx::middle_left);
        txt(tft, DP_NAME[s_dpage], 262, HDR_H / 2, 2, LTGREY, BLACK, lgfx::middle_right);
        dots(296, HDR_H / 2, DP_COUNT, s_dpage);
        // 4 dots, 14 px apart, end at 317
        return;
    }
    txt(tft, PAGE_NAME[s_page], 8, HDR_H / 2, 2, ACCENT, BLACK, lgfx::middle_left);
    dots(150, HDR_H / 2, P_COUNT, s_page);
    int mv = power_battery_mv();                      // battery: percent on a straight 3.3 V to 4.2 V line (rough)
    if (mv > 0) {
        int pct = constrain((mv - 3300) * 100 / 900, 0, 100);
        char bt[8]; snprintf(bt, sizeof(bt), "%d%%", pct);
        txt(tft, bt, 226, HDR_H / 2, 2, pct < 20 ? RED : pct < 40 ? ORANGE : SIGNAL, BLACK, lgfx::middle_right);
    } else {
        txt(tft, "--", 226, HDR_H / 2, 2, MDGREY, BLACK, lgfx::middle_right);
    }
    char b[16];
    snprintf(b, sizeof(b), "%d ALERTS", reg.count);
    uint16_t pc = reg.count > 0 ? RED : BORDER;
    tft.fillRoundRect(232, 3, 84, HDR_H - 7, 8, pc);
    txt(tft, b, 274, HDR_H / 2 - 1, 2, WHITE, pc, lgfx::middle_center);
}

// ── Detail state ──────────────────────────────────────────────────────────────
static void hunt_reset();
static void hunt_off();

static void open_detail(const Detection& d) {
    memcpy(s_dmac, d.mac, 6);
    s_dlast = d; s_dgone = false;
    s_dscroll = 0; s_dpage = DP_INFO; s_detail = true;
    if (d.proto & PROTO_WIFI) focus_start(d.mac, FOCUS_WIFI, d.channel >= 1 && d.channel <= 13 ? d.channel : 0);
    else                      focus_start(d.mac, FOCUS_BLE, 0);   // stop sweeping, work this one target
    hunt_reset();
    s_force = true; s_hdr_dirty = true;
}

static void close_detail() {
    hunt_off();
    focus_stop();                      // resume sweeping
    s_detail = false;
    s_force = true; s_hdr_dirty = true;
}

// Focus the device behind a tapped LINKS/TRAFF row. Unknown WiFi peers are added to the registry as low-confidence
// UNKNOWN entries so INFO / HUNT work on them (same behaviour as the CYD's pivot_to).
static void pivot_to(const LRow& r) {
    Detection d{};
    bool found = false;
    if (xSemaphoreTake(g_reg_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return;
    for (int i = 0; i < g_reg.count; i++)
        if (memcmp(g_reg.items[i].mac, r.mac, 6) == 0) { d = g_reg.items[i]; found = true; break; }
    if (!found) {
        d.type = ThreatType::UNKNOWN;
        snprintf(d.name, sizeof(d.name), "Peer (pivot)");
        memcpy(d.mac, r.mac, 6);
        d.addr_type = (r.mac[0] & 0x02) ? ADDR_WIFI_LOCAL : ADDR_WIFI_UNIVERSAL;
        d.proto = PROTO_WIFI;
        d.channel = r.channel;
        d.rssi = r.rssi;
        d.timestamp_ms = millis();
        d.wifi_frame = WF_NONE;
        int idx = g_reg.upsert(d);
        if (idx < 0) { xSemaphoreGive(g_reg_mutex); Serial.println("[pivot] registry full"); return; }
        d = g_reg.items[idx];
    }
    xSemaphoreGive(g_reg_mutex);
    hunt_off();
    memcpy(s_dmac, d.mac, 6);
    s_dlast = d; s_dgone = false; s_dscroll = 0; s_dpage = DP_INFO;
    if (d.proto & PROTO_WIFI)
        focus_start(d.mac, FOCUS_WIFI, d.channel >= 1 && d.channel <= 13 ? d.channel : r.channel);
    else
        focus_start(d.mac, FOCUS_BLE, 0);
    hunt_reset();
    s_force = true; s_hdr_dirty = true;
    Serial.printf("[pivot] -> %02X:%02X:%02X:%02X:%02X:%02X %s\n", d.mac[0], d.mac[1], d.mac[2], d.mac[3], d.mac[4],
                  d.mac[5], found ? "(known)" : "(added as peer)");
}

// ── Scrolling row lists (LIST, INFO, LINKS, TRAFF) ───────────────────────────
// A list is `n` rows, each `rh(i)` tall, scrolled by `scroll` px; draw(i) renders row i into rowspr.
struct RowList {
    int  n;
    int  (*rh)(int);
    void (*draw)(int, const DetectionRegistry&);
};

static int rows_total_h(const RowList& L) { int h = 0; for (int i = 0; i < L.n; i++) h += L.rh(i); return h; }

static int rows_max_scroll(const RowList& L) { int m = rows_total_h(L) - CONT_H; return m > 0 ? m : 0; }

static void rows_paint(const RowList& L, int scroll, const DetectionRegistry& reg) {
    tft.setClipRect(0, CONT_Y, SCREEN_W, CONT_H);
    int y = -scroll;
    for (int i = 0; i < L.n; i++) {
        int h = L.rh(i);
        if (y + h > 0 && y < CONT_H) {
            L.draw(i, reg);
            rowspr.pushSprite(&tft, 0, CONT_Y + y);
        }
        y += h;
        if (y >= CONT_H) break;
    }
    if (y < CONT_H) tft.fillRect(0, CONT_Y + (y < 0 ? 0 : y), SCREEN_W, CONT_H - (y < 0 ? 0 : y), BLACK);
    // scroll indicator
    int total = rows_total_h(L);
    if (total > CONT_H) {
        int th = max(14, CONT_H * CONT_H / total);
        int ty = CONT_Y + (scroll * (CONT_H - th)) / max(1, total - CONT_H);
        tft.fillRect(SCREEN_W - 3, ty, 3, th, ACCENT);
    }
    tft.clearClipRect();
}

// the row hit by a tap at screen y (returns -1 when none) and that row's top in content coordinates
static int rows_hit(const RowList& L, int scroll, int ty) {
    int y = -scroll, cy = ty - CONT_Y;
    for (int i = 0; i < L.n; i++) {
        int h = L.rh(i);
        if (cy >= y && cy < y + h) return i;
        y += h;
    }
    return -1;
}

// ── LIST page ─────────────────────────────────────────────────────────────────
static int  list_rh(int) { return LIST_RH; }
static void list_draw(int i, const DetectionRegistry& reg) {
    const Detection& d = reg.items[i];
    uint16_t tc = TCOL[(int)d.type], bg = (i & 1) ? PANEL2 : PANEL;
    rowspr.fillSprite(bg);
    rowspr.fillRect(0, 0, 8, LIST_RH, tc);
    char nm[40], mac[20], ln[48];
    snprintf(nm, sizeof(nm), "%s", d.name);
    fit(rowspr, nm, 2, 196);
    txt(rowspr, nm, 16, 6, 2, WHITE, bg);
    d.mac_str(mac);
    uint32_t age = (millis() - d.timestamp_ms) / 1000;
    if (d.channel) snprintf(ln, sizeof(ln), "%s  ch%d  %us", mac, d.channel, (unsigned)age);
    else           snprintf(ln, sizeof(ln), "%s  BLE  %us", mac, (unsigned)age);
    txt(rowspr, ln, 16, 28, 2, MDGREY, bg);
    char r[10];
    snprintf(r, sizeof(r), "%d", d.rssi);
    txt(rowspr, r, 312, 6, 4, d.alerted ? RED : SIGNAL, bg, lgfx::top_right);
    signal_bars(rowspr, 266, 28, d.rssi_bars(), tc);
}

static RowList list_rows(const DetectionRegistry& reg) { return RowList{reg.count, list_rh, list_draw}; }

// ── Detail rows: INFO / LINKS / TRAFF ────────────────────────────────────────
static int info_rh(int) { return INFO_RH; }
static void info_draw(int i, const DetectionRegistry&) {
    uint16_t bg = (i & 1) ? PANEL2 : PANEL;
    rowspr.fillSprite(bg);
    txt(rowspr, s_rows[i].k, 6, INFO_RH / 2, 2, MDGREY, bg, lgfx::middle_left);
    char v[40];
    snprintf(v, sizeof(v), "%s", s_rows[i].v);
    fit(rowspr, v, 2, SCREEN_W - 118 - 8);
    txt(rowspr, v, 112, INFO_RH / 2, 2, WHITE, bg, lgfx::middle_left);
}

static int link_rh(int i) { return s_lrows[i].kind == LR_PEER ? LIST_RH : s_lrows[i].kind == LR_HEAD ? 26 : 28; }
static void link_draw(int i, const DetectionRegistry&) {
    const LRow& r = s_lrows[i];
    uint16_t bg = r.kind == LR_HEAD ? BLACK : ((i & 1) ? PANEL2 : PANEL);
    int h = link_rh(i);
    rowspr.fillSprite(bg);
    if (r.kind == LR_PEER) {
        // "TAG AA:BB:CC:DD:EE:FF extra"  or  "AA:BB:CC:DD:EE:FF extra": MAC on line one, the rest on line two
        char l1[48], l2[48] = "";
        snprintf(l1, sizeof(l1), "%s", r.text);
        char* sp = strchr(l1, ' ');
        if (sp && (sp - l1) <= 3) sp = strchr(sp + 1, ' ');   // short role tag first (AP, STA, SIB, PRB)
        if (sp) { snprintf(l2, sizeof(l2), "%s", sp + 1); *sp = 0; }
        fit(rowspr, l1, 2, 270);
        fit(rowspr, l2, 2, 270);
        txt(rowspr, l1, 8, 6, 2, r.color, bg);
        txt(rowspr, l2, 8, 28, 2, MDGREY, bg);
        rowspr.fillTriangle(SCREEN_W - 18, h / 2 - 9, SCREEN_W - 18, h / 2 + 9, SCREEN_W - 8, h / 2, ACCENT);   // tap to pivot
    } else {
        char t[48];
        snprintf(t, sizeof(t), "%s", r.text);
        fit(rowspr, t, 2, SCREEN_W - 16);
        txt(rowspr, t, r.kind == LR_HEAD ? 6 : 10, h / 2, 2, r.color, bg, lgfx::middle_left);
    }
}

static RowList detail_rows() {
    if (s_dpage == DP_INFO)  return RowList{s_nrows, info_rh, info_draw};
    return RowList{s_nl, link_rh, link_draw};   // LINKS and TRAFF
}

// Rebuild the rows of the current detail page from live data
static void detail_build(const DetectionRegistry& reg) {
    const Detection* live = find_mac(reg, s_dmac);
    s_dgone = (live == nullptr);
    if (live) s_dlast = *live;                 // keep the last copy: shown "aged out" if it disappears
    static FocusProfile f;
    bool have = !s_dgone && focus_copy(f) && f.active && memcmp(f.mac, s_dmac, 6) == 0;
    if (s_dpage == DP_INFO) {
        build_rows(s_dlast, millis());
        if (!s_dgone) add_focus_rows(s_dlast);
        else if (s_nrows < 48) { s_rows[s_nrows].k = "Status"; snprintf(s_rows[s_nrows].v, 40, "aged out of the list"); s_nrows++; }
    } else if (s_dpage == DP_LINKS) {
        build_links(s_dlast, reg, f, have);
    } else if (s_dpage == DP_TRAFF) {
        build_traffic(f, have);
    }
}

// ── HUNT page (visual; sound and LED come with the Phase 5 AlertOutput) ───────
#define HUNT_SILENCE_MS 8000
#define HUNT_RSSI_FLOOR -95
#define HUNT_RSSI_CEIL  -35
static uint32_t s_hunt_last_click = 0, s_hunt_last_pulse = 0;
static bool     s_hunt_pulse = false;
static int      s_hunt_peak = -128, s_hunt_prev = -128;
#define HUNT_HIST 60                       // one sample per 500 ms = 30 s of history
static int8_t   s_hunt_hist[HUNT_HIST];    // 0 = no sample
static uint32_t s_hunt_hist_ms = 0, s_hunt_ts = 0;
static float    s_hunt_ema = 0;            // smoothed RSSI, updated only when a new frame arrives
static bool     s_hunt_dirty = true;

static void hunt_reset() {
    s_hunt_peak = -128; s_hunt_prev = -128; s_hunt_pulse = false;
    memset(s_hunt_hist, 0, sizeof(s_hunt_hist)); s_hunt_hist_ms = 0; s_hunt_ts = 0; s_hunt_ema = 0; s_hunt_dirty = true;
}
static void hunt_off()   { alert_all_off(); s_hunt_pulse = false; }

// RSSI -> click/pulse interval. Closer = faster. -95 dBm ~1.2 s, -35 dBm ~70 ms (CYD values).
static uint32_t hunt_interval(int rssi) {
    float t = constrain((float)(rssi - HUNT_RSSI_FLOOR) / (HUNT_RSSI_CEIL - HUNT_RSSI_FLOOR), 0.0f, 1.0f);
    return (uint32_t)(1200.0f - t * 1130.0f);
}
static uint16_t hunt_heat(int rssi) {   // blue -> cyan -> amber -> red as the target gets closer
    float t = constrain((float)(rssi - HUNT_RSSI_FLOOR) / (HUNT_RSSI_CEIL - HUNT_RSSI_FLOOR), 0.0f, 1.0f);
    if (t < 0.33f) return 0x04FF;
    if (t < 0.66f) return ACCENT;
    if (t < 0.85f) return 0xFD20;
    return RED;
}

static void hunt_draw(bool force) {
    uint32_t now = millis();
    uint32_t age = now - s_dlast.timestamp_ms;
    bool live = !s_dgone && age < HUNT_SILENCE_MS;
    int raw = live ? s_dlast.rssi : -128;
    if (live && s_dlast.timestamp_ms != s_hunt_ts) {          // new frame: fold into the average (alpha 0.4)
        s_hunt_ts = s_dlast.timestamp_ms;
        s_hunt_ema = s_hunt_ema == 0 ? (float)raw : s_hunt_ema + 0.4f * (raw - s_hunt_ema);
    }
    if (!live) s_hunt_ema = 0;
    int rssi = live ? (int)lroundf(s_hunt_ema) : -128;
    if (live && rssi > s_hunt_peak) s_hunt_peak = rssi;
    if (now - s_hunt_hist_ms >= 500) {                        // push a history sample
        s_hunt_hist_ms = now;
        memmove(s_hunt_hist, s_hunt_hist + 1, HUNT_HIST - 1);
        s_hunt_hist[HUNT_HIST - 1] = live ? (int8_t)rssi : 0;
        s_hunt_dirty = true;
    }
    if (live) {
        uint32_t iv = hunt_interval(rssi);
        if (now - s_hunt_last_pulse >= iv) {
            s_hunt_last_pulse = now;
            s_hunt_pulse = !s_hunt_pulse;
            if (s_cfg->led_en) alert_hunt_led(s_hunt_pulse, constrain((float)(rssi - HUNT_RSSI_FLOOR) / (HUNT_RSSI_CEIL - HUNT_RSSI_FLOOR), 0.0f, 1.0f));
            if (s_cfg->speaker_en) { if (s_hunt_pulse) alert_click_start(); else alert_click_stop(); }
        }
    } else {
        alert_all_off(); s_hunt_pulse = false;
    }
    uint16_t heat = live ? hunt_heat(rssi) : PANEL;
    uint16_t fillc = (live && s_hunt_pulse) ? heat : BLACK;
    int c = 56;
    ringspr.fillSprite(BLACK);
    ringspr.fillCircle(c, c, 52, fillc);
    ringspr.drawCircle(c, c, 52, live ? heat : BORDER);
    ringspr.drawCircle(c, c, 53, live ? heat : BORDER);
    char v[12];
    if (live) snprintf(v, sizeof(v), "%d", rssi); else snprintf(v, sizeof(v), "--");
    txt(ringspr, v, c, c - 8, 4, live ? (s_hunt_pulse ? BLACK : WHITE) : MDGREY, fillc, lgfx::middle_center);
    txt(ringspr, "dBm", c, c + 16, 2, live ? (s_hunt_pulse ? BLACK : LTGREY) : MDGREY, fillc, lgfx::middle_center);
    ringspr.pushSprite(&tft, (SCREEN_W - 112) / 2, CONT_Y + 18);

    const char* arrow = "--"; uint16_t ac = MDGREY;
    if (live) {
        if (rssi > s_hunt_prev + 1)      { arrow = "WARMER"; ac = ORANGE; }
        else if (rssi < s_hunt_prev - 1) { arrow = "colder"; ac = ACCENT; }
        else                             { arrow = "steady"; ac = LTGREY; }
        s_hunt_prev = rssi;
    }
    txt(tft, arrow, 8, CONT_Y + 42, 2, ac, BLACK);
    char pk[20]; snprintf(pk, sizeof(pk), "peak %d  ", s_hunt_peak > -128 ? s_hunt_peak : 0);
    txt(tft, pk, 8, CONT_Y + 66, 2, MDGREY, BLACK);
    char ls[24];
    if (s_dgone) snprintf(ls, sizeof(ls), "aged out ");
    else snprintf(ls, sizeof(ls), "%us ago  ", (unsigned)(age / 1000));
    txt(tft, ls, SCREEN_W - 8, CONT_Y + 42, 2, live ? LTGREY : RED, BLACK, lgfx::top_right);
    char rw[20]; snprintf(rw, sizeof(rw), live ? "raw %d  " : "raw --  ", raw);
    txt(tft, rw, SCREEN_W - 8, CONT_Y + 66, 2, MDGREY, BLACK, lgfx::top_right);
    if (s_hunt_dirty || force) {                              // 30 s RSSI history strip under the ring
        s_hunt_dirty = false;
        const int sy = CONT_Y + 132, sh = 14, bw = 5;
        tft.fillRect(0, sy, SCREEN_W, sh + 2, BLACK);
        for (int i = 0; i < HUNT_HIST; i++) {
            int v = s_hunt_hist[i];
            if (!v) { tft.drawFastHLine(8 + i * bw, sy + sh - 1, bw - 1, BORDER); continue; }
            int h = 1 + (int)(constrain((float)(v - HUNT_RSSI_FLOOR) / (HUNT_RSSI_CEIL - HUNT_RSSI_FLOOR), 0.0f, 1.0f) * (sh - 1));
            tft.fillRect(8 + i * bw, sy + sh - h, bw - 1, h, hunt_heat(v));
        }
    }
}

// ── HOME page ─────────────────────────────────────────────────────────────────
static void draw_ring(const DetectionRegistry& reg) {
    int c = 56;
    ringspr.fillSprite(BLACK);
    ringspr.fillArc(c, c, 40, 54, 0, 360, GRID);
    int cnt[5] = {0}, total = 0;
    for (int i = 0; i < reg.count; i++) { int t = (int)reg.items[i].type; if (t >= 0 && t < 5) { cnt[t]++; total++; } }
    float a = 0;
    if (total > 0) {
        for (int t = 0; t < 5; t++) {
            if (!cnt[t]) continue;
            float span = 360.0f * cnt[t] / total;
            ringspr.fillArc(c, c, 40, 54, a, a + span - (total > cnt[t] ? 2.0f : 0.0f), TCOL[t]);
            a += span;
        }
    }
    // scanning sweep: an arc that turns while the scanners run
    float s0 = fmodf(millis() / 5.0f, 360.0f), s1 = s0 + 60.0f;
    uint16_t sc = total > 0 ? WHITE : ACCENT;
    if (s1 <= 360.0f) ringspr.fillArc(c, c, 34, 38, s0, s1, sc);
    else { ringspr.fillArc(c, c, 34, 38, s0, 360, sc); ringspr.fillArc(c, c, 34, 38, 0, s1 - 360.0f, sc); }
    char n[8]; snprintf(n, sizeof(n), "%d", total);
    txt(ringspr, n, c, c - 6, 4, total > 0 ? WHITE : MDGREY, BLACK, lgfx::middle_center);
    txt(ringspr, total == 1 ? "found" : total ? "found" : "clear", c, c + 18, 2, MDGREY, BLACK, lgfx::middle_center);
    ringspr.pushSprite(&tft, 14, CONT_Y + 18);
}

static void bar(int x, int y, int w, int h, uint32_t count, uint16_t col, bool live) {
    tft.fillRect(x, y, w, h, GRID);
    float f = log2f(1.0f + count) / log2f(1.0f + 200.0f);
    if (f > 1) f = 1;
    tft.fillRect(x, y, (int)(w * f), h, live ? col : MDGREY);
}

static void draw_home_right(const DetectionRegistry& reg) {
    ScanActivity live, last; bool ble; uint8_t ch;
    scan_c6_activity(live, last, ble, ch);
    char b[32];
    const int X = 152, W = 164;
    // WiFi
    uint32_t wf = ble ? last.wifi_frames : live.wifi_frames;
    uint32_t bl = ble ? live.ble_adverts : last.ble_adverts;
    txt(tft, "WiFi", X, CONT_Y + 6, 2, LTGREY, BLACK);
    snprintf(b, sizeof(b), "%u frames ", (unsigned)wf);
    txt(tft, b, X + W, CONT_Y + 6, 2, MDGREY, BLACK, lgfx::top_right);
    bar(X, CONT_Y + 26, W, 10, wf, SIGNAL, !ble);
    txt(tft, "BLE", X, CONT_Y + 44, 2, LTGREY, BLACK);
    snprintf(b, sizeof(b), "%u adverts ", (unsigned)bl);
    txt(tft, b, X + W, CONT_Y + 44, 2, MDGREY, BLACK, lgfx::top_right);
    bar(X, CONT_Y + 64, W, 10, bl, ACCENT, ble);
    // strongest
    int wr = ble ? last.wifi_best_rssi : live.wifi_best_rssi, br = ble ? live.ble_best_rssi : last.ble_best_rssi;
    char w1[8], b1[8];
    if (wr > -127) snprintf(w1, sizeof(w1), "%d", wr); else snprintf(w1, sizeof(w1), "--");
    if (br > -127) snprintf(b1, sizeof(b1), "%d", br); else snprintf(b1, sizeof(b1), "--");
    snprintf(b, sizeof(b), "best %s / %s dBm   ", w1, b1);
    txt(tft, b, X, CONT_Y + 84, 2, LTGREY, BLACK);
    // what the radio is doing now
    tft.fillCircle(X + 6, CONT_Y + 114, 5, ble ? ACCENT : SIGNAL);
    if (ble) snprintf(b, sizeof(b), "BLE slice        ");
    else     snprintf(b, sizeof(b), "WiFi ch %u        ", (unsigned)ch);
    if (g_focus.active) snprintf(b, sizeof(b), "focus: %s       ", g_focus.radio == FOCUS_WIFI ? "WiFi" : "BLE");
    txt(tft, b, X + 18, CONT_Y + 106, 2, WHITE, BLACK);
    // last alert
    char la[32];
    snprintf(la, sizeof(la), "%s", s_last_alert[0] ? s_last_alert : "no alerts yet");
    fit(tft, la, 2, W);
    tft.fillRect(X, CONT_Y + 126, W, 18, BLACK);
    txt(tft, la, X, CONT_Y + 128, 2, s_last_alert[0] ? s_last_alert_col : MDGREY, BLACK);
    (void)reg;
}

// ── STATUS page ───────────────────────────────────────────────────────────────
static void draw_status(const DetectionRegistry& reg) {
    const ScanC6Stats& s = g_scan_stats;
    char l[48];
    uint32_t up = millis() / 1000;
    int y = CONT_Y + 6;
    snprintf(l, sizeof(l), "SignalSweep C6  %s            ", UI_VERSION);
    txt(tft, l, 8, y, 2, ACCENT, BLACK); y += 22;
    snprintf(l, sizeof(l), "Up %um %02us   Found %d           ", (unsigned)(up / 60), (unsigned)(up % 60), reg.count);
    txt(tft, l, 8, y, 2, WHITE, BLACK); y += 22;
    snprintf(l, sizeof(l), "Heap %uK free, %uK low           ",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024));
    txt(tft, l, 8, y, 2, WHITE, BLACK); y += 22;
    snprintf(l, sizeof(l), "WiFi %u fr   BLE %u adv           ", (unsigned)s.wifi_done, (unsigned)s.ble_done);
    txt(tft, l, 8, y, 2, LTGREY, BLACK); y += 22;
    snprintf(l, sizeof(l), "Dropped %u   match %u us           ", (unsigned)(s.wifi_qdrop + s.ble_qdrop),
             s.wifi_done ? (unsigned)(s.wifi_us_total / s.wifi_done) : 0);
    txt(tft, l, 8, y, 2, (s.wifi_qdrop + s.ble_qdrop) ? ORANGE : LTGREY, BLACK); y += 22;
    int mv = power_battery_mv();
    char bt[16];
    if (mv > 0) snprintf(bt, sizeof(bt), "%d.%02dV", mv / 1000, (mv % 1000) / 10); else snprintf(bt, sizeof(bt), "--");
    snprintf(l, sizeof(l), "Batt %s   SD %s              ", bt, sd_card_text());
    txt(tft, l, 8, y, 2, LTGREY, BLACK);
}

// ── SETTINGS page: a scrolling list of 50 px rows ────────────────────────────
// Rows 0-4 type toggles, then alert flash, buzzer (only when built with C6_HAS_BUZZER), SD alert log (privacy switch,
// off at every boot), MAC mode of exports, "save list to SD", brightness.
enum SetItem { SI_T0 = 0, SI_FLASH = 5, SI_BUZZER, SI_SDLOG, SI_MAC, SI_SAVE, SI_BRIGHT };
static const uint8_t BRIGHT_STEPS[] = {40, 100, 170, 255};
static const char* const MACMODE_NAME[] = {"full", "OUI only", "none"};
static bool      s_sd_log = false;             // alerts.csv logging: runtime only, resets every boot (privacy)
static SdMacMode s_mac_mode = SDMAC_FULL;
static char      s_save_msg[40] = "";
static uint32_t  s_save_msg_until = 0;

static int set_items[16], set_n = 0;
static void set_build() {
    set_n = 0;
    for (int i = 0; i < 5; i++) set_items[set_n++] = SI_T0 + i;
    set_items[set_n++] = SI_FLASH;
#if C6_HAS_BUZZER
    set_items[set_n++] = SI_BUZZER;
#endif
    set_items[set_n++] = SI_SDLOG;
    set_items[set_n++] = SI_MAC;
    set_items[set_n++] = SI_SAVE;
    set_items[set_n++] = SI_BRIGHT;
}

static bool type_on(const Settings& s, int i) {
    switch (i) { case 0: return s.detect_drone; case 1: return s.detect_tracker; case 2: return s.detect_camera;
                 case 3: return s.detect_bodycam; default: return s.detect_attack; }
}
static void type_set(Settings& s, int i, bool v) {
    switch (i) { case 0: s.detect_drone = v; break; case 1: s.detect_tracker = v; break; case 2: s.detect_camera = v; break;
                 case 3: s.detect_bodycam = v; break; default: s.detect_attack = v; break; }
}

static int  set_rh(int) { return LIST_RH; }
static void set_draw(int row, const DetectionRegistry&) {
    int it = set_items[row];
    uint16_t bg = (row & 1) ? PANEL2 : PANEL, stripe = ACCENT;
    char label[28] = "", val[34] = "";
    bool on = false, is_toggle = false;
    const Settings& s = *s_cfg;
    if (it <= 4) {
        snprintf(label, sizeof(label), "%s", TNAME[it]); stripe = TCOL[it];
        on = type_on(s, it); is_toggle = true;
    } else if (it == SI_FLASH) {
        snprintf(label, sizeof(label), "Alert flash"); on = s.led_en; is_toggle = true;
    } else if (it == SI_BUZZER) {
        snprintf(label, sizeof(label), "Buzzer (GPIO7)"); on = s.speaker_en; is_toggle = true;
    } else if (it == SI_SDLOG) {
        snprintf(label, sizeof(label), "Log alerts to SD"); on = s_sd_log; is_toggle = true;
        if (s_sd_log) snprintf(val, sizeof(val), "ON  %u", (unsigned)g_sd_stats.alerts_logged);
    } else if (it == SI_MAC) {
        snprintf(label, sizeof(label), "MAC in exports"); snprintf(val, sizeof(val), "%s", MACMODE_NAME[s_mac_mode]);
    } else if (it == SI_SAVE) {
        snprintf(label, sizeof(label), "Save list to SD");
        if (millis() < s_save_msg_until) snprintf(val, sizeof(val), "%s", s_save_msg); else snprintf(val, sizeof(val), "tap");
    } else {
        snprintf(label, sizeof(label), "Brightness"); snprintf(val, sizeof(val), "%d%%", (int)(s.brightness * 100 / 255));
    }
    rowspr.fillSprite(bg);
    rowspr.fillRect(0, 0, 8, LIST_RH, stripe);
    txt(rowspr, label, 18, LIST_RH / 2, 2, WHITE, bg, lgfx::middle_left);
    if (is_toggle) {
        uint16_t pc = on ? SIGNAL : BORDER;
        rowspr.fillRoundRect(SCREEN_W - 70, 9, 58, 32, 10, pc);
        txt(rowspr, on ? "ON" : "OFF", SCREEN_W - 41, LIST_RH / 2, 2, on ? BLACK : LTGREY, pc, lgfx::middle_center);
        if (val[0]) txt(rowspr, val, SCREEN_W - 78, LIST_RH / 2, 2, MDGREY, bg, lgfx::middle_right);
    } else {
        fit(rowspr, val, 2, 150);
        txt(rowspr, val, SCREEN_W - 14, LIST_RH / 2, 2, ACCENT, bg, lgfx::middle_right);
    }
}
static RowList set_rows() { return RowList{set_n, set_rh, set_draw}; }

static void settings_tap(int row, const DetectionRegistry& reg, Settings& s) {
    if (row < 0 || row >= set_n) return;
    int it = set_items[row];
    if (it <= 4) {
        bool nv = !type_on(s, it);
        type_set(s, it, nv);
        if (!nv) purge_type((ThreatType)it);
    } else if (it == SI_FLASH) { s.led_en = !s.led_en;
    } else if (it == SI_BUZZER) { s.speaker_en = !s.speaker_en;
    } else if (it == SI_SDLOG) {
        s_sd_log = !s_sd_log;
        if (s_sd_log && !sd_card_ok()) snprintf(s_save_msg, sizeof(s_save_msg), "no SD card");   // card is checked now, once
        Serial.printf("[ui] SD alert log %s (card: %s)\n", s_sd_log ? "ON" : "off", sd_card_text());
    } else if (it == SI_MAC) { s_mac_mode = (SdMacMode)((s_mac_mode + 1) % 3);
    } else if (it == SI_SAVE) {
        char m[40];
        bool ok = sd_list_export(reg, s_mac_mode, "all", m, sizeof(m));
        snprintf(s_save_msg, sizeof(s_save_msg), "%s", m);
        s_save_msg_until = millis() + 6000;
        Serial.printf("[ui] save list: %s (%s)\n", m, ok ? "ok" : "failed");
    } else {
        int next = 0;   // next step above the current brightness, wrapping
        for (size_t k = 0; k < sizeof(BRIGHT_STEPS); k++) if (BRIGHT_STEPS[k] > s.brightness) { next = k; break; }
        s.brightness = BRIGHT_STEPS[next];
        display_backlight(s.brightness);
    }
    if (it != SI_SDLOG && it != SI_SAVE && it != SI_MAC) settings_save(s);   // persistent settings only
    s_force = true;
    Serial.printf("[ui] settings row %d (item %d) -> types=%d%d%d%d%d flash=%d brightness=%d sdlog=%d saved=%d\n", row, it,
                  s.detect_drone, s.detect_tracker, s.detect_camera, s.detect_bodycam, s.detect_attack, s.led_en, s.brightness,
                  s_sd_log, (it != SI_SDLOG && it != SI_SAVE && it != SI_MAC));
}

// ── Gesture handling ─────────────────────────────────────────────────────────
static struct {
    bool down = false, vertical = false, horizontal = false, fired = false;   // fired: the page swipe already happened mid-gesture
    int x0 = 0, y0 = 0, x = 0, y = 0, base = 0;
    int n = 0, minx = 0, maxx = 0, miny = 0, maxy = 0;   // gesture log
    uint32_t t0 = 0;
} G;

static int* cur_scroll() { return s_detail ? &s_dscroll : &s_scroll; }

static bool page_scrolls(const DetectionRegistry& reg) {
    if (s_detail) return s_dpage == DP_INFO || s_dpage == DP_LINKS || s_dpage == DP_TRAFF;
    return (s_page == P_LIST && reg.count > 0) || s_page == P_SET;
}

static RowList active_rows(const DetectionRegistry& reg) {
    if (s_detail) return detail_rows();
    return s_page == P_SET ? set_rows() : list_rows(reg);
}

static void go_page(int dir) {
    if (s_detail) {
        int np = s_dpage + dir;
        if (np < 0) { close_detail(); return; }          // swipe right on the first detail page = back
        if (np >= DP_COUNT) return;
        hunt_off(); hunt_reset();
        s_dpage = np; s_dscroll = 0;
    } else {
        int np = s_page + dir;
        if (np < 0 || np >= P_COUNT) return;
        s_page = np; s_scroll = 0;
    }
    s_force = true; s_hdr_dirty = true;
    s_page_req_ms = millis();
}

static void on_tap(int tx, int ty, const DetectionRegistry& reg, Settings& s) {
    Serial.printf("[ui] tap %d,%d page=%d%s\n", tx, ty, s_detail ? s_dpage : s_page, s_detail ? " (detail)" : "");
    if (s_detail) {
        if (tx < 70 && ty < HDR_H + 8) { close_detail(); return; }      // BACK corner (generous: 70 x 32)
        if (s_dpage == DP_LINKS || s_dpage == DP_TRAFF) {
            RowList L = detail_rows();
            int i = rows_hit(L, s_dscroll, ty);
            if (i >= 0 && i < s_nl && s_lrows[i].kind == LR_PEER) pivot_to(s_lrows[i]);
        }
        return;
    }
    if (s_page == P_LIST) {
        RowList L = list_rows(reg);
        int i = rows_hit(L, s_scroll, ty);
        if (i >= 0 && i < reg.count) open_detail(reg.items[i]);
    } else if (s_page == P_SET) {
        settings_tap(rows_hit(set_rows(), s_scroll, ty), reg, s);
    }
}

static void on_touch(const TouchEvent& e, const DetectionRegistry& reg, Settings& s) {
    if (e.kind == TE_DOWN) {
        G.down = true; G.vertical = G.horizontal = G.fired = false;
        G.x0 = G.x = e.x; G.y0 = G.y = e.y; G.base = *cur_scroll();
        G.n = 1; G.minx = G.maxx = e.x; G.miny = G.maxy = e.y; G.t0 = e.ms;
    } else if (e.kind == TE_MOVE && G.down) {
        G.x = e.x; G.y = e.y;
        G.n++; G.minx = min(G.minx, (int)e.x); G.maxx = max(G.maxx, (int)e.x);
        G.miny = min(G.miny, (int)e.y); G.maxy = max(G.maxy, (int)e.y);
        int dx = G.x - G.x0, dy = G.y - G.y0;
        if (!G.vertical && !G.horizontal) {
            if (abs(dy) > DRAG_START && abs(dy) > abs(dx)) G.vertical = true;
            else if (abs(dx) > DRAG_START && abs(dx) > abs(dy)) G.horizontal = true;
        }
        if (G.vertical && page_scrolls(reg)) {
            RowList L = active_rows(reg);
            int ns = constrain(G.base - dy, 0, rows_max_scroll(L));
            if (ns != *cur_scroll()) { *cur_scroll() = ns; s_force = true; }
        }
        // Page swipe fires as soon as the travel is enough, without waiting for the finger to lift (the lift is only
        // detected after the release timeout, which made every swipe feel late).
        if (!G.fired && !G.vertical && abs(dx) >= SWIPE_MIN_DX && abs(dx) > abs(dy) * 3 / 2) {
            G.fired = true;
            Serial.printf("[ui] gesture SWIPE (live): frames=%d dur=%ums dx=%d dy=%d\n", G.n, (unsigned)(e.ms - G.t0), dx, dy);
            go_page(dx < 0 ? +1 : -1);                // finger moves left = next page
        }
    } else if (e.kind == TE_UP && G.down) {
        G.down = false;
        if (G.fired) return;                          // already handled while moving
        int dx = G.x - G.x0, dy = G.y - G.y0;
        const char* verdict = (abs(dx) >= SWIPE_MIN_DX && abs(dx) > abs(dy) * 3 / 2) ? "SWIPE"
                            : (abs(dx) <= TAP_SLOP && abs(dy) <= TAP_SLOP) ? "tap"
                            : (abs(dy) >= abs(dx)) ? "vertical/diagonal"
                            : (abs(dx) < SWIPE_MIN_DX) ? "too short" : "too diagonal";
        Serial.printf("[ui] gesture %s: frames=%d dur=%ums from %d,%d to %d,%d dx=%d dy=%d (x %d..%d, y %d..%d)\n",
                      verdict, G.n, (unsigned)(e.ms - G.t0), G.x0, G.y0, G.x, G.y, dx, dy, G.minx, G.maxx, G.miny, G.maxy);
        if (abs(dx) >= SWIPE_MIN_DX && abs(dx) > abs(dy) * 3 / 2) {
            go_page(dx < 0 ? +1 : -1);                // finger moves left = next page
        } else if (abs(dx) <= TAP_SLOP && abs(dy) <= TAP_SLOP) {
            on_tap(G.x0, G.y0, reg, s);
        }
    }
}

// ── Public API ────────────────────────────────────────────────────────────────
void ui_init(Settings& s) {
    s_cfg = &s;
    display_init(tft, s.brightness);
    touch_init();
    alert_init();
    set_build();
    rowspr.setColorDepth(16);
    ringspr.setColorDepth(16);
    bool ok = rowspr.createSprite(SCREEN_W, LIST_RH) && ringspr.createSprite(112, 112);
    s_have_sprites = ok;
    Serial.printf("[ui] sprites %s (heap largest block %u)\n", ok ? "ok" : "FAILED",
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    tft.fillScreen(BLACK);
}

// Called every few ms, separately from the drawing, so a fast swipe is not sampled at the drawing rate
void ui_touch(const DetectionRegistry& reg, Settings& s) {
    TouchEvent e;
    while (touch_poll(e)) on_touch(e, reg, s);
}

void ui_update(const DetectionRegistry& reg, Settings& s) {
    s_cfg = &s;
    if (!s_have_sprites) return;
    uint32_t now = millis();
    // Latency probe: time from a page change request (go_page) to the end of the redraw that follows it.
    struct PageProbe { uint32_t start; ~PageProbe() {
        if (s_page_req_ms) { Serial.printf("[ui] page change: request->drawn %lu ms (this update %lu ms)\n",
            (unsigned long)(millis() - s_page_req_ms), (unsigned long)(millis() - start)); s_page_req_ms = 0; } } } probe{now};
    alert_tick(s);

    static uint32_t s_bat_ms = 0;
    if (now - s_bat_ms >= 5000) { s_bat_ms = now; s_hdr_dirty = true; }      // battery text in the header

    // header: page change, count change, toast start/stop/blink
    static bool s_toast_was = false;
    bool toast = now < s_toast_until;
    if (s_hdr_dirty || reg.count != s_last_count || toast || s_toast_was) {
        static uint32_t s_hdr_ms = 0;
        if (s_hdr_dirty || reg.count != s_last_count || toast != s_toast_was || now - s_hdr_ms > 100) {
            draw_header(reg);
            s_hdr_ms = now;
            s_hdr_dirty = false;
            s_last_count = reg.count;
            if (s_toast_was && !toast) s_force = true;
        }
    }
    s_toast_was = toast;

    if (s_flash_t0) {                                       // alert flash: content area in the threat colour, twice
        uint32_t dt = now - s_flash_t0;
        if (dt >= 330) { s_flash_t0 = 0; s_flash_on = false; s_force = true; }
        else {
            bool on = dt < 110 || (dt >= 220);
            if (on != s_flash_on) {
                s_flash_on = on;
                if (on) tft.fillRect(0, CONT_Y, SCREEN_W, CONT_H, s_flash_col); else s_force = true;
            }
            if (s_flash_on) return;
        }
    }

    static uint32_t s_page_ms = 0;
    static int s_prev_count = -1;
    if (s_force) { tft.fillRect(0, CONT_Y, SCREEN_W, CONT_H, BLACK); s_page_ms = 0; }
    bool force = s_force;
    s_force = false;

    if (s_detail) {
        if (s_dpage == DP_HUNT) {
            if (force) { detail_build(reg); }
            if (force || now - s_page_ms >= 40) { s_page_ms = now; hunt_draw(force); }
            // keep the target's last copy fresh
            const Detection* live = find_mac(reg, s_dmac);
            s_dgone = live == nullptr;
            if (live) s_dlast = *live;
        } else if (force || now - s_page_ms >= 1000) {
            s_page_ms = now;
            detail_build(reg);
            RowList L = detail_rows();
            if (s_dscroll > rows_max_scroll(L)) s_dscroll = rows_max_scroll(L);
            rows_paint(L, s_dscroll, reg);
        }
        return;
    }

    switch (s_page) {
        case P_HOME:
            if (force || now - s_page_ms >= 100) {
                s_page_ms = now;
                draw_ring(reg);
                static uint32_t s_right_ms = 0;
                if (force || now - s_right_ms >= 500) { s_right_ms = now; draw_home_right(reg); }
            }
            break;
        case P_LIST:
            if (force || reg.count != s_prev_count || now - s_page_ms >= 2000) {
                s_page_ms = now;
                s_prev_count = reg.count;
                RowList L = list_rows(reg);
                int ms = rows_max_scroll(L);
                if (s_scroll > ms) s_scroll = ms;
                if (reg.count == 0) {
                    tft.fillRect(0, CONT_Y, SCREEN_W, CONT_H, BLACK);
                    txt(tft, "No detections yet", SCREEN_W / 2, CONT_Y + CONT_H / 2 - 12, 2, LTGREY, BLACK, lgfx::middle_center);
                    txt(tft, "scanning WiFi and BLE", SCREEN_W / 2, CONT_Y + CONT_H / 2 + 12, 2, MDGREY, BLACK, lgfx::middle_center);
                } else {
                    rows_paint(L, s_scroll, reg);
                }
            }
            break;
        case P_STATUS:
            if (force || now - s_page_ms >= 1000) { s_page_ms = now; draw_status(reg); }
            break;
        case P_SET:
            if (force || now - s_page_ms >= 1000) {
                s_page_ms = now;
                RowList L = set_rows();
                if (s_scroll > rows_max_scroll(L)) s_scroll = rows_max_scroll(L);
                rows_paint(L, s_scroll, reg);
            }
            break;
    }
}

void ui_update_scope(const uint8_t*, int) {}   // the C6 has no scope (docs/port/UI_CUTS.md)

void ui_alert(const Detection& d, const Settings& s) {
    uint16_t tc = TCOL[(int)d.type];
    snprintf(s_toast, sizeof(s_toast), "%s: %s", TNAME[(int)d.type], d.name);
    fit(tft, s_toast, 2, SCREEN_W - 10);
    s_toast_col = tc;
    s_toast_start = millis();
    s_toast_until = s_toast_start + 4000;
    snprintf(s_last_alert, sizeof(s_last_alert), "%s", d.name);
    s_last_alert_col = tc;
    alert_fire(d.type, s);                              // backlight pattern / buzzer, advanced by alert_tick()
    if (s.led_en) { s_flash_t0 = millis(); s_flash_col = tc; }
    if (s_sd_log) sd_log_alert(d);                      // UI task only: the SD card shares the LCD's SPI bus
}
