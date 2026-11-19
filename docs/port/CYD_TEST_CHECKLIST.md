# CYD manual test checklist — v3.21-hal1 (Phase 1)

I could not test on hardware. The build compiles and the pin/driver greps are clean (see the PR text), but nothing below has been run on a board. Flash `v3.21-hal1`, watch the serial monitor at 115200, and report which items fail.

| # | Test | Expect |
|---|---|---|
| 1 | Boot | Serial prints `[SignalSweep CYD v3.21-hal1] boot`, `[ui] init w=320 h=240 ...`, then `[ble] init OK`. Header shows SIGNALSWEEP, "2.4G+BLE", alert badge. |
| 2 | Colours and orientation | Landscape, antenna right. Red looks red, green looks green (no purple scope). Same as v3.21. |
| 3 | Backlight | Screen lit at the saved brightness. SET > brightness still changes it. |
| 4 | Touch | Tap each of the 4 tabs: SCOPE, LIST, INFO, SET. Taps land where you touch, not mirrored. Serial shows `[touch] raw=...`. |
| 5 | WiFi slice | Scope shows humps on WiFi channels 1-11 during the WiFi slot. Phone hotspot `TELLO-TEST01` (2.4 GHz) alerts within about 12 s. |
| 6 | BLE slice | Serial alternates `[slot] BLE` / `[slot] WiFi` every 6 s. BLE adverts counted in `[stats 5s]`. |
| 7 | Alert beep | On a new alert: one 150 ms tone, pitch by type. LED flashes the type colour for 200 ms, then off. |
| 8 | Boot LED | RGB LED stays off during boot and idle. |
| 9 | Settings persistence | Toggle sound off, LED off, change brightness, reboot: the settings stay. |
| 10 | Hunt mode | Tap a detection, HUNT sub-tab: clicks, ring pulse and LED speed up as RSSI rises, silent after 8 s. Leaving HUNT or the tab silences everything. |
| 11 | Focus, LINKS, TRAFF | Tap a detection: sweep stops, channel locks. LINKS and TRAFF fill in, and tapping a peer pivots. |

Note: speaker setup (LEDC ch1) now runs in `setup()` via `alert_init()` instead of at the start of the UI task. This is the one ordering change in this phase, and test 7 and test 10 cover it.
