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
direction and orientation (TL top-left, BR bottom-right) were asked for but not reported; not confirmed.

## Touch (guided 5-point test at rotation 1)
| Target | Screen | Raw x, y |
|---|---|---|
| upper-left | 40,30 | 34,29 |
| upper-right | 279,30 | 42,306 |
| lower-right | 279,141 | 147,311 |
| lower-left | 40,141 | 139,43 |
| centre | 160,86 | 99,181 |

Derived: **screen X = raw Y, screen Y = raw X, neither inverted.** This is the same as the rotation-1 case in Waveshare's touch library.
Residual error of the five taps against their targets: about +-13 px in Y and +-30 px in X. NOT established whether this is finger placement or a
scale or offset to correct. The sketch's free-draw dots can show it; waiting on Paul's report.

## SD / shared bus
`sd mount FAILED (no card, or not FAT32)`. Whether a card was in the slot was asked and **not answered**, so this is not a result yet. The panel redraw after the
SD attempt worked (the pattern was shown again), which only shows the failed mount did not break the display.
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
