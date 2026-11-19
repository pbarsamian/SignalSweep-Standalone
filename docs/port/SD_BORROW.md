# SD features borrowed from the post-3.21 SignalSweep-CYD work

Decision (Paul, 2026-11-19): the standalone firmware gets the microSD features built after v3.21 on the split
architecture. Implementation is **Phase 5** (it needs the HAL `storage` seam from Phase 1 and, on the C6, the shared
SPI arbitration from Phase 2). This file is the map, so Phase 5 starts from known code instead of a search.

Source: `github.com/pbarsamian/SignalSweep-CYD`, `main` at `163c063`, files under `firmware-cyd/src/`. Same author,
GPL-3.0-or-later, so the code can be copied with its SPDX headers.

## What exists there

| Feature | Commits | Source file |
|---|---|---|
| Wi-Fi capture to pcapng (radiotap + 802.11) | `1412ab3` | `sdcap.cpp` |
| BLE adverts in the same capture file | `d911561` | `sdcap.cpp` (`write_ble_epb`) |
| Per-packet comment with matched rule + confidence | `679bfc6` | `sdcap.cpp` |
| Pre-trigger ring (frames heard before REC), marked in the file | `2ef560d` | ring is in `firmware-c5/main/capture.cpp`; the CYD side only flags it (`CAP_F_PRE`) |
| `alerts.csv` log behind a SET > PRIVACY switch, off at every boot | `1469c9f` | `sdcap.cpp` (`sdcap_log_alert`, `sdcap_dump_alerts`) |
| Detection list export, CSV + JSON, "schema v1" | `64b66f7` | `listexport.cpp`, schema table in `PROTOCOL.md` |

## Reusable as is

- pcapng writer: the 4 KB staging buffer and `put/p16/p32/pad4`, `write_headers`, `write_epb`, `write_ble_epb`,
  and `write_sidecar` (Wireshark display filter and name map).
- SD mount with a 3 s retry guard, `/signalsweep/` directory, `cap_NNN` numbering that never overwrites.
- `alerts.csv` row format and the CSV-safe text cleaning.
- `listexport` and schema v1 (CSV and JSON from one code path, MAC modes `full | oui | none`, no payloads, no timestamps
  because the device has no clock, only ages relative to the snapshot).
- The deliberate limits: data frames cut to the 802.11 header (no payloads, no handshakes), nothing transmitted.

## Must be re-done for the standalone design

1. **Frame source.** There the C5 streams frames as CBOR (`sdcap_event` parses `ss_cbor` maps with 12-byte record headers).
   Standalone has the frames in hand: the `promisc()` callback (`scanner_wifi.cpp:117`) and `SweepCallback::onResult`
   (`scanner_ble.cpp`). Replace `sdcap_event` with a function the scan side feeds directly, through the Phase 3 queue
   (copy in the callback, write on the storage task). Do not write to SD from the callbacks.
2. **Pre-trigger ring.** It lives in the C5 firmware. Re-implement in the standalone scanner: roll the focused target's
   frames into a RAM ring while a focus is active, allocate on focus start and free on focus end. Size it from the
   Phase 2 heap numbers (the CYD value was 16 KB, 8 KB fallback). The C6 has 512 KB total and no PSRAM.
3. **Data model mapping.** The code takes `Det` from the CYD's `model.h`. Map to this repo's `Detection`:
   `conf`->`confidence`, `best`->`best_weight`, `d.mac` (text there)->`mac_str()`, `radio`->`proto`, `seen`->`seen_count`,
   `ch`->`channel`. `band` is 2 for Wi-Fi and 0 for BLE until a 5 GHz source exists.
   `CONF_ALERT_MIN` (70) comes from `theme.h` there. Standalone already has the alert threshold in `signatures`.
4. **Settings.** Add the privacy switch for `alerts.csv` (default OFF, reset each boot) to `Settings`/UI.

## Not borrowed (C5-only, out of scope)

`ss_cbor`, `uart_link`, `model.*`, `mode.*`, `status_ui`, the SET_CONFIG `{cap}` flag, and anything in `firmware-c5/`.

## Board constraints found while reading it

- **CYD:** the SD slot uses the VSPI controller (GPIO 18/19/23, CS 5). v3.21's touch driver also instantiates
  `SPIClass(VSPI)` on other pins, so the two collide. The later CYD work solved it by **bit-banging the touch chip** on its own
  four pins (`firmware-cyd/include/touch.h`). The CYD standalone build needs the same change before SD works. It goes into
  `boards/cyd/touch_cyd.cpp` in Phase 5, behind the HAL, so the core is untouched.
- **C6:** LCD and TF share SCK (GPIO1) and MOSI (GPIO2), separate CS. Every SD access and every display flush goes
  through the `storage_lock()/unlock()` mutex. The existing code writes SD from one loop task, so only the display flush needs wrapping.
- RAM: the staging buffer is 4 KB static plus the ring. Budget both against the Phase 2 free-heap report.
