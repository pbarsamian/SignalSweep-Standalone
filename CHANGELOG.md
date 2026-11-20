# Changelog

## v3.21-c6d — Phase 5: alerts, SD logging, settings, battery (in progress, hardware review pending)
- **Alerts:** `alert_c6.cpp` runs a non-blocking backlight pulse pattern per threat type (advanced by a new `alert_tick()` HAL call), plus a two-flash colour flash of the content area and the header banner. Optional passive buzzer on GPIO7 behind `-DC6_HAS_BUZZER=1` (`env:c6touch_buzzer`, compiled, untested; no buzzer fitted). New HAL call `display_backlight()`.
- **SD:** `src/sd_log/` ports `alerts.csv` and the detection-list export (schema v1) from SignalSweep-CYD@163c063. Alert logging is behind a privacy switch that is off at every boot. SD I/O happens only in the UI task, which is also the only task that draws, so the shared LCD/TF bus is never used by two tasks (no mutex needed yet). The alert path copies the detection out and releases the registry mutex before it can touch the card. pcapng capture is not ported.
- **Settings:** SETTINGS is now a scrolling list of 50 px rows: five type toggles, alert flash, (buzzer), SD alert log, MAC mode of exports, save list to SD, brightness. Persistence stays on NVS `Preferences` (already used since v3.21; no migration, no new partition use). Alert flash and buzzer reuse the existing `led_en` and `speaker_en` keys.
- **Battery:** `power_c6.cpp` reads GPIO0 with `analogReadMilliVolts`, VBAT = 3 x VADC, smoothed; header shows a rough percent, STATUS the voltage; serial `v`. Needs a meter check.
- **Not done:** pcapng capture and the CAP page, OTA (the CYD firmware has none), IMU (optional, off by default).
- Build: c6touch RAM 87,864 B static, flash 1,369,866 B; CYD RAM 85,804 B (unchanged), flash 1,038,409 B (+24 B vs v3.21-c6c). CYD not run on hardware. Version strings in the CYD sources are `v3.21-c6d`.
- Hardware checklist: `docs/port/PHASE5_CHECKLIST.md`.

## v3.21-c6c — Phase 4: C6 UI, 320x172 landscape, swipe navigation (in progress, hardware review pending)
- New `env:c6touch` (scanners + UI): `src/ui_c6/` (swipe pages HOME / DETECTIONS / STATUS / SETTINGS, detail view with INFO / LINKS / TRAFFIC / HUNT), `src/boards/c6_touch147/` display, touch (with press/move/release events, `hal/touch_events.h`) and a placeholder alert output.
- Shared change: the detail-page row builders moved unchanged from `src/ui.cpp` to `src/ui_model.cpp` + `include/ui_model.h`. CYD build: RAM 85,804 B (unchanged), flash 1,038,385 B (+252 B vs v3.21-c6b). Not run on a CYD.
- C6 build: static RAM 86,840 B, flash 1,306,594 B. Heap with UI and radios: about 132 KB free.
- Cuts and decisions: `docs/port/UI_CUTS.md`. Hardware checklist: `docs/port/PHASE4_CHECKLIST.md`. The layout was built without seeing the screen.
- Serial: `1`/`2`/`3` inject demo detections, `i` prints heap/stack/queue info.
- Version strings in the CYD sources are now `v3.21-c6c`.

## v3.21-c6b — Phase 3: scanning core with serial output (in progress, gate pending)
- New `env:c6scan` (`src/scan_c6/`): WiFi promiscuous and BLE scanners on the single-core C6, serial output only. Radio callbacks only copy; a worker task does the matching; a control task time-slices the radio (`SCAN_SLICE_MS` 6000). Details and priorities: `docs/port/TASKS.md`.
- Shared change: `scanner_wifi.cpp` now has `wifi_process_frame()` (the old callback body) and `scanner_wifi_hop()`; the CYD callback calls the former. `scanner_wifi_init` takes an optional callback. `post_detection` gained an optional trace hook (`g_post_trace`, null on the CYD). Behavior on the CYD is meant to be identical, but it has not been run on a CYD.
- C6 BLE matching (`src/scan_c6/ble_adv.cpp`) parses the raw advertisement because NimBLE-Arduino 2.x devices cannot be copied out of the callback. It duplicates the CYD rules; see `TASKS.md` for the known differences.
- Serial test mode (`h`): per-match trace with rule and score, synthetic BLE and WiFi inputs, forced single radio, frame-type survey, focus commands.
- Activity summary (`scan_c6_activity()`) for the Phase 4 indicator.
- Build (cyd): static RAM 85,804 B (+8 B), flash 1,038,133 B (+148 B vs v3.21-hal1). Build (c6scan): RAM 66,448 B static, flash 1,176,298 B.
- Version strings in the CYD sources are now `v3.21-c6b` (the CYD sources changed).
- Hardware checklist: `docs/port/PHASE3_CHECKLIST.md`.

## v3.21-c6a — Phase 2: C6 environment scaffold and bring-up (in progress, gate pending)
- `c6_common`, `c6bringup` and `c6touch` (scaffold, not buildable until Phase 3/4) environments. pioarduino 55.03.312-1 pinned. `default_envs = cyd`.
- C6 board layer: `board_c6_touch147.h`, `lgfx_c6.h` (LovyanGFX panel with Waveshare's init list), `touch_axs5106l.h`.
- `bringup/c6_bringup.cpp` bring-up sketch 0.2. Results: `docs/port/BRINGUP_RESULTS.md`. Display decision: `docs/port/DISPLAY_DECISION.md`.
- The `cyd` firmware is unchanged from v3.21-hal1.

## v3.21-hal1 — Phase 1: HAL split, no CYD behavior change
- Hardware moved behind `src/hal/` interfaces with a compile-time board layer: `src/boards/cyd/` (display, touch, alert output, pins) and an empty `src/boards/c6_touch147/`.
- `main.cpp` and the scanners no longer reference pins, TFT_eSPI, LEDC or the touch driver. `ui.cpp` no longer touches pins or LEDC.
- `platformio.ini`: platform pinned to `espressif32 @ 6.9.0`, unused `XPT2046_Touchscreen` dependency removed, `-DBOARD_CYD -Isrc` and a `build_src_filter` added.
- Build (cyd): static RAM 85,796 B (unchanged), flash 1,037,985 B (+416 B vs 1,037,569 B).
- Docs: `docs/port/AUDIT.md`, `docs/port/SD_BORROW.md`, `docs/port/CYD_TEST_CHECKLIST.md`.

## v3.21 — baseline
Tag `standalone-v3.21` (LINKS + TRAFF). Last single-board CYD firmware from SignalSweep-CYD.
