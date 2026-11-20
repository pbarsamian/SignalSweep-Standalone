// SPDX-License-Identifier: GPL-3.0-or-later
// C6 battery voltage: GPIO0 reads the battery through a divider; Waveshare's documented relation is VBAT = VADC x 3
// (docs.waveshare.com/ESP32-C6-Touch-LCD-1.47). analogReadMilliVolts() applies the chip's ADC calibration.
// NEEDS HARDWARE TEST: accuracy against a multimeter (BAT_CAL_PERMILLE trims it), and behaviour with no battery fitted.
#include <Arduino.h>
#include "hal/board.h"
#include "hal/power.h"

#ifndef BAT_CAL_PERMILLE
#define BAT_CAL_PERMILLE 1000      // 1000 = no trim; set from a meter reading if the board reads high or low
#endif

int power_battery_mv() {
    static float    ema = 0;
    static uint32_t last = 0;
    static bool     init = false;
    uint32_t now = millis();
    if (!init || now - last >= 500) {
        if (!init) analogSetPinAttenuation(PIN_BAT_ADC, ADC_11db);
        uint32_t acc = 0;
        for (int i = 0; i < 8; i++) acc += analogReadMilliVolts(PIN_BAT_ADC);
        float vbat = (acc / 8.0f) * 3.0f * BAT_CAL_PERMILLE / 1000.0f;
        ema = init ? ema * 0.8f + vbat * 0.2f : vbat;           // smooth: the reading jitters by tens of mV
        init = true;
        last = now;
    }
    // Below ~2 V or above ~4.6 V a LiPo is not what the pin is seeing (no battery fitted, or a floating input)
    if (ema < 2000 || ema > 4600) return -1;
    return (int)ema;
}
