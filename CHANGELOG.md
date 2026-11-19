# Changelog

## v3.21-hal1 — Phase 1: HAL split, no CYD behavior change
- Hardware moved behind `src/hal/` interfaces with a compile-time board layer: `src/boards/cyd/` (display, touch, alert output, pins) and an empty `src/boards/c6_touch147/`.
- `main.cpp` and the scanners no longer reference pins, TFT_eSPI, LEDC or the touch driver. `ui.cpp` no longer touches pins or LEDC.
- `platformio.ini`: platform pinned to `espressif32 @ 6.9.0`, unused `XPT2046_Touchscreen` dependency removed, `-DBOARD_CYD -Isrc` and a `build_src_filter` added.
- Build (cyd): static RAM 85,796 B (unchanged), flash 1,037,985 B (+416 B vs 1,037,569 B).
- Docs: `docs/port/AUDIT.md`, `docs/port/SD_BORROW.md`, `docs/port/CYD_TEST_CHECKLIST.md`.

## v3.21 — baseline
Tag `standalone-v3.21` (LINKS + TRAFF). Last single-board CYD firmware from SignalSweep-CYD.
