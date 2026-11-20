// SPDX-License-Identifier: GPL-3.0-or-later
// BLE advertisement as the C6 scanner carries it between the NimBLE callback and the worker task:
// plain bytes, no NimBLE objects. The callback copies these fields; ble_process_advert() (ble_adv.cpp)
// parses the payload and runs the same rules as the CYD's scanner_ble.cpp.
#pragma once
#include <stdint.h>

// Legacy advertising is 31 bytes, plus 31 more when a scan response is merged. NimBLE-Arduino 2.5.2 is built
// without extended advertising (nimconfig.h: CONFIG_BT_NIMBLE_EXT_ADV is commented out), so 62 is the most
// it can deliver; anything longer is cut to this size and flagged.
#define BLE_ADV_MAX 64

struct BleAdv {
    uint8_t mac[6];       // display order (first byte = first octet of the printed address)
    uint8_t addr_type;    // NimBLE/HCI address type: 0 public, 1 random, 2/3 resolved-identity variants
    uint8_t adv_type;     // HCI advertising report event type (0 ADV_IND ... 4 SCAN_RSP)
    int8_t  rssi;
    uint8_t len;          // payload bytes used
    uint8_t truncated;    // payload was longer than BLE_ADV_MAX
    uint8_t payload[BLE_ADV_MAX];
};

// Parse and match one advertisement, then post any detection (post_detection). Worker task only.
void ble_process_advert(const BleAdv& a);
