# Phase 4 hardware checklist (Paul): the C6 UI

Firmware: `env:c6touch` (scanners + touch UI). Flash from PowerShell, board on COM14:
`$env:PLATFORMIO_CORE_DIR='C:\pio'; python -m platformio run -e c6touch -t upload --upload-port COM14`
Serial monitor (115200): keys `1`, `2`, `3` inject demo detections (BLE Penguin name, WiFi `TELLO-TEST01` beacon, Apple tracker advert) so the screens have content without a transmitter; `i` prints heap, stacks, queues and timing. Demo entries age out after 2 minutes.

**Claude could not see the screen.** What was verified without eyes: it builds, boots, the sprites allocate, the scanners and UI run together for 30 s with demo inputs and no reset. Everything below about how it looks and feels needs your eyes.

Navigation: swipe left/right (60 px or more, mostly horizontal) between HOME, DETECTIONS, STATUS, SETTINGS. Tap a row on DETECTIONS to open the detail view; inside it swipe left/right between INFO, LINKS, TRAFFIC, HUNT; swipe right on INFO or tap `< BACK` (top-left) to leave. Drag up/down to scroll lists.

| # | Check | Expected | Result / notes |
|---|---|---|---|
| 1 | Boot | HOME shows within a few seconds: a ring that turns, "0 clear", WiFi/BLE bars on the right moving as slices alternate, header "HOME", 4 page dots, "0 ALERTS" | |
| 2 | Swipe between pages | Each swipe moves exactly one page, no accidental double swipes, page dots follow. How often does a swipe fail to register or register as a tap? | |
| 3 | Demo detections (keys 1,2,3) | Header flashes the alert text in the type colour for ~4 s, then shows "3 ALERTS"; ring on HOME fills with 3 coloured segments and "3 found"; "last alert" line on HOME | |
| 4 | DETECTIONS list | 3 rows, ~50 px each, name, MAC, RSSI, signal bars, type colour stripe. Tap a row: opens detail for that row (not its neighbour). Note which row you meant vs which opened | |
| 5 | Detail INFO | Rows scroll by dragging; values readable; back by `< BACK` and by swiping right | |
| 6 | Detail LINKS / TRAFFIC / HUNT | pages reachable by swiping; HUNT ring pulses faster with stronger RSSI (it is visual only: no sound or LED yet) | |
| 7 | SETTINGS | 6 buttons (5 types + brightness), each tap toggles/steps once; type OFF removes that type's list entries; brightness changes and survives a reset | |
| 8 | Readability | Is Font 2 text (16 px) readable at arm's length? Anything too small or cramped? | |
| 9 | Touch accuracy | Any button/row you had to tap more than once? Where on the screen (top/bottom/edges)? | |
| 10 | Flicker / tearing | Any visible flicker on HOME ring, list scroll, or hunt ring? | |
| 11 | STATUS | Version `v3.21-c6c`, heap, counters update about once a second | |

Known and expected for this phase: no sound/LED/backlight alerts (Phase 5), no SD, no battery, no RAW or CAP detail pages (see `UI_CUTS.md`).

## Swipe reliability (2026-11-19, on Paul's unit)
First report: swipes "hit and miss". After adding a per-gesture serial log (`[ui] gesture ...`) and polling touch every ~5 ms instead of at the drawing rate, two captures: run 1 logged 7 gestures (5 swipes recognised, 1 tap, 1 short vertical drag); run 2 logged 10 of 10 swipes recognised (225 to 255 px travel, 17 to 26 touch frames each, 350 to 440 ms, vertical drift up to 51 px). Paul: "much better now".
What was not separated: the first run's misses left no log line, so whether they were missed touches, wrong-direction swipes at the first/last page (they do nothing; no wrap), or technique is unknown. Both runs used long swipes; short swipes (about 60 to 100 px) are untested. Keep the log in; it costs a serial line per gesture.
