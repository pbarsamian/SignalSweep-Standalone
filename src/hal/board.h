// SPDX-License-Identifier: GPL-3.0-or-later
// board.h — selects the board header at compile time (-DBOARD_CYD or -DBOARD_C6_TOUCH147).
//
// Every board header must define:
//   BOARD_NAME, SCREEN_W, SCREEN_H           the orientation the UI draws in
//   BOARD_HAS_SPEAKER / _RGB_LED / _LDR / _IMU / _BATTERY_ADC / _SD   (0 or 1)
//   BOARD_SCAN_TASK_CORE, BOARD_UI_TASK_CORE
//   TONE_DRONE/TRACKER/CAMERA/BODYCAM/ATTACK, TONE_MS      alert tone table (Hz, ms)
// Compile-time selection on purpose: no vtables, nothing in RAM, no other board's pins in the binary.
#pragma once
#if defined(BOARD_CYD)
  #include "../boards/cyd/board_cyd.h"
#elif defined(BOARD_C6_TOUCH147)
  #include "../boards/c6_touch147/board_c6_touch147.h"
#else
  #error "No board selected: add -DBOARD_CYD or -DBOARD_C6_TOUCH147 to build_flags"
#endif
