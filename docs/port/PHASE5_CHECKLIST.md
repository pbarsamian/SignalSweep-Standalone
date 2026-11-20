# Phase 5 hardware checklist (Paul): alerts, SD, settings, battery

Firmware: `env:c6touch` (v3.21-c6d). Flash: `$env:PLATFORMIO_CORE_DIR='C:\pio'; python -m platformio run -e c6touch -t upload --upload-port COM14`
Serial keys: `1` `2` `3` demo detections (they raise alerts), `v` battery millivolts, `a` print `alerts.csv` from the SD card, `i` heap/stack info.

What Claude verified without a card or a meter: all four envs build (`cyd`, `c6scan`, `c6touch`, `c6touch_buzzer`), the new firmware boots and runs with the scanners, the battery reader returns a value, a failed SD mount (empty slot) does not break the display or the UI, settings load at boot (`[boot] settings: ...`). Nothing below about how it looks, sounds or what lands on the card has been seen.

| # | Check | How | Expected | Result / notes |
|---|---|---|---|---|
| 1 | Alert patterns | Press `1`, `2`, `3` (Penguin = tracker/camera class, Tello = drone, AirTag = tracker) | Header flashes the alert text in the type colour; the content area flashes twice; the backlight pulses a pattern that differs by type (drone 3 quick, tracker 2 medium, camera 1 long, bodycam 2 quick, attack 5 rapid), then returns to your brightness | |
| 2 | Alert flash switch | SETTINGS > Alert flash OFF, press `1` again (wait for the old entry to age out or reset first) | no backlight pulse or content flash (the header text still shows) | |
| 3 | Settings persist | Change a few SETTINGS rows (a type OFF, Alert flash OFF, brightness), power-cycle, read the `[boot] settings:` line | the line matches what you set; the UI shows the same | |
| 4 | Brightness | SETTINGS > Brightness taps through 16/39/67/100 %, keep one, reset | level kept after reset | |
| 5 | Battery reading | `v` on serial, and the % in the header; compare with a multimeter on the battery if you can | mV within a few percent of the meter (Claude saw 4123 mV on the first reading; unverified). Note whether a battery is connected and whether USB is plugged in | |
| 6 | SD mount | Put a FAT32 card in, SETTINGS > Log alerts to SD ON | the row stays ON, STATUS shows `SD ready <size>`; the display keeps drawing normally | |
| 7 | alerts.csv | with logging ON press `1` `2` `3`, then `a` on serial | a CSV header and three rows (boot_ms, type, confidence, ...) between the marker lines; same file on the card under `/signalsweep/` | |
| 8 | List export | SETTINGS > Save list to SD with 3 demo entries | row shows `list_001: 3 saved`; `list_001.csv` and `.json` on the card; MAC column follows the MAC-in-exports row (full / OUI / none) | |
| 9 | Bus sharing | While scanning, trigger several alerts with logging ON and scroll the list | no garbled screen, no touch lockups after SD writes; note any glitch | |
| 10 | Privacy default | Reset the board | Log alerts to SD is OFF again (it is off at every boot by design) | |
| 11 | Buzzer (optional) | only if you fit a passive buzzer on GPIO7 and flash `env:c6touch_buzzer`: SETTINGS > Buzzer, press `1` | tone per type; HUNT clicks | |

Known and expected: no pcapng capture to SD yet (CAP page), no OTA (the CYD firmware has none), IMU not implemented (optional, off by default).

## Results so far (2026-11-20, 7.4 GB SDHC card, board on COM14)
| # | Result |
|---|---|
| 5 | `v` read 4122-4123 mV. Not compared with a meter. |
| 6 | PASS. First attempt failed: LovyanGFX held SPI2 and SD.begin failed at every clock. Fixed with a per-operation bus handover (display_bus_release / restore). Card mounts, screen stays fine. |
| 7 | PASS. `a` printed the header and 3 rows (camera, drone, tracker; conf 80; correct MACs, SSID, channel). |
| 8 | PASS on screen: `list_001: 3 saved`. Files on the card not yet opened on a PC. |
| 9 | Partial: screen fine and touch responsive with SD sessions. Long run with many alerts not done. |
| 1-4, 10 | Not done. Note: Alert flash was OFF on the board (setting saved from an earlier tap); turn it ON for check 1. |
Touch latency (found during this check): UI stalled after 2-3 gestures because serial log lines blocked the UI task once the USB TX buffer filled. Fixed with Serial.setTxTimeoutMs(0); Paul confirmed swipes fast.
