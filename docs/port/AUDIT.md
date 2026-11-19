# Port Audit (Phase 0) — SignalSweep standalone → Waveshare ESP32-C6-Touch-LCD-1.47

Status: Phase 0, read-only. No source file was modified.
Baseline: tag `standalone-v3.21` (commit `003a6bc`, "v3.21a: LINKS + TRAFF"), the tip of the old
`SignalSweep-CYD` `standalone` branch. This is the real baseline, not provisional. It is the last
single-board CYD firmware, with no C5 code.
Audited tree: this repo at `003a6bc` on branch `port/c6-touch-lcd-147`.
Method: every claim below comes from reading the source or from the build log. Anything I could not
confirm is marked `UNVERIFIED`. Anything needing a board is marked `NEEDS HARDWARE TEST`.

---

## 0. Findings that change the plan

1. **The CYD UI is landscape, 320x240. The C6 panel is portrait, 172x320.** `SCREEN_W=320, SCREEN_H=240`
   (`include/board_pins.h:46-47`), `tft.setRotation(3)` (`src/ui.cpp:1259`). The 4 tabs are 80 px each,
   the scope buffers are 320 samples, and the layout constants hang off `SCREEN_W/H`. Phase 4 is therefore a
   re-layout, not a rescale. See section 3.4.
2. **There is no SD code in v3.21.** The only mention is a UI string ("Needs session history on SD (planned)",
   `src/ui.cpp:818`). SD capture and logging were built later on the split `main`, which is out of scope.
   Consequences: the `Storage` HAL has no existing code to move. The shared LCD/SD SPI-bus problem
   (manifest risk 5) does not apply until Phase 5 adds SD logging. Manifest Phase 5 item 2 becomes new
   code, not a port.
3. **Nothing reads the LDR.** `PIN_LDR` is defined (`board_pins.h:31`) and never used. The `analogRead` grep over `src/` and
   `include/` finds nothing. No auto-brightness exists, and no GPIO27 `analogRead` exists.
4. **The build reaches the network and works on this machine.** A clean `python -m platformio run -e cyd`
   succeeded here (details in section 2). That makes the unverified pioarduino fetch the main remaining
   toolchain risk.
5. **`platform = espressif32` is unpinned.** It resolved to `framework-arduinoespressif32 @ 3.20017.241212`,
   which is arduino-esp32 **2.0.17**, and `toolchain-xtensa-esp32 @ 8.4.0+2021r2-patch5`. The C6 needs
   arduino-esp32 3.x, so the Arduino-API and IDF-API surface changes between 2.0.x and 3.x
   (LEDC, `esp_wifi`, NimBLE-Arduino version). See risk R3.
6. **`CLAUDE.md` says "all changes straight to main, no branches".** The manifest says use a branch. For
   this repo, the manifest wins for the port work. This is recorded as a decision, not an open question.
7. **Version discipline is three-place.** `CLAUDE.md` requires a lockstep bump in `src/ui.cpp` line 1 and its settings
   footer, `src/main.cpp` line 1 and its boot Serial line, and `platformio.ini` line 1. The manifest's `<baseline>-hal1` labels
   have to follow this rule. A single `FW_VERSION` define would remove the triple edit (Phase 1 candidate).

---

## 1. CYD / ESP32-classic dependency inventory

### 1.1 Pins and board constants (`include/board_pins.h`, all lines)

| Item | Where | What | C6 replacement |
|---|---|---|---|
| Display SPI pins MISO12 MOSI13 SCLK14 CS15 DC2 RST-1 BL21 | `board_pins.h:7-13`, duplicated in `config/User_Setup.h:15-21` | ILI9341 on HSPI. Two sources of truth: TFT_eSPI reads `User_Setup.h`, the rest of the code reads `board_pins.h`. | LCD SCK=1 MOSI=2 CS=14 DC=15 RST=22 BL=23 (manifest 2.2, from Waveshare docs). No MISO is used for the LCD. |
| Touch XPT2046 MOSI32 MISO39 CLK25 CS33 IRQ36 | `board_pins.h:16-20`, `touch.h` | Resistive touch on its own VSPI bus | AXS5106L over I2C SDA=18 SCL=19, RST=20, INT=21 |
| RGB LED active-LOW R4 G16 B17 | `board_pins.h:23-25` | `main.cpp:159-161` init off, `ui.cpp:580-587`, `1365-1367` | None on the C6. `AlertOutput` decides (backlight pulse, optional buzzer). |
| Speaker GPIO26 (LEDC ch1) | `board_pins.h:28`, `ui.cpp:1252-1254` | Alert beeps and hunt-mode clicks | None. Optional passive buzzer on GPIO7 (`C6_HAS_BUZZER`). |
| LDR GPIO34 | `board_pins.h:31` | Defined, unused | Drop. `Board` caps `has_ldr=false`. |
| SD pins (VSPI) | `board_pins.h:34-37` | Defined, unused in v3.21 | C6 TF: SCK=1 MOSI=2 MISO=3 CS=4, shared with the LCD (Phase 5). |
| `SCREEN_W/H` 320/240 | `board_pins.h:46-47` | Landscape. Used throughout `ui.cpp` | `Board` caps 172x320 portrait. |
| Touch calibration `TOUCH_X/Y_MIN/MAX` | `board_pins.h:50-53` | Raw to screen mapping, both axes inverted (`touch.h:42-43`) | None needed. Axis swap/invert becomes a board constant. |
| `TONE_*` frequencies, `TONE_MS` | `board_pins.h:56-61` | Alert tones per threat type | Stay in core or board data. A buzzer path reuses them. |

### 1.2 Display (TFT_eSPI)

- Library: `bodmer/TFT_eSPI @ ^2.5.43` (installed 2.5.43). `include/` has no sprite code. `src/ui.cpp:2` says "no sprites", and
  `grep Sprite|createSprite` finds nothing. All drawing is direct `tft.fillRect`, `drawString`, etc.
- Config: `config/User_Setup.h` with `ILI9341_2_DRIVER`, `USE_HSPI_PORT`, SPI 55 MHz write and 20 MHz read. It reaches the library by
  `extra_scripts = pre:copy_setup_pre.py`, which copies the file into `.pio/libdeps/*/TFT_eSPI/User_Setup.h`.
  The copy is skipped silently on the very first build, when TFT_eSPI is not downloaded yet. A second build is then needed
  to get the right config. This is a latent footgun (the audit here only observed a build, not that sequence; derived from `copy_setup_pre.py:6-12`).
- `tft.` calls are not isolated: **all of them are in `src/ui.cpp`**, which also owns touch handling,
  alert output (tones, LED) and backlight setup. No `tft.` calls exist in scanners or `main.cpp`.
- Panel fixes in code: `tft.setRotation(3)` then `writecommand(0x36); writedata(0xE8); invertDisplay(true)`
  (`ui.cpp:1259-1264`). These are ILI9341-specific and do not carry to the JD9853.
- Backlight: LEDC ch0, 5 kHz, 8-bit, `ledcSetup/ledcAttachPin` (`ui.cpp:1246-1248`). **This is the arduino-esp32 2.x API.**
  3.x replaced it with `ledcAttach(pin, freq, res)` and `ledcWrite(pin, duty)`. `ledcWriteTone(1, ...)`
  (`ui.cpp:603`, `1351`) also changes. UNVERIFIED against the 3.x headers, to be checked in Phase 2.

### 1.3 Touch

- Inline XPT2046 driver in `include/touch.h`, a `static SPIClass touchSPI(VSPI)`, 3-sample average per axis,
  IRQ-pin pre-check, noise reject `<100`. It prints `[touch] raw=...` to Serial on **every** touch.
- Consumers of touch coordinates: `handle_touch` and `ui_update` in `ui.cpp` (`1135` tab hit-test `tx / TAB_W`,
  `1149` scroll strip `tx >= SCREEN_W-16`, `1182` sub-tab width `SCREEN_W / DP_COUNT`, `1194` mute button
  `inRect(SCREEN_W-72, PAGE_Y, 70, 22)`, `1207` detail list strip). Edge-triggered through `s_was_touched`.
  All hit-tests are in `ui.cpp` and all assume landscape 320x240.

### 1.4 SPI buses

- Display on HSPI (via `USE_HSPI_PORT` in TFT_eSPI), touch on `SPIClass(VSPI)`. There are **two independent buses**
  and the code relies on that: no arbitration exists, because touch reads and display writes run on the same
  task (`ui_task`) and never overlap anyway. The C6 has one shared bus for LCD and TF. The touch moves to I2C,
  so that part disappears.

### 1.5 Tasks, cores, priorities (`src/main.cpp`)

| Task | Core | Priority | Stack | Does |
|---|---|---|---|---|
| `scan_task` | 0 | 1 | 8192 | Radio slot switching, BLE init, scope update, registry prune, 5 s stats |
| `ui_task` | 1 | 1 | 12288 | UI init, registry snapshot, touch, drawing, alert queue to `ui_alert` (which beeps and blinks inside the UI task via `vTaskDelay`, `ui.cpp:1351-1353`) |
| `loop()` | (Arduino default) | | | `vTaskDelay(1000)` only |
| NimBLE host task | (NimBLE-Arduino internal) | | | Runs `SweepCallback::onResult`, which does rule matching in the callback |
| Wi-Fi task | (IDF internal) | | | Runs `promisc()`, which parses frames and runs rule matching in the callback |

- Synchronization: `g_reg_mutex` (registry), `g_queue` (16 ints, alert indices), `focus.cpp` has its own mutex `s_mux`, plus
  `volatile` flags `g_scope_w`, `g_ble_slot_active`. **No `portENTER_CRITICAL`, no spinlocks anywhere.**
- **Answer to manifest risk 2:** the design uses two cores but does not depend on them for correctness. All shared state is
  mutex- or queue-guarded, and both tasks are the same priority. On one core they time-slice. The risk is latency, not
  correctness: the heavy matching runs inside the BLE and Wi-Fi callbacks today (they take `g_reg_mutex` with a 5 ms
  timeout, `signatures.cpp:240`, and **drop the detection if busy**, counted as `post_lock`). On a single core, a long UI
  frame holding the mutex or starving the radio task makes that drop more likely. Phase 3 moves matching out of the callbacks.
- `scan_task` uses `vTaskDelay` of 40-50 ms per loop as its own pacing (`scanner_wifi.cpp:339`, `main.cpp:123`).

### 1.6 Wi-Fi promiscuous

- `esp_wifi_init(WIFI_INIT_CONFIG_DEFAULT)`, storage RAM, mode NULL, filter MGMT (DATA added in focus), callback
  `promisc(void*, wifi_promiscuous_pkt_type_t)` (`scanner_wifi.cpp:117`), channels 1-11 hopped from `scanner_wifi_tick`
  (40 ms per tick), `esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE)`.
- Parses: OUI rules, SSID prefix, vendor IEs (Flock/Lite-On, Remote ID `FA:0B:BC` / `90:3A:E6`, DJI DroneID `26:37:12`),
  NAN action frames via `odid_wifi.c`, WPS, RSN/WPA security, HT streams, country.
- The callback is **not** minimal: it walks IEs, runs rule tables and calls `post_detection`.
- The vendored OpenDroneID files (`opendroneid.*`, `odid_wifi.*`, Apache-2.0) must stay unmodified (`CLAUDE.md`).
- UNVERIFIED for C6: the same `esp_wifi` calls compile under IDF 5.x, and the C6 delivers the same frame types.

### 1.7 BLE

- NimBLE-Arduino `h2zero/NimBLE-Arduino @ ^1.4.1` (installed **1.4.3**) plus `-DCONFIG_BT_NIMBLE_ENABLED=1`. Uses the 1.4 API:
  `NimBLEAdvertisedDeviceCallbacks`, `onResult(NimBLEAdvertisedDevice*)`, `setAdvertisedDeviceCallbacks(&cb, true)`,
  `scan->start(0, nullptr, false)`. Active scan, interval 100, window 50 (`scanner_ble.cpp:303-307`), `setMaxResults(0)`.
  Focus raises the window to 100.
- NimBLE-Arduino 2.x changed this callback API. Whether 1.4.3 builds against arduino-esp32 3.x on the C6 is `UNVERIFIED`
  (risk R3). Do not assume either direction.

### 1.8 Time-sliced alternation (`src/main.cpp`)

- `BLE_SLOT_MS` and `WIFI_SLOT_MS` are both 6000, as file-local `#define`s (`main.cpp:38-39`). Loop in `scan_task`.
  `radio_to_wifi()` stops the BLE scan, then `esp_wifi_set_promiscuous(true)` (Wi-Fi is initialised lazily the first
  time). `radio_to_ble()` turns promiscuous off and restarts the scan. BLE is initialised once at task start and never
  deinitialised. Wi-Fi is initialised once and never deinitialised either.
- Note `ui.cpp:190` re-derives the slot from `millis()/6000` independently to draw it. That is a hidden coupling to
  the same 6000 constant, and it is wrong whenever focus mode shifts the slot phase. Phase 3 should expose the
  slot state and a named `SCAN_SLICE_MS` and stop the UI recomputing it.
- Focus mode stops the sweep, locks the channel or runs BLE at full duty, and widens the Wi-Fi filter to data.

### 1.9 Storage and settings

- SD: not used (finding 2).
- Settings: **NVS via `Preferences`**, namespace `"ss"`, keys `drone tracker camera bodycam attack spk led brt`
  (`include/settings.h`). The header includes `<Preferences.h>` and is included by core files, so settings are not
  hardware-independent as written. Per the manifest (Phase 5 item 3), NVS is already used, so it stays NVS on the C6. A
  migration note is not needed because there is no format change.
- No LittleFS or SPIFFS. Partition `huge_app.csv` (3 MB app, no OTA, no FS). `board_build.flash_size = 4MB`.

### 1.10 Audio

- `ledcSetup(1, 2000, 8)`, `ledcAttachPin(PIN_SPEAKER, 1)`. Alert: one 150 ms tone, frequency by `ThreatType`
  (`ui.cpp:1344-1353`: DRONE 1200, TRACKER 800, CAMERA 600, BODYCAM 440, ATTACK 1800). Hunt mode: 3 kHz clicks
  (`ui.cpp:603-606`). Gated by `cfg.speaker_en`.

### 1.11 RGB LED

- Alert: per-type colour for the alert, then off (`ui.cpp:1365-1367`). Hunt: LED tracks RSSI (`ui.cpp:580-587`).
  Gated by `cfg.led_en`. The exact colour-per-type table is in `ui.cpp` near line 1355 and is the thing `AlertOutput`
  must abstract.

### 1.12 Orientation

- Current: **landscape, rotation 3, 320x240**, set in `ui_init` (`ui.cpp:1259`) with MADCTL `0xE8` and inversion on. The
  CLAUDE.md history records this as the end of a long colour/rotation debugging saga. The C6 build must not inherit any
  of it.

### 1.13 Other

- OTA: none. `Serial` at 115200 on UART0. On the C6 console moves to USB Serial/JTAG so GPIO16/17 stay free.
- `XPT2046_Touchscreen` is listed in `lib_deps` but never included: the inline `touch.h` replaced it. It is a dead
  dependency, and a GitHub clone at build time (it was fetched during this audit's build). Removing it is a
  harmless Phase 1 cleanup, but it changes `platformio.ini`, so the manifest's "builds exactly as before" rule
  needs Paul's call (open question Q4).
- C5/UART/Mesh code: **none present** in this baseline. Out-of-scope code does not intertwine.

---

## 2. Measurements (no hardware)

Clean `python -m platformio run -e cyd` from `003a6bc`, 2026-11-19, Windows, corporate network:

| Metric | Value |
|---|---|
| Result | SUCCESS, 88.9 s (first build, including library and platform downloads) |
| Flash | **1,037,569 B** of 3,145,728 B (33.0%) |
| Static RAM | **85,796 B** of 327,680 B (26.2%) |
| Framework | `framework-arduinoespressif32 @ 3.20017.241212+sha.dcc1105b` (arduino-esp32 2.0.17) |
| Toolchain | `toolchain-xtensa-esp32 @ 8.4.0+2021r2-patch5` |
| Libraries | TFT_eSPI 2.5.43, NimBLE-Arduino 1.4.3, XPT2046_Touchscreen 1.4.0 (`sha.f956c5d`, unused) |

These are the Phase 1 regression reference numbers. Environment name `cyd` already exists, so the
manifest's rename step is a no-op.

Sprite budget on paper:

| | 240x320 (CYD native) | 172x320 (C6) |
|---|---|---|
| Sprites alive today | **0** (direct rendering) | n/a |
| One full-frame 16-bit buffer | 153,600 B | 110,080 B |
| Static data worth noting | `DetectionRegistry` is 32 `Detection` plus 32x60 B history, held twice (live `g_reg` + the UI's `static snap`, `main.cpp:156`); exact size not measured (would need `sizeof` on a host or target build) | |

The C6 has 512 KB HP SRAM and no PSRAM (manifest 2.2). Wi-Fi + BLE together will take a large share of it. A full
frame buffer is not affordable by default, so Phase 4 uses banded rendering (as the manifest says). Real free-heap
numbers come from the Phase 2 bring-up. `NEEDS HARDWARE TEST`.

---

## 3. File classification

| File | Class | Notes |
|---|---|---|
| `src/main.cpp` | **mixed: PORTABLE + HAL** | Scan scheduling and slot logic are core. The LED init at boot (`:159-161`), `board_pins.h`, task core IDs and `Serial` setup are board-specific. Split. |
| `src/ui.cpp` (1369 lines) | **mixed: UI + HAL + PORTABLE** | The biggest split. Contains the display init and panel fixes, backlight and speaker LEDC setup, alert output (tone/LED), touch handling, the registry-mutating actions (`purge_type`, pivot), and all layout. |
| `include/touch.h` | HAL | XPT2046 specific. |
| `include/board_pins.h` | HAL | Replace with per-board headers. |
| `config/User_Setup.h`, `copy_setup_pre.py` | HAL (cyd only) | TFT_eSPI config. Must stay `cyd`-only. |
| `include/settings.h` | PORTABLE (with a Preferences dependency) | Works on the C6 if `Preferences` does under 3.x (UNVERIFIED). |
| `src/scanner_wifi.cpp` | PORTABLE with IDF-API risk | Pure `esp_wifi` and frame parsing, no pins. Callback is heavy. Needs a queue split in Phase 3. |
| `src/scanner_ble.cpp` | PORTABLE with library-API risk | No pins. Uses NimBLE-Arduino 1.4 API. |
| `src/signatures.cpp`, `include/signatures.h`, `include/detection.h` | PORTABLE | Rule table, scoring and data model. `detection.h` uses Arduino `constrain`. `signatures.cpp` takes `g_reg_mutex` directly. |
| `src/focus.cpp`, `include/focus.h` | PORTABLE | |
| `src/vendor.cpp`, `include/vendor.h`, `include/vendor_db_data.h` | PORTABLE | `vendor_db_data.h` is generated by `tools/gen_vendor_tables.py`. Flash cost matters on the C6 (8 MB flash is ample). |
| `src/opendroneid.c`, `src/odid_wifi.c`, `include/opendroneid.h`, `include/odid_wifi.h`, `include/odid_util.h` | PORTABLE (vendored, do not edit) | `odid_util.h` is own code. |
| `include/ui.h`, `include/scanner_*.h` | PORTABLE | Interface headers. `ui.h` is the nearest thing to a HAL boundary today. |
| `tools/gen_vendor_tables.py` | PORTABLE | Host tool. |
| `platformio.ini` | HAL | Per-env. |

OUT-OF-SCOPE: nothing in this baseline.

### 3.4 Proposed HAL boundary (headers only, no implementation)

Compile-time selection: a build flag (`BOARD_CYD` or `BOARD_C6_TOUCH147`) picks `src/boards/<board>/` through
`build_src_filter`. No virtual dispatch. Each interface is a free-function set in a namespace, which costs no RAM and
no vtable.

```cpp
// hal/board.h  - capabilities, constexpr so the core can `if constexpr`/#if on them
namespace board {
  constexpr const char* NAME;                 // "cyd" | "c6-touch-1.47"
  constexpr int  SCREEN_W, SCREEN_H;          // native orientation used by the UI
  constexpr bool HAS_SPEAKER, HAS_RGB_LED, HAS_LDR, HAS_IMU, HAS_BATTERY_ADC, HAS_SD;
  constexpr int  SCAN_SLICE_MS;               // default 6000, tuned per board later
  constexpr int  UI_TASK_CORE, SCAN_TASK_CORE;// C6: both 0
}

// hal/display.h
namespace display {
  void init();                                // panel init, rotation, colour fixes live here, not in ui
  void set_backlight(uint8_t duty);
  // Draw primitives the UI already uses (fillRect, drawFastH/VLine, fillCircle, fillTriangle, drawRect,
  // drawString + datum, textWidth, setTextColor). Phase 1 keeps the TFT_eSPI object behind this on cyd.
  // A banded-render hook (begin_band/flush_band) is added in Phase 4 only.
}

// hal/touch.h
namespace touch {
  void init();
  bool read(int& x, int& y);                  // already in screen coordinates; cyd applies calibration, c6 applies axis constants
}

// hal/storage.h   (no v3.21 users; defined in Phase 1 as a stub so Phase 5 has a home)
namespace storage {
  bool mount();
  // lock()/unlock() arbitration around the shared SPI bus, no-ops on cyd
}

// hal/alert_output.h
enum class AlertKind { NEW_DETECTION, HUNT_TICK, ... };
namespace alert {
  void init();
  void fire(ThreatType t);                    // board decides tone / LED / backlight pulse
  void hunt_tick(int rssi);                   // geiger click + LED/ring intensity
  void off();
}

// hal/power.h  (optional)
namespace power { bool available(); int battery_mv(); }
```

Why this is the smallest surface that works:
- `display` is the one thing `ui.cpp` cannot be portable without. Keeping the UI's current primitive calls and hiding the
  object behind them means Phase 1 changes little code, which is the "move, don't rewrite" rule. A sprite abstraction is
  deferred because v3.21 has no sprites.
- `touch::read` returns screen coordinates, so calibration and axis handling stay inside the board layer. The core never
  sees raw values (the manifest's grep criterion).
- `alert::fire(ThreatType)` is the manifest's "core says level X, type Y". Today the UI task does `ledcWriteTone` and `digitalWrite` itself.
  The frequencies and colours become data in the board layer.
- `storage` exists only to give Phase 5 a seam. It contains no code today.
- `board` is `constexpr` so nothing costs RAM and the C6 build does not carry CYD pins.

Additional splits Phase 1 needs, found by the audit:
- `ui.cpp` mixes layout, drawing, touch dispatch, alert output and backlight/speaker setup. Phase 1 moves only the
  hardware parts (init, backlight, tone, LED, touch read) behind the HAL. Layout stays in `ui.cpp` until Phase 4.
- `main.cpp:159-161` LED init and the `Serial` banner leave `main.cpp`.
- `UI_TASK_CORE`/`SCAN_TASK_CORE` become board constants (`main.cpp:194-195`).
- A single `FW_VERSION` define replaces the triple version edit (finding 7).

---

## 4. Risk register

| # | Risk | Evidence from this code | Severity |
|---|---|---|---|
| R1 | **Heap on 512 KB SRAM with Wi-Fi + BLE + display, no PSRAM** | Static RAM already 85.8 KB on the CYD. Registry held twice. NimBLE and Wi-Fi are both initialised and never freed (`main.cpp`). Wi-Fi and BLE are time-sliced but both stacks stay resident. | High. Measure first in Phase 2. |
| R2 | **Single core** | Correctness does not depend on two cores (section 1.5). Latency does: heavy work in callbacks, mutex drop on timeout, blocking `vTaskDelay` tones in the UI task. | Medium |
| R3 | **Toolchain / library API**: arduino-esp32 3.x via pioarduino. Baseline resolves to 2.0.17. `ledcSetup/ledcAttachPin/ledcWriteTone`, NimBLE-Arduino 1.4.x callback API, `Preferences` and `esp_wifi` init all need checking against 3.x. | `ui.cpp:1246-1254, 603, 1351`; `scanner_ble.cpp:231, 303`; unpinned `platform =` | High. UNVERIFIED, resolve in Phase 2. |
| R4 | **JD9853 / AXS5106L driver support.** TFT_eSPI is not a known fit. | Everything in `ui.cpp` goes through TFT_eSPI calls. If a replacement library has a different primitive set (text especially), more of `ui.cpp` changes. `TFT_eSPI`'s font/datum API (`drawString` with font numbers 1, 2, 4, 6) is used heavily. | High |
| R5 | **Shared LCD/SD SPI bus** | Not a v3.21 problem (no SD). Becomes real in Phase 5. | Low now, Medium in Phase 5 |
| R6 | **Alert UX with no speaker or LED** | Alerts and hunt mode are audio plus LED today (`ui.cpp:580-606, 1344-1367`). Hunt mode is a geiger-style feature and loses its main channel. | Medium (design) |
| R7 | **Portrait vs landscape UI** (new) | Finding 1. Scope is 320 samples wide. Sub-tab row is `SCREEN_W / 6`. Tabs 4x80. | High (Phase 4 scope) |
| R8 | **`copy_setup_pre.py` first-build skip** | Silent skip when TFT_eSPI is not yet downloaded (`copy_setup_pre.py:11-12`). | Low, `cyd` only |
| R9 | **Detections dropped under mutex contention** | `signatures.cpp:240`, `post_lock` counter | Low, Medium on one core |

---

## 5. Decisions from Paul (2026-11-19)

- **D1 (was Q1) Landscape on the C6.** The C6 UI is **landscape 320x172** (panel rotated), not the manifest's portrait.
  Consequence: the CYD layout (4 tabs x 80 px, 320-sample scope) carries over with the height reduced from 240 to
  172 px. Phase 4 is a height rework, not a re-layout. Paul has casing and battery options open. The manifest's
  portrait skeleton in section 7 is superseded by this. The orientation is the C6 board constant (`SCREEN_W/H`).
- **D2 (was Q3) Pin the platform.** `cyd` pins `espressif32 @ 6.9.0` (the version that resolved on 2026-11-19).
- **D3 (was Q4) Drop the unused `XPT2046_Touchscreen` library.**
- **D4 SD features come from the post-3.21 CYD work.** See `SD_BORROW.md`. Implemented in Phase 5.
- **D5 Toolchain for the C6** follows the manifest: pioarduino, pinned exactly, verified from its docs in Phase 2.

## 6. Open questions for Paul

Phase 1 is **not blocked** by any of these.

- **Q2. Hunt mode and alerts without audio or LED.** On the C6 the geiger click and LED go. Backlight pulses, a screen
  flash and the optional GPIO7 buzzer are the plan. Do you want to fit a passive buzzer on GPIO7, or should the
  design assume none? (Needed by Phase 5.)
- **Q5. Display library for the C6** is decided in Phase 2 (`DISPLAY_DECISION.md`) from the Waveshare demo and the
  LovyanGFX/Arduino_GFX sources. If neither supports the JD9853, an in-repo driver is the fallback. No decision is needed from you now.
- **Q6. CI.** Not requested. If other people are to pick this up, a GitHub Actions build of both environments would catch
  breakage. UNVERIFIED that the pioarduino fetch works from GitHub-hosted runners. Say if you want it.
