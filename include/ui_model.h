// SPDX-License-Identifier: GPL-3.0-or-later
// ui_model.h — what the detail screens say, as rows of text, with no drawing code. Shared by the CYD UI (src/ui.cpp)
// and the C6 UI (src/ui_c6/). Moved unchanged out of ui.cpp in Phase 4.
#pragma once
#include <Arduino.h>
#include "detection.h"
#include "focus.h"

// INFO page: key/value rows for one detection (+ focus-mode rows)
struct DRow { const char* k; char v[40]; };
extern DRow s_rows[48];
extern int  s_nrows;
void build_rows(const Detection& d, uint32_t now);
void add_focus_rows(const Detection& d);   // appends to the rows above

// LINKS / TRAFF pages: text rows; LR_PEER rows are tappable (pivot focus to that device)
enum { LR_HEAD = 0, LR_INFO = 1, LR_PEER = 2 };
struct LRow {
    char     text[48];
    uint8_t  kind;
    uint8_t  radio;       // FOCUS_WIFI / FOCUS_BLE, for LR_PEER
    uint8_t  channel;
    int8_t   rssi;
    uint8_t  mac[6];
    uint16_t color;
};
extern LRow s_lrows[48];
extern int  s_nl;
void build_links(const Detection& d, const DetectionRegistry& reg, const FocusProfile& f, bool have);
void build_traffic(const FocusProfile& f, bool have);
