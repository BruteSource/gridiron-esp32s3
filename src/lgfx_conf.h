#pragma once
// Hosyond ESP32-S3 2.8" — ILI9341, 4-wire SPI. See HOSYOND_ESP32S3_BOARD.md 5b.
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 _panel;
  lgfx::Bus_SPI       _bus;
  lgfx::Light_PWM     _light;

public:
  LGFX() {
    {
      auto c = _bus.config();
      c.spi_host   = SPI3_HOST;
      c.spi_mode   = 0;
      c.freq_write = 40000000;
      c.freq_read  = 16000000;
      c.pin_sclk   = 12;
      c.pin_mosi   = 11;
      c.pin_miso   = 13;
      c.pin_dc     = 46;
      _bus.config(c);
      _panel.setBus(&_bus);
    }
    {
      auto c = _panel.config();
      c.pin_cs     = 10;
      c.pin_rst    = -1;    // panel RST tied to EN — no GPIO
      c.pin_busy   = -1;
      c.panel_width  = 240;
      c.panel_height = 320;
      c.offset_x   = 0;
      c.offset_y   = 0;
      c.invert     = true;  // REQUIRED on this panel or black renders white
      c.readable   = true;
      c.bus_shared = false;
      _panel.config(c);
    }
    {
      auto c = _light.config();
      c.pin_bl      = 45;   // active HIGH
      c.invert      = false;
      c.freq        = 12000;
      c.pwm_channel = 7;
      _light.config(c);
      _panel.setLight(&_light);
    }
    setPanel(&_panel);
  }
};
