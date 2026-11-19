// SPDX-License-Identifier: GPL-3.0-or-later
// board_c6_touch147.h — Waveshare ESP32-C6-Touch-LCD-1.47. Pins are Waveshare's GPIO table
// (https://docs.waveshare.com/ESP32-C6-Touch-LCD-1.47, cross-checked against their Arduino demo sketches).
#pragma once
#include "lgfx_c6.h"
typedef c6::LGFX Display;   // the drawing object type the UI uses (hal/display.h)

#define BOARD_NAME            "c6-touch-1.47"

// LCD (SPI2 / FSPI), shared SCK/MOSI with the TF slot
#define PIN_LCD_SCK   1
#define PIN_LCD_MOSI  2
#define PIN_LCD_CS   14
#define PIN_LCD_DC   15
#define PIN_LCD_RST  22
#define PIN_LCD_BL   23
// TF (microSD), same bus
#define PIN_SD_SCK    1
#define PIN_SD_MOSI   2
#define PIN_SD_MISO   3
#define PIN_SD_CS     4
// Touch AXS5106L + IMU QMI8658A share this I2C bus
#define PIN_I2C_SDA  18
#define PIN_I2C_SCL  19
#define PIN_TP_RST   20
#define PIN_TP_INT   21
#define PIN_IMU_INT1  5
#define PIN_IMU_INT2  6
// Battery divider, VBAT = VADC x 3 (Waveshare)
#define PIN_BAT_ADC   0
// The only free pin (optional passive buzzer, Phase 5)
#define PIN_FREE_IO7  7

// Landscape (Paul, 2026-11-19): the 172x320 panel is rotated, so the UI sees 320x172.
#define SCREEN_W 320
#define SCREEN_H 172
#define BOARD_ROTATION 1   // LovyanGFX rotation 1

// Touch (AXS5106L reports in the native portrait frame, x 0..171, y 0..319). Measured on hardware 2026-11-19 with
// the bring-up's guided 5-point test at rotation 1: screen X = raw Y, screen Y = raw X, neither inverted.
// Fitted over four guided runs plus a verification set (docs/port/BRINGUP_RESULTS.md):
//   screenX = TOUCH_SX_A * rawY + TOUCH_SX_B,   screenY = TOUCH_SY_A * rawX + TOUCH_SY_B   (then clamp to the screen)
// Expect about +-5 px in X and +-10 px in Y. X is stable; Y is noisier and carries a ~1 mm bias (parallax suspected).
#define TOUCH_SWAP_XY  1
#define TOUCH_INVERT_X 0
#define TOUCH_INVERT_Y 0
#define TOUCH_SX_A  0.894f
#define TOUCH_SX_B  6.3f
#define TOUCH_SY_A  1.051f
#define TOUCH_SY_B -5.0f

#define BOARD_HAS_SPEAKER     0
#define BOARD_HAS_RGB_LED     0
#define BOARD_HAS_LDR         0
#define BOARD_HAS_IMU         1
#define BOARD_HAS_BATTERY_ADC 1
#define BOARD_HAS_SD          1
#define BOARD_SCAN_TASK_CORE  0     // single HP core
#define BOARD_UI_TASK_CORE    0

// Tone table kept so a buzzer on IO7 can reuse it (Phase 5, C6_HAS_BUZZER)
#define TONE_DRONE   1200
#define TONE_TRACKER  800
#define TONE_CAMERA   600
#define TONE_BODYCAM  440
#define TONE_ATTACK  1800
#define TONE_MS       150
