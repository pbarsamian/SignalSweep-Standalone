# Changelog

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
