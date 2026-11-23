// SPDX-License-Identifier: GPL-3.0-or-later
// sd_store.h — microSD logging for the C6 build: alerts.csv and detection-list export (schema v1).
// Ported from SignalSweep-CYD@163c063 firmware-cyd/src/sdcap.cpp (sdcap_log_alert, sdcap_dump_alerts) and listexport.cpp
// (same author, GPL-3.0-or-later); see docs/port/SD_BORROW.md. pcapng capture is not ported yet.
//
// BUS RULE: the LCD and the TF slot share SCK/MOSI (GPIO1/2) on one SPI host. Every call in this header does SD I/O and
// must be made from the UI task, which is also the only task that draws, so the two can never use the bus at once.
// (If another task ever needs the card, add a mutex around the UI's drawing and these calls: hal/storage.h.)
// Nothing here runs from a radio callback or the matching worker.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "detection.h"

enum SdMacMode : uint8_t { SDMAC_FULL = 0, SDMAC_OUI = 1, SDMAC_NONE = 2 };

struct SdStats { uint32_t alerts_logged, alerts_failed, alerts_no_card; };
extern SdStats g_sd_stats;

bool        sd_card_ok();                 // mounts on first use; after a failure retries at most every 3 s
const char* sd_card_text();               // "not checked", "no card", "ready 14.8 GB"

void sd_log_alert(const Detection& d);    // one row appended to /signalsweep/alerts.csv (caller checks the privacy switch)
void sd_dump_alerts();                    // serial: print alerts.csv between marker lines

// Writes /signalsweep/list_NNN.csv and .json (first free number). msg gets a short result for the screen.
bool sd_list_export(const DetectionRegistry& reg, SdMacMode mode, const char* filter, char* msg, size_t msgsz);

// Generic one-line append, its own session. path is relative to the card root (e.g. "/signalsweep/portal_log.txt").
bool sd_append_line(const char* path, const char* line);
