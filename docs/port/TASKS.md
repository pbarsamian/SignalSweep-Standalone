# C6 task layout, queues and the Phase 3 API notes

Applies to `env:c6scan` (Phase 3, serial output) and is the starting point for the UI and alert tasks in Phases 4 and 5.
Code: `src/scan_c6/`. The CYD firmware is unchanged in structure (dual core, matching inside the radio callbacks).

## Why the C6 differs from the CYD
The CYD has two cores: the scan task runs matching inside the WiFi promiscuous callback and the NimBLE callback on core 0, and the UI
runs on core 1. The C6 has **one** core (`BOARD_SCAN_TASK_CORE` and `BOARD_UI_TASK_CORE` are both 0). Everything shares it: the WiFi task,
the BLE controller and host tasks, the scanner, the UI and USB serial. Matching one frame walks 96 rules, so it must not run in the callbacks that
hold the radio tasks. The callbacks only copy bytes and queue them.

## Tasks (single core)
| Task | Priority | Stack (bytes) | Free at worst so far | What it does |
|---|---|---|---|---|
| WiFi task (IDF) | not read | IDF | n/a | runs `wifi_cb`: copy the frame into a pool slot, queue the slot index, return |
| NimBLE host task | `configMAX_PRIORITIES - 4` (`nimble_port_freertos.c:40`) | NimBLE | n/a | runs `SweepCallbacks::onResult`: copy the advert into `s_bq`, return |
| `scanctl` (control) | 3 | 4096 | 2216 | BLE/WiFi time-slicing, channel hop, focus mode, registry prune. Sleeps 40 ms (WiFi) or 50 ms (BLE) per loop, so it uses very little CPU |
| `scanwork` (worker) | 1 | 6144 | 3528 | takes one item at a time from both queues (queue set) and runs the matching: `wifi_process_frame()` (shared with the CYD) and `ble_process_advert()` |
| `loopTask` (Arduino) | 1 (`cores/esp32/main.cpp:113`) | Arduino default | not measured | Phase 3: serial commands and 5 s status lines |
| `ui` (Phase 4, `ui_c6/main_c6.cpp`) | 2 | 8192 | see `[stack] ui` from the `i` command | touch events, page drawing (LovyanGFX), alert queue. Above the worker so the screen stays responsive, below `scanctl`. Draws from a registry snapshot taken each frame |

Priorities: the worker is deliberately low (1). Under a frame flood the pool fills and frames are dropped and counted (`wifi_qdrop`),
which is the designed overload behaviour; a high-priority worker would instead starve the UI and serial. Stack sizes were set from the CYD (8192 for its scan task)
and the measured free-minimum (worker 6144 B with 3528 B free at worst, `scanctl` 4096 B with 2216 B free) says both could shrink a little, but the
ODID decoder and the WiFi init path run in these tasks, so they are left alone until the UI is in.

## Queues and buffers
| Object | Size | Policy |
|---|---|---|
| WiFi frame pool | `WIFI_POOL` 12 slots x (512 B data + 8 B header) = 6.2 KB static | callback takes a free slot index (`s_free`), fills it, sends the index on `s_wq`; the worker returns it. No slot = frame dropped, `wifi_qdrop++` |
| BLE queue `s_bq` | `BLE_QUEUE_DEPTH` 24 x `BleAdv` (75 B) = 1.8 KB | `xQueueSend(..., 0)`; full = dropped, `ble_qdrop++` |
| Queue set | 36 entries | worker blocks on it with a 1 s timeout |
| Alert queue `g_queue` | 16 ints (unchanged) | `post_detection` queues the registry index of a new alert; Phase 3 `loop()` prints it, Phase 4's UI task will draw it |

Frame truncation: management frames keep their first `WIFI_RAW_MAX` = 512 bytes, data frames (focus only) the first `WIFI_DATA_COPY` = 64
(headers are all `focus_note_peer` reads; the real length is passed separately for the byte tally). The longest management frame
seen in the test runs was 446 B (4074 B for a data frame during the survey), and `wifi_trunc` stayed 0. Beacons with a late information element past
512 B would lose it (the IE walk stops at the cut and ignores the missing FCS); if `wifi_trunc` is not 0 in the field, raise `WIFI_RAW_MAX` (heap: 12 x the increase).

Callback rules (what keeps the radio tasks free): no `g_reg_mutex`, no `snprintf`/`String`, no registry or focus calls, no `Serial`.
(`ble_cb` does one `std::string` for a one-time MAC-order check on the very first advert.)

## Radio schedule
Named constants in `scan_c6.h`: `SCAN_SLICE_MS` 6000 (one BLE or WiFi slice; the CYD's `BLE_SLOT_MS`/`WIFI_SLOT_MS`) and `WIFI_HOP_MS` 40 (dwell per channel, as the CYD's
`scanner_wifi_tick`). Channels 1-11 as on the CYD. Focus mode behaves as on the CYD (WiFi focus locks the channel and widens the filter to data; BLE focus
runs the scan window at 100%). Bench commands can force one radio (`b`, `w`). Whether a continuous both-radios mode works better on the C6 than the 6 s slicing is untested.

## Activity summary (replaces the CYD scope, per `UI_CUTS.md`)
`scan_c6_activity()` returns, for the slice in progress and the slice that just ended: management frames, beacons and BLE adverts counted, and the strongest RSSI
for each radio, plus the radio now active and the WiFi channel. The callbacks update it with plain increments. The Phase 4 indicator draws from this.

## API differences found while porting (each checked against the header in the toolchain)
| Area | CYD (arduino-esp32 2.x / IDF 4.4, NimBLE-Arduino 1.4) | C6 (arduino-esp32 3.3.12 / IDF 5.5.5, NimBLE-Arduino 2.5.2) | Where |
|---|---|---|---|
| Promiscuous packet header | `wifi_pkt_rx_ctrl_t` has `rssi`, `channel`, `sig_len` | same field names on the C6, `sig_len` is 12 bits and includes the FCS | `framework-arduinoespressif32-libs/esp32c6/include/esp_wifi/include/local/esp_wifi_types_native.h:36,59,81` |
| WiFi event loop | not needed in practice | `esp_wifi_init` posts events to the IDF default loop; without it the log shows `failed to post WiFi event=43 ret=259`. `main_c6_serial.cpp` calls `esp_event_loop_create_default()` first (Phase 3 run: no such error, so this was the cause) | Phase 2 bring-up log, Phase 3 boot line `[boot] default event loop: created` |
| Scan callbacks | `setAdvertisedDeviceCallbacks(cb, wantDuplicates)`, `onResult(NimBLEAdvertisedDevice*)` | `setScanCallbacks(cb, wantDuplicates)`, `onResult(const NimBLEAdvertisedDevice*)` | `NimBLEScan.h:73,222` |
| Starting a scan | `start(0, nullptr, false)` | `start(duration, isContinue, restart)`; used as `start(0, false, true)` | `NimBLEScan.h:71` |
| TX power | `setPower(esp_power_level_t)` | `setPower(int8_t dbm)`; used `setPower(0)` | `NimBLEDevice.h:162` |
| Payload access | `getPayload()` pointer + `getPayloadLength()` | `getPayload()` returns `const std::vector<uint8_t>&` | `NimBLEAdvertisedDevice.h:99` |
| Copying a device out of the callback | n/a | not safe: the object carries an intrusive list node (`m_pNextWaiting`, `NimBLEAdvertisedDevice.h:166`) and its constructor is private, so the C6 copies raw fields and parses the payload itself (`ble_adv.cpp`) | `NimBLEAdvertisedDevice.h:154-166` |
| Scan response merge | scan response appended to the device that `onResult` receives | same behaviour in 2.x (`update()` appends a legacy SCAN_RSP to the payload and the waiting list sends `onResult` once both arrived) | `NimBLEAdvertisedDevice.cpp:62-87`, `NimBLEScan.cpp:61,275-343` |
| Extended advertising | n/a | disabled in this build (`nimconfig.h:48` commented out), so adverts are at most 62 bytes (31 + scan response) and BLE 5 extended Remote ID messages are not seen. Same coverage as the CYD | `NimBLEDevice` config |
| Address byte order | parsed from `toString()` | `getVal()` is least-significant first; the callback reverses it. Checked on hardware: `mac order check: OK` against `toString()` | `NimBLEAddress.h:62`, Phase 3 run |

## Verified on the board (2026-11-19, `c6scan`, Paul's unit)
- Boot, both radios up and slicing 6 s / 6 s; real traffic matched through the worker path: about 2300 management frames and 1500 BLE adverts in the first 90 s, zero queue drops, zero truncations.
- Synthetic inputs through the exact callback path (serial commands `1`, `2`, `3`): Penguin-name advert, a `TELLO-TEST01` beacon and an Apple 0x12/0x19 advert each produced the expected rule, score 80, a NEW entry and an ALERT.
- Worker time per frame (esp_timer): WiFi mean 0.5 to 0.8 ms, max 1.4 ms; BLE mean 0.9 ms, max 1.9 ms. This is on the order of 100 beacons/s x 0.8 ms = 8% of the core at the rates seen.
  `oui_matches()` uses `sscanf` once per rule per frame and is the likely cost; it was not profiled, so it is not changed yet.
- Focus mode: WiFi focus locked channel 6 and BLE focus ran at full duty on the synthetic targets, and `g` resumed the sweep. Real peers and data frames were not tested (no real target).
- Frame types the C6 promiscuous callback delivers: with the filter set to `WIFI_PROMIS_FILTER_MASK_ALL` for about 12 s, mgmt=1675 and data=389 arrived, **ctrl=0, misc=0**.
  Management subtypes seen: 0, 1, 2, 4, 5, 7, 8, 9, 10, 11, 12, 13 (action frames, subtype 13, were common). One short run in one room, not an exhaustive result.
- Heap (internal RAM, both radios running, registry live): free about 225 KB, minimum about 224 KB, largest block about 209 KB.

### 10-minute soak (bench, not a field test)
`c6scan` left running on Paul's desk for 10 minutes with `i` sampled once a minute: no reset (the capture was opened on an already-running board and no boot banner appeared in the 10 minutes), no `[match]` lines (so no false positives from the surrounding phones, laptops and routers),
cumulative 6396 management frames and 4429 BLE adverts processed with `qdrop=0` and `trunc=0` throughout, pool and queue empty at every sample, worker stack free-minimum 3736 B and `scanctl` 2216 B, heap free 225.5 to 225.9 KB with the minimum 224.3 KB
(the minimum moved by about 1 KB in the first minutes and then stayed put, so no leak is visible at this length). Worker time per frame stayed at 0.7 to 0.8 ms (WiFi) and 0.9 ms (BLE) on average, 2.0 ms and 1.9 ms worst case. Longest management frame 446 B.

## Known gaps / follow-ups
- `src/scan_c6/ble_adv.cpp` duplicates the CYD's `scanner_ble.cpp` rules over a different data source. Unify after the CYD hardware tests (`CYD_TEST_CHECKLIST.md`) so the BLE rules live in one place.
- `scanner_wifi.cpp` is shared. The CYD's promiscuous callback now calls `wifi_process_frame()` instead of holding the body itself (same code, moved); CYD build: RAM +8 B, flash +148 B. **Not yet run on a CYD.**
- Real-world tests Paul has to run: see "Phase 3 hardware checklist" in `docs/port/PHASE3_CHECKLIST.md`.

## Phase 4 additions (env `c6touch`)
- Heap with display, two sprites (320x50 row band = 32 KB, 112x112 ring = 25 KB), both radios and the registry: **about 132 KB free, largest block about 117 KB** (Phase 3 serial-only: 225 KB / 209 KB). So the UI plus LovyanGFX costs about 93 KB of heap. A full 110 KB frame buffer would not fit alongside these, which confirms banded/sprite rendering.
- Worker time per frame measured while the UI runs: mean unchanged (0.9 ms), worst case rises to 14 to 24 ms because the UI task (priority 2) preempts the worker (priority 1) during sprite pushes. Queue drops stayed 0 in a 30 s test with demo inputs; not tested under heavy traffic with the UI scrolling.
