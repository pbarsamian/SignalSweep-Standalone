// SPDX-License-Identifier: GPL-3.0-or-later
// touch_axs5106l.h — minimal AXS5106L capacitive touch driver for the C6-Touch-LCD-1.47.
// Protocol (I2C address 0x63, ID reg 0x08, touch data reg 0x01, 14-byte frame, 12-bit X/Y per point, INT
// falling edge, RST pulse on init) is taken from Waveshare's esp_lcd_touch_axs5106l demo library; this is a
// small independent implementation of it. The address is Waveshare's; the bring-up I2C scan confirms it.
#pragma once
#include <Arduino.h>
#include <Wire.h>

namespace axs {

constexpr uint8_t ADDR = 0x63, REG_ID = 0x08, REG_TOUCH = 0x01;

struct Point { uint16_t x, y; };   // raw, in the panel's native portrait frame (172 x 320)

static volatile bool g_int = false;
static void IRAM_ATTR on_int() { g_int = true; }

static bool rd(uint8_t reg, uint8_t* d, size_t n) {
  Wire.beginTransmission(ADDR);
  Wire.write(reg);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom((int)ADDR, (int)n) != (int)n) return false;
  for (size_t i = 0; i < n; i++) d[i] = Wire.read();
  return true;
}

// Wire.begin(sda, scl) must already have been called (the IMU shares the bus).
static bool begin(int rst, int intr) {
  pinMode(rst, OUTPUT);
  digitalWrite(rst, LOW);  delay(200);
  digitalWrite(rst, HIGH); delay(300);
  pinMode(intr, INPUT);
  attachInterrupt(digitalPinToInterrupt(intr), on_int, FALLING);
  uint8_t id[3] = {};
  return rd(REG_ID, id, 3);
}

// Reads one frame when the INT line has fired. Returns number of points (0..max), fills p[].
static int read(Point* p, int max = 1) {
  if (!g_int) return 0;
  g_int = false;
  uint8_t b[14] = {};
  if (!rd(REG_TOUCH, b, sizeof(b))) return 0;
  int n = b[1];
  if (n > max) n = max;
  for (int i = 0; i < n; i++) {
    p[i].x = ((uint16_t)(b[2 + i * 6] & 0x0F) << 8) | b[3 + i * 6];
    p[i].y = ((uint16_t)(b[4 + i * 6] & 0x0F) << 8) | b[5 + i * 6];
  }
  return n;
}

}  // namespace axs
