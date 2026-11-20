
## v3.21-c6d follow-ups (SD check, touch latency)
- SD: LovyanGFX holds SPI2, so SD.begin failed at every clock with the display running (the same card mounted in the scanner-only build). Every SD operation is now a session: display_bus_release, mount, I/O, unmount, display_bus_restore.
- Touch: page swipes now fire mid-gesture at 60 px of travel; release timeout 120 to 70 ms (taps only).
- Touch lag found on hardware: after 2-3 gestures the UI stalled. Cause: serial log lines blocked the UI task once the USB TX buffer filled with no host reading. Fix: Serial.setTxTimeoutMs(0). Verified by Paul: swipes fast. Rule: no task may block on serial output.
- Diagnostics: serial 'i' prints ui_update/ui_touch timing; page-change latency line; c6scan key 'd' (SD mount test).
