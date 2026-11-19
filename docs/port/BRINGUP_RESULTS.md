# Phase 2 bring-up results — Waveshare ESP32-C6-Touch-LCD-1.47

Run on Paul's board, 2026-11-19, `c6bringup` env, bring-up sketch versions 0.1 and 0.2, read over USB Serial/JTAG (COM14).
Toolchain: pioarduino platform 55.03.312-1 = Arduino 3.3.12 on ESP-IDF 5.5.5 (`sdk=v5.5.5` printed by the board).

## Chip
| Item | Result |
|---|---|
| Chip | ESP32-C6, revision v0.2, 1 core, 160 MHz, WiFi 6 + BT 5 + 802.15.4 (esptool `chip_id`) |
| Flash | 8 MB (esptool `flash_id`: manufacturer 0x20, device 0x4017; sketch reads `flash=8388608`) |
| PSRAM | none (`psram=0`) |
| Partitions | `default_8MB.csv`: app0 and app1 0x330000 each, spiffs 0x180000, coredump 0x10000, nvs, otadata. Read from the framework's `partitions/default_8MB.csv` |
| Bring-up image | 1,295,696 B (app slot 3,342,336 B) |

## Heap (internal RAM only, no PSRAM)
| Point | Free | Largest block |
|---|---|---|
| boot | 340,052 | 311,284 |
| after display init | 334,928 | 311,284 |
| after SD mount attempt | 334,428 | 311,284 |
| WiFi init only | 294,096 (-40 KB) | 270,324 |
| BLE (NimBLE 2.5.2) init only | 300,036 (-34 KB) | 278,516 |
| WiFi and BLE both | 259,944 (-75 KB) | 241,652 |

Reading: with both radios initialised, about 260 KB free and a 241 KB largest block remain before any SignalSweep
data. A full 320x172x16-bit frame is 110,080 B, so one fits, but it would take 45% of the largest block. Phase 4 should
still choose banded rendering unless Paul decides otherwise; the registry (about 25 KB live plus the snapshot copy) and
the Phase 5 SD buffers also need room. Heap while the scanners are running and the UI is drawing is not measured yet.

## I2C (SDA 18, SCL 19)
Found `0x63`, `0x6B`, `0x7E`. `0x63` is the AXS5106L touch controller (its ID register read OK). Waveshare's FAQ lists `0x51`, `0x6B`, `0x7E`;
**`0x51` was not present on this board**, and `0x63` was not on their list. `0x6B` and `0x7E` match their list; I did not identify which parts they are.

## Display (LovyanGFX 1.3.0 + in-repo panel class, rotation 1, 320x172)
Reported by Paul from looking at the screen: the 1-px border is complete on all four sides and the colour bars are in the correct order
(red, green, blue, yellow, cyan, magenta). So the column offset of 34, the init sequence and the inversion setting are right. Text
direction and orientation: Paul confirmed on the second run that orientation and text are correct (readable, not mirrored).

## Touch (guided 5-point test at rotation 1)
| Target | Screen | Raw x, y |
|---|---|---|
| upper-left | 40,30 | 34,29 |
| upper-right | 279,30 | 42,306 |
| lower-right | 279,141 | 147,311 |
| lower-left | 40,141 | 139,43 |
| centre | 160,86 | 99,181 |

Second run (every run in this section was a finger touch; Paul reports that none of his styluses register on this capacitive panel):

| Target | Screen | Raw x, y (run 1) | Raw x, y (run 2) |
|---|---|---|---|
| upper-left | 40,30 | 34,29 | 48,39 |
| upper-right | 279,30 | 42,306 | 66,310 |
| lower-right | 279,141 | 147,311 | 137,296 |
| lower-left | 40,141 | 139,43 | 141,45 |
| centre | 160,86 | 99,181 | 103,179 |

Derived both times: **screen X = raw Y, screen Y = raw X, neither inverted.** This is the same as the rotation-1 case in Waveshare's touch library.

Accuracy: the same target moved by up to 24 raw units between the two runs (upper-right raw x 42 vs 66), which is as large as the offsets
from the targets (up to 31 px in X, 36 px in Y on single taps). The averaged offsets grow from about 0 at the left edge to about +20 at the centre and +27 at the
right, and are about +17 at the top and 0 at the bottom, but with this run-to-run spread that is not distinguishable from tap placement.
Scale: 1.47 inch diagonal over a 172x320 grid is about 0.1 mm per pixel (derived from the product's diagonal, not measured), so 30 px is about 3 mm,
which is ordinary fingertip placement error. (That was the first reading; the repeated runs below superseded the "no correction" conclusion for the X axis.)
**Phase 4 consequence:** touch targets should be at least about 50 px (5 mm). The CYD's 34 px tab bar is about 6 mm on its 2.8 inch panel
but only about 3.4 mm on this one, so the C6 layout needs a taller tab bar.

### Repeated runs and the fitted correction
(All finger touches. An earlier version of this note called some runs "stylus" in error; no stylus registered on this panel.)
Four runs (the last on sketch 0.3) gave 4 sets of the same 5 targets. Sketch 0.3 also fitted a scale and offset per axis from
its 5 taps and then scored it on 5 targets the fit had not seen:

| | mean abs error, X | mean abs error, Y |
|---|---|---|
| unseen targets, uncorrected (raw as screen) | 15.8 px | 13.2 px |
| unseen targets, after the 5-point fit | **8.0 px** | **9.2 px** |

Pooled least-squares over all runs (25 points: 4 x 5 calibration taps and the 5 verification taps):
**screenX = 0.894 x rawY + 6.3**, **screenY = 1.051 x rawX - 5.0**.
- **X is stable:** the gain was 0.881 to 0.899 across fits of different run subsets, and leaving one run out gave 3.7 to 6.5 px mean error on it.
  So the controller reports about 1.12 raw units per screen pixel along X; the old "no correction" reading was wrong for X.
- **Y is noisier:** leaving one run out gave 5.6 to 14.1 px. The same crosshair moved by up to 24 raw units between runs, and all five verification taps
  landed 4 to 17 px above their targets. Fingertip placement is the likely cause of the scatter (a finger pad is several millimetres wide, the panel is about 0.1 mm per pixel),
  and a constant offset could come from how the finger or the viewing angle covers the crosshair; neither is tested. The fitted Y constants include that bias.
- Applied in `board_c6_touch147.h` as `TOUCH_SX_A/B`, `TOUCH_SY_A/B`. Expect roughly +-5 px in X and +-10 px in Y. With the Phase 4 touch targets of at least 50 px that is
  adequate; a larger sample (or taps made looking straight down at the panel) would tighten Y.

## SD / shared bus
`sd mount FAILED (no card, or not FAT32)`. Paul confirmed there is **no card in the slot yet**, so the failure is expected and this is not a result. The panel redraw
after the SD attempt worked (the pattern was shown again), which only shows an empty-slot mount attempt did not break the display.
NEEDS HARDWARE TEST with a FAT32 card: mount, listing, and display redraw after real SD traffic.

## Other findings
- `wifi:failed to post WiFi event=43 ret=259` at WiFi init: no ESP-IDF default event loop exists. WiFi still initialised. The Phase 3 scanner should
  call `esp_event_loop_create_default()` first. (Cause inferred from the error code, not tested.)
- `NimBLE-Arduino 2.5.2` compiled and initialised on the C6. The standalone scanner uses the 1.4 callback API, so Phase 3 must port it.

## Build environment notes (Windows corporate machine)
- Windows long paths are off (`LongPathsEnabled=0`). Unpacking pioarduino's `esp32-arduino-libs` overflowed 260 characters under the default `~\.platformio`.
  Fix without admin rights or security changes: a short PlatformIO home, `PLATFORMIO_CORE_DIR=C:\pio`, for the C6 envs only.
- `idf_tools.py` refuses to run under MSys/Git Bash ("MSys/Mingw is not supported"), so the C6 build must be run from **PowerShell**, not Git Bash.
- pioarduino fetches from GitHub worked on this network, no certificate changes needed.
- Command: `$env:PLATFORMIO_CORE_DIR='C:\pio'; python -m platformio run -e c6bringup -t upload --upload-port COM14`
