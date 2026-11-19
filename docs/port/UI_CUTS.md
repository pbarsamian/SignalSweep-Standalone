# UI cuts and changes for the C6 build (320x172 landscape, capacitive touch)

Decided by Paul, 2026-11-19. These are Phase 4 inputs. Nothing here is implemented yet. The CYD UI is unchanged.

## 1. Spectrum scope becomes a simple indicator
- **CYD today:** a 320-sample oscilloscope-style scope (`g_scope[2][320]`, per-channel humps for WiFi, three peaks for BLE), bright-top/dim-bottom bars, drawn on the SCOPE tab.
- **C6:** no detailed spectrum graph. A simpler, good-looking indicator (for example a small number of activity bars or a radial/ring gauge showing WiFi and BLE activity and the strongest signal) so the freed screen area goes to detections.
- **Why:** at 172 px tall there is no room for a readable spectrum, and the detail adds little on a handheld.
- **What this touches:**
  - `src/main.cpp` fills 320-sample scope buffers in `scan_task`, and `ui_update_scope()` copies them. The scanners' scope output
    (`scanner_wifi_tick`, `boost_ble_scope`) only exists to feed the display. In Phase 3 keep a small activity summary in the core (per-slice WiFi and BLE
    counts and the strongest RSSI) and let each board's UI decide how to draw it. The CYD keeps its scope unchanged through `ui_update_scope`.
  - Less per-frame work and less RAM for the C6 UI, which helps the heap budget in `BRINGUP_RESULTS.md`.
- **Content cut:** the per-channel spectrum shape. Everything the scope implied that matters (which band is active, how strong) stays available through the indicator and the detection list.

## 2. Swipe between screens instead of a tab bar
- **CYD today:** a 4-tab bar along the bottom (SCOPE, LIST, INFO, SET), 80 px per tab, 34 px tall.
- **C6:** horizontal swipe left and right moves between the screens. No top or bottom menu bar. This frees about 34 to 50 px of height on a 172 px screen.
- **Why it suits this panel:** the measured touch accuracy (about +-5 px X, +-10 px Y with a finger, see `BRINGUP_RESULTS.md`) is poor for small taps but fine for a long swipe, and a 34 px tab bar is only about 3.4 mm tall here.
- **Design points to settle in Phase 4:**
  - Page indicator (dots or a thin strip) so the swipe is discoverable and the user knows where they are.
  - Swipe versus scroll: the list screens scroll vertically, so a swipe is a horizontal-dominant gesture with a minimum travel (suggest 60 px or more) and a direction test; vertical drags stay scrolls.
  - The detail view has six sub-pages (INFO, LINKS, TRAFF, RAW, HUNT, CAP). Swiping inside the detail view could page through those, with a back gesture or a corner button to leave it. Needs a decision.
  - Touch targets inside screens stay at least about 50 px (5 mm).
- **Implementation note (not yet verified):** the touch driver reports points frame by frame while a finger is down (INT-driven, repeats were seen at roughly 10 to 20 ms in the bring-up capture),
  so a swipe can be recognised from the first and last point of a touch. The recogniser belongs in the board's touch layer or a small shared gesture module, and `touch_get()` stays the
  point API. A press/move/release event stream is probably needed instead of the CYD's single "is touched" bool. That is an interface change to `hal/touch.h` for the C6 only.
- The CYD keeps its tab bar. The layout abstraction in Phase 4 selects tab bar versus swipe pages from the board capabilities.

## 3. Touch target size (from the hardware test)
Minimum about 50 px (5 mm) on the C6, against the CYD's 34 px bars. Applies to buttons, list rows and any remaining controls.

## Other C6 cuts already known
- No speaker, RGB LED or LDR: the alert and hunt-mode outputs become backlight pulses, screen flashes and the optional GPIO7 buzzer (open question Q2 in `AUDIT.md`).
