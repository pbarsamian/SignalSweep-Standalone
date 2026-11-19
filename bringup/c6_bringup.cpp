// SPDX-License-Identifier: GPL-3.0-or-later
// c6_bringup.cpp — Phase 2 bring-up for the Waveshare ESP32-C6-Touch-LCD-1.47. NOT the SignalSweep firmware.
// Prints chip info and heap numbers, scans I2C, draws a border/offset test pattern, shows touch coordinates,
// mounts the TF card, and measures free heap after WiFi, BLE and both are initialised.
// Serial (USB Serial/JTAG) commands: h heap, i i2c scan, s sd list, w wifi init, b ble init, c redraw, r reboot.
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <esp_chip_info.h>
#include <esp_wifi.h>
#include <esp_idf_version.h>
#include <NimBLEDevice.h>
#include "boards/c6_touch147/board_c6_touch147.h"
#include "boards/c6_touch147/touch_axs5106l.h"

#define FW "c6-bringup 0.3"
static Display gfx;
static bool s_wifi = false, s_ble = false;

static void heap(const char* tag) {
  Serial.printf("[heap] %-22s free=%u  largest=%u  min_ever=%u  internal_free=%u\n", tag,
                (unsigned)esp_get_free_heap_size(),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT),
                (unsigned)esp_get_minimum_free_heap_size(),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

static void chip_info() {
  esp_chip_info_t ci; esp_chip_info(&ci);
  Serial.printf("[chip] model=%d rev=%d.%d cores=%d features=0x%x\n", (int)ci.model, ci.revision / 100, ci.revision % 100, ci.cores, (unsigned)ci.features);
  Serial.printf("[chip] cpu=%u MHz  flash=%u B  psram=%u B  sdk=%s  idf=%s  arduino=%s\n", (unsigned)getCpuFrequencyMhz(),
                (unsigned)ESP.getFlashChipSize(), (unsigned)ESP.getPsramSize(), ESP.getSdkVersion(), esp_get_idf_version(), ESP_ARDUINO_VERSION_STR);
  Serial.printf("[chip] sketch=%u B  free_sketch=%u B  mac=%012llX\n", (unsigned)ESP.getSketchSize(), (unsigned)ESP.getFreeSketchSpace(), ESP.getEfuseMac());
}

static void i2c_scan() {
  Serial.print("[i2c] scan SDA=18 SCL=19:");
  int n = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { Serial.printf(" 0x%02X", a); n++; }
  }
  Serial.printf("  (%d found)\n", n);
}

static void sd_list() {
  // SD shares SCK/MOSI with the LCD: chip selects idle high, SD runs on the same SPI host as the display.
  digitalWrite(PIN_LCD_CS, HIGH);
  SPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  if (!SD.begin(PIN_SD_CS, SPI, 10000000)) { Serial.println("[sd] mount FAILED (no card, or not FAT32)"); return; }
  Serial.printf("[sd] type=%d  size=%llu MB  used=%llu MB\n", (int)SD.cardType(), SD.cardSize() / (1024 * 1024), SD.usedBytes() / (1024 * 1024));
  File root = SD.open("/");
  int n = 0;
  for (File f = root.openNextFile(); f && n < 20; f = root.openNextFile(), n++)
    Serial.printf("[sd]   %s%s  %u B\n", f.name(), f.isDirectory() ? "/" : "", (unsigned)f.size());
  Serial.printf("[sd] %d entries listed (max 20)\n", n);
}

static void wifi_init() {
  if (s_wifi) return;
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_err_t e = esp_wifi_init(&cfg);
  esp_wifi_set_storage(WIFI_STORAGE_RAM);
  esp_wifi_set_mode(WIFI_MODE_NULL);
  e = e ? e : esp_wifi_start();
  esp_wifi_set_promiscuous(true);
  s_wifi = true;
  Serial.printf("[wifi] init %s\n", e ? esp_err_to_name(e) : "OK");
}

static void ble_init() {
  if (s_ble) return;
  NimBLEDevice::init("");
  s_ble = true;
  Serial.println("[ble] NimBLE init OK");
}

static int W() { return gfx.width(); }
static int H() { return gfx.height(); }

static void pattern() {
  gfx.fillScreen(TFT_BLACK);
  gfx.drawRect(0, 0, W(), H(), TFT_WHITE);                // 1-px border: any offset error clips or gaps it
  gfx.drawRect(1, 1, W() - 2, H() - 2, TFT_RED);
  gfx.drawFastHLine(0, H() / 2, W(), 0x4208);
  gfx.drawFastVLine(W() / 2, 0, H(), 0x4208);
  int bw = (W() - 8) / 6;
  const uint16_t col[6] = {TFT_RED, TFT_GREEN, TFT_BLUE, TFT_YELLOW, TFT_CYAN, TFT_MAGENTA};
  for (int i = 0; i < 6; i++) gfx.fillRect(4 + i * bw, 22, bw, 14, col[i]);
  gfx.setTextColor(TFT_WHITE, TFT_BLACK);
  gfx.setTextDatum(textdatum_t::top_left);     gfx.drawString("TL", 4, 4);
  gfx.setTextDatum(textdatum_t::top_right);    gfx.drawString("TR", W() - 5, 4);
  gfx.setTextDatum(textdatum_t::bottom_left);  gfx.drawString("BL", 4, H() - 5);
  gfx.setTextDatum(textdatum_t::bottom_right); gfx.drawString("BR", W() - 5, H() - 5);
  gfx.setTextDatum(textdatum_t::top_left);
  char b[64];
  snprintf(b, sizeof(b), "%dx%d rot=%d  %s", W(), H(), gfx.getRotation(), FW);
  gfx.drawString(b, 24, 44);
}

// ---- guided touch calibration: prompts each target on screen, records raw coordinates, derives the axis mapping ----
struct Tgt { const char* name; int x, y; };
#define N_CAL 5
#define N_TG  10                            // 5 calibration targets, then 5 verification targets at new positions
static Tgt TG[N_TG];
static int  s_tg = 0;                       // 0..4 calibrating, 5..9 verifying, 10 = finished
static axs::Point s_rec[N_CAL];
static uint32_t s_last_touch_ms = 0;
static bool s_map_x_from_rawy = true, s_map_x_inv = false, s_map_y_inv = false;   // derived axis choice
// Linear fit per screen axis: screen = a*raw + b (least squares over the 5 calibration taps)
static float s_ax = 1, s_bx = 0, s_ay = 1, s_by = 0;
static bool  s_fit_ok = false;
static float s_ver_sum_x = 0, s_ver_sum_y = 0;

static void tg_prompt() {
  pattern();                                // clears the previous crosshair
  if (s_tg < N_TG) {
    uint16_t c = s_tg < N_CAL ? TFT_YELLOW : TFT_CYAN;
    gfx.setTextColor(c, TFT_BLACK);
    char b[48]; snprintf(b, sizeof(b), "%s the crosshair: %s", s_tg < N_CAL ? "TAP" : "VERIFY", TG[s_tg].name);
    gfx.drawString(b, 24, 76);
    int x = TG[s_tg].x, y = TG[s_tg].y;
    gfx.drawFastHLine(x - 8, y, 17, c); gfx.drawFastVLine(x, y - 8, 17, c);
  }
}

static void fit_axis(const float* raw, const float* scr, int n, float& a, float& b) {
  float mr = 0, ms = 0;
  for (int i = 0; i < n; i++) { mr += raw[i]; ms += scr[i]; }
  mr /= n; ms /= n;
  float num = 0, den = 0;
  for (int i = 0; i < n; i++) { num += (raw[i] - mr) * (scr[i] - ms); den += (raw[i] - mr) * (raw[i] - mr); }
  a = den > 0 ? num / den : 1;
  b = ms - a * mr;
}

static void tg_done() {
  // Screen X axis is whichever raw axis changed most between TL and TR; inverted if it decreased.
  int dxx = (int)s_rec[1].x - (int)s_rec[0].x, dxy = (int)s_rec[1].y - (int)s_rec[0].y;
  s_map_x_from_rawy = abs(dxy) >= abs(dxx);
  s_map_x_inv = s_map_x_from_rawy ? dxy < 0 : dxx < 0;
  int dyx = (int)s_rec[3].x - (int)s_rec[0].x, dyy = (int)s_rec[3].y - (int)s_rec[0].y;   // TL -> BL
  s_map_y_inv = s_map_x_from_rawy ? dyx < 0 : dyy < 0;
  Serial.printf("[cal] screen X = %s%s,  screen Y = %s%s   (native raw x:0..171, y:0..319)\n",
                s_map_x_inv ? "inverted " : "", s_map_x_from_rawy ? "rawY" : "rawX",
                s_map_y_inv ? "inverted " : "", s_map_x_from_rawy ? "rawX" : "rawY");
  // Least-squares scale+offset per axis from the 5 calibration taps (targets are in screen coordinates)
  float rxa[N_CAL], rya[N_CAL], sxa[N_CAL], sya[N_CAL];
  for (int i = 0; i < N_CAL; i++) {
    rxa[i] = s_map_x_from_rawy ? s_rec[i].y : s_rec[i].x;
    rya[i] = s_map_x_from_rawy ? s_rec[i].x : s_rec[i].y;
    sxa[i] = TG[i].x; sya[i] = TG[i].y;
  }
  fit_axis(rxa, sxa, N_CAL, s_ax, s_bx);
  fit_axis(rya, sya, N_CAL, s_ay, s_by);
  s_fit_ok = true;
  Serial.printf("[cal] fit: screenX = %.3f * raw + %.1f   screenY = %.3f * raw + %.1f\n", s_ax, s_bx, s_ay, s_by);
  for (int i = 0; i < N_CAL; i++)
    Serial.printf("[cal] residual after fit %-12s dx=%+.0f dy=%+.0f\n", TG[i].name, s_ax * rxa[i] + s_bx - sxa[i], s_ay * rya[i] + s_by - sya[i]);
  s_tg = N_CAL;
  tg_prompt();                              // continue straight into the verification targets
}

static void map_touch(const axs::Point& p, int& sx, int& sy) {
  float rx = s_map_x_from_rawy ? p.y : p.x, ry = s_map_x_from_rawy ? p.x : p.y;
  if (s_fit_ok) { sx = (int)lroundf(s_ax * rx + s_bx); sy = (int)lroundf(s_ay * ry + s_by); return; }
  int xmax = s_map_x_from_rawy ? 319 : 171, ymax = s_map_x_from_rawy ? 171 : 319;
  sx = s_map_x_inv ? xmax - (int)rx : (int)rx;
  sy = s_map_y_inv ? ymax - (int)ry : (int)ry;
}

static void touch_show() {
  axs::Point p[1];
  if (axs::read(p, 1) < 1) return;
  uint32_t now = millis();
  bool fresh = now - s_last_touch_ms > 400;           // a new tap, not the repeat frames of a held finger
  s_last_touch_ms = now;
  Serial.printf("[touch] raw=%u,%u%s\n", p[0].x, p[0].y, fresh ? "  (new tap)" : "");
  if (s_tg < N_CAL) {
    if (!fresh) return;
    s_rec[s_tg] = p[0];
    Serial.printf("[cal] %s raw=%u,%u\n", TG[s_tg].name, p[0].x, p[0].y);
    s_tg++;
    if (s_tg == N_CAL) tg_done(); else tg_prompt();
    return;
  }
  if (s_tg < N_TG) {                        // verification: score the fitted mapping on targets it was not fitted to
    if (!fresh) return;
    int mx, my; map_touch(p[0], mx, my);
    int ex = mx - TG[s_tg].x, ey = my - TG[s_tg].y;
    Serial.printf("[ver] %-12s target=%d,%d got=%d,%d err=%+d,%+d raw=%u,%u\n", TG[s_tg].name, TG[s_tg].x, TG[s_tg].y, mx, my, ex, ey, p[0].x, p[0].y);
    s_ver_sum_x += abs(ex); s_ver_sum_y += abs(ey);
    s_tg++;
    if (s_tg == N_TG) {
      Serial.printf("[ver] mean abs error after fit: x=%.1f px  y=%.1f px  (5 targets)\n", s_ver_sum_x / 5, s_ver_sum_y / 5);
      pattern();
      gfx.setTextColor(TFT_GREEN, TFT_BLACK);
      gfx.drawString("done - touch to draw dots", 24, 76);
    } else tg_prompt();
    return;
  }
  int mx, my; map_touch(p[0], mx, my);
  gfx.fillCircle(constrain(mx, 0, W() - 1), constrain(my, 0, H() - 1), 3, TFT_YELLOW);
  if (fresh) Serial.printf("[touch] mapped=%d,%d\n", mx, my);
}

static void heap_sequence() {
  Preferences pr; pr.begin("bringup", false);
  uint8_t boots = pr.getUChar("boots", 0); pr.putUChar("boots", boots + 1); pr.end();
  bool bleFirst = boots & 1;       // alternate the order each boot so two boots give WiFi-only, BLE-only and both
  Serial.printf("[seq] boot #%u, order: %s\n", boots, bleFirst ? "BLE then WiFi" : "WiFi then BLE");
  if (bleFirst) { ble_init(); heap("after BLE init"); wifi_init(); heap("after BLE+WiFi init"); }
  else          { wifi_init(); heap("after WiFi init"); ble_init(); heap("after WiFi+BLE init"); }
}

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);   // USB CDC: wait briefly for a monitor
  delay(500);
  Serial.println("\n=== " FW " ===");
  chip_info();
  heap("boot");

  pinMode(PIN_LCD_CS, OUTPUT); digitalWrite(PIN_LCD_CS, HIGH);   // idle both chip selects high before either init
  pinMode(PIN_SD_CS, OUTPUT);  digitalWrite(PIN_SD_CS, HIGH);

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  i2c_scan();

  gfx.init();
  gfx.setRotation(BOARD_ROTATION);
  gfx.setBrightness(200);
  pattern();
  Serial.printf("[lcd] init done, %dx%d rotation %d\n", W(), H(), gfx.getRotation());
  heap("after display init");

  Serial.printf("[touch] AXS5106L begin: %s\n", axs::begin(PIN_TP_RST, PIN_TP_INT) ? "ID read OK" : "ID read FAILED");
  sd_list();
  heap("after SD mount");
  pattern();                       // redraw after SD traffic: proves the shared bus still drives the panel
  Serial.println("[bus] display redrawn after SD access (check the screen shows the pattern)");

  heap_sequence();
  // targets sit well inside the corners (the far edges are hard to reach on this small panel)
  TG[0] = {"UPPER-LEFT", 40, 30};  TG[1] = {"UPPER-RIGHT", W() - 41, 30};
  TG[2] = {"LOWER-RIGHT", W() - 41, H() - 31};  TG[3] = {"LOWER-LEFT", 40, H() - 31};  TG[4] = {"CENTRE", W() / 2, H() / 2};
  // verification targets: new positions the fit never saw (one near the right edge, one near the bottom)
  TG[5] = {"V-LEFT-UPPER", 100, 60};  TG[6] = {"V-RIGHT-UPPER", 220, 60};
  TG[7] = {"V-RIGHT-LOWER", 220, 112};  TG[8] = {"V-LEFT-LOWER", 100, 112};  TG[9] = {"V-FAR-RIGHT", 300, 86};
  tg_prompt();
  Serial.println("[ready] commands: h i s w b c r. Follow the on-screen touch prompts.");
}

void loop() {
  touch_show();
  while (Serial.available()) {
    switch (Serial.read()) {
      case 'h': heap("now"); break;
      case 'i': i2c_scan(); break;
      case 's': sd_list(); pattern(); tg_prompt(); break;
      case 'w': wifi_init(); heap("after wifi_init"); break;
      case 'b': ble_init(); heap("after ble_init"); break;
      case 'c': s_tg = 0; s_fit_ok = false; s_ver_sum_x = s_ver_sum_y = 0; tg_prompt(); break;
      case 'r': Serial.println("[reboot]"); delay(100); ESP.restart(); break;
    }
  }
  delay(5);
}
