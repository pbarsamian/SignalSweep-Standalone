// SPDX-License-Identifier: GPL-3.0-or-later
// power.h — optional battery HAL (INTERFACE ONLY, Phase 5). Boards with BOARD_HAS_BATTERY_ADC == 0 omit it.
#pragma once
int power_battery_mv();     // smoothed battery voltage in mV, or -1 when unavailable
