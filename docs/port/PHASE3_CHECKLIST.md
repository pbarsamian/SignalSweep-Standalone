# Phase 3 hardware checklist (Paul)

Board: Waveshare ESP32-C6-Touch-LCD-1.47 on USB. Firmware: `env:c6scan`, serial 115200 on the board's USB port (a terminal such as `pio device monitor`).
Flash with (PowerShell, not Git Bash): `$env:PLATFORMIO_CORE_DIR='C:\pio'; python -m platformio run -e c6scan -t upload --upload-port COM14`
Type `h` for the command list. `[match]` lines show every scored match: rule (`#n` = table index, `protocol` = built-in detector), score, outcome.

| # | Test | How | Pass looks like | Result |
|---|---|---|---|---|
| 1 | Boot and radios | Reset the board, watch the first 20 s | `[boot] default event loop: created`, `[ble] init OK`, `[wifi] init OK`, no `failed to post WiFi event`, slot lines alternating BLE / WiFi every 6 s | _(Claude saw this)_ |
| 2 | Synthetic inputs | Commands `1`, `2`, `3` | each prints `[match] ... NEW ALERT` and `[ALERT]` | _(Claude saw this)_ |
| 3 | Real WiFi positive | Phone hotspot named `TELLO-TEST01`, 2.4 GHz (iPhone: Maximize Compatibility; Android: 2.4 GHz band). Wait up to 12 s | `[match] WiFi rule=#93 'DJI Tello drone (AP)' ... NEW ALERT` with the phone's MAC | |
| 4 | Real BLE positive | A tracker you own (AirTag away from its owner for a while, Tile, Samsung SmartTag, or a Google tracker) | `[match] BLE ... Tracker` with that device | |
| 5 | Sweep stays quiet | Leave it 10 minutes with `v` (status lines) on | no reboot, `qdrop` stays 0 or tiny, `[heap] min` stable, no false alerts from your own phone or watch | _(Claude's 10-minute soak is below, not a field test)_ |
| 6 | Focus on a real WiFi target | After test 3: `f`, wait 20 s, `i` | `[focus] WiFi ch<n> frames=<>0 ...` growing; `g` ends it and the sweep resumes | |
| 7 | Focus on a real BLE target | After test 4: `F`, wait 20 s | `[focus] BLE ... adverts=<>0` growing | |
| 8 | Frame types | `w`, then `y`, wait 15 s, `i`, `y`, `s` | the `[types]` line. Claude's run saw mgmt and data only (ctrl=0, misc=0) | |

Please report any `[match]` with a rule that looks wrong, any reboot, and anything where `mac order check` says MISMATCH.
