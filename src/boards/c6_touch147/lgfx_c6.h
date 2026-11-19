// SPDX-License-Identifier: GPL-3.0-or-later
// lgfx_c6.h — LovyanGFX configuration for the Waveshare ESP32-C6-Touch-LCD-1.47 panel.
//
// Panel: 172x320, controller RAM 240x320, column offset 34 (symmetric: 240-172-34 = 34). Pins are Waveshare's
// GPIO table. The init sequence below is Waveshare's own `lcd_reg_init()` from their Arduino demo
// (ESP32-C6-Touch-LCD-1.47-Demo.zip, examples/01_gfx_helloworld), translated mechanically to LovyanGFX's command
// list format; nothing in it was invented here. The demo drives the panel with an ST7789-class driver
// (Arduino_ST7789 + this list), and LovyanGFX's own Panel_JD9853 is a different 240x240 panel with different
// register values, so this class carries Waveshare's list. See docs/port/DISPLAY_DECISION.md.
//
// The LCD shares SCK/MOSI with the TF slot (GPIO1/2); bus_shared lets the SD library use the same SPI host.
#pragma once
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

namespace c6 {

struct Panel_WS147 : public lgfx::Panel_GC9xxx {
  Panel_WS147() {
    _cfg.memory_width  = 240;
    _cfg.memory_height = 320;
    _cfg.panel_width   = 172;
    _cfg.panel_height  = 320;
    _cfg.offset_x      = 34;
    _cfg.offset_y      = 0;
    _cfg.dummy_read_pixel = 16;
  }
 protected:
  uint8_t getColMod(uint8_t bpp) const override { return bpp > 16 ? 0x66 : 0x05; }
  const uint8_t* getInitCommands(uint8_t listno) const override {
    static constexpr uint8_t list0[] = {
        0x11, 0 + CMD_INIT_DELAY, 120,
        0xDF, 2, 0x98, 0x53,
        0xB2, 1, 0x23,
        0xB7, 4, 0x00, 0x47, 0x00, 0x6F,
        0xBB, 6, 0x1C, 0x1A, 0x55, 0x73, 0x63, 0xF0,
        0xC0, 2, 0x44, 0xA4,
        0xC1, 1, 0x16,
        0xC3, 8, 0x7D, 0x07, 0x14, 0x06, 0xCF, 0x71, 0x72, 0x77,
        0xC4, 12, 0x00, 0x00, 0xA0, 0x79, 0x0B, 0x0A, 0x16, 0x79, 0x0B, 0x0A, 0x16, 0x82,
        0xC8, 32, 0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28, 0x28, 0x26, 0x25, 0x17, 0x12, 0x0D, 0x04, 0x00, 0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28, 0x28, 0x26, 0x25, 0x17, 0x12, 0x0D, 0x04, 0x00,
        0xD0, 5, 0x04, 0x06, 0x6B, 0x0F, 0x00,
        0xD7, 2, 0x00, 0x30,
        0xE6, 1, 0x14,
        0xDE, 1, 0x01,
        0xB7, 5, 0x03, 0x13, 0xEF, 0x35, 0x35,
        0xC1, 3, 0x14, 0x15, 0xC0,
        0xC2, 2, 0x06, 0x3A,
        0xC4, 2, 0x72, 0x12,
        0xBE, 1, 0x00,
        0xDE, 1, 0x02,
        0xE5, 3, 0x00, 0x02, 0x00,
        0xE5, 3, 0x01, 0x02, 0x00,
        0xDE, 1, 0x00,
        0x35, 1, 0x00,
        0x3A, 1, 0x05,
        0x2A, 4, 0x00, 0x22, 0x00, 0xCD,
        0x2B, 4, 0x00, 0x00, 0x01, 0x3F,
        0xDE, 1, 0x02,
        0xE5, 3, 0x00, 0x02, 0x00,
        0xDE, 1, 0x00,
        0x36, 1, 0x00,
        0x21, 0 + CMD_INIT_DELAY, 10,
        0x29, 0,
        0xFF, 0xFF,
    };
    return listno == 0 ? list0 : nullptr;
  }
};

class LGFX : public lgfx::LGFX_Device {
  Panel_WS147     _panel;
  lgfx::Bus_SPI   _bus;
  lgfx::Light_PWM _light;
 public:
  LGFX() {
    { auto c = _bus.config();
      c.spi_host   = SPI2_HOST;      // FSPI on the C6
      c.spi_mode   = 0;
      c.freq_write = 40000000;
      c.freq_read  = 16000000;
      c.pin_sclk   = 1;
      c.pin_mosi   = 2;
      c.pin_miso   = -1;             // GPIO3 is the TF slot's MISO; the LCD is write-only
      c.pin_dc     = 15;
      c.use_lock   = true;
      c.dma_channel = SPI_DMA_CH_AUTO;
      _bus.config(c);
      _panel.setBus(&_bus); }
    { auto c = _panel.config();
      c.pin_cs   = 14;
      c.pin_rst  = 22;
      c.pin_busy = -1;
      c.readable = false;
      c.invert   = true;             // Waveshare's list ends with 0x21 (INVON)
      c.rgb_order = false;
      c.bus_shared = true;           // the TF slot is on the same SCK/MOSI
      _panel.config(c); }
    { auto c = _light.config();
      c.pin_bl      = 23;
      c.invert      = false;
      c.freq        = 5000;
      c.pwm_channel = 0;
      _light.config(c);
      _panel.setLight(&_light); }
    setPanel(&_panel);
  }
};

}  // namespace c6
