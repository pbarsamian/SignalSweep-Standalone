#pragma once
#include <esp_wifi_types.h>
#include "settings.h"
// cb == nullptr: the built-in callback does all the matching itself (CYD). The C6 passes a callback that only
// copies the frame to a worker task (src/scan_c6/).
void scanner_wifi_init(const Settings& s, wifi_promiscuous_cb_t cb = nullptr);
void scanner_wifi_data_filter(bool on);                        // focus: mgmt+data vs mgmt only
void scanner_wifi_tick(uint8_t* out_scope, int lock_ch = 0);   // lock_ch > 0: focus, no hopping (CYD: also draws the scope)
int  scanner_wifi_hop(int lock_ch = 0);                        // channel step only; returns the channel
// type = wifi_promiscuous_pkt_type_t. Any context that is allowed to take g_reg_mutex.
void wifi_process_frame(uint8_t type, uint8_t* payload, int length, int wire_len, int rssi, int rxCh);
