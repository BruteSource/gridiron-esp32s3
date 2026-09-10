#include "touch.h"
#include "config.h"
#include <Wire.h>
#include <Preferences.h>

// FT6336G capacitive touch — raw register reads (library drivers misbehave on
// this panel; see HOSYOND_ESP32S3_BOARD.md 5c / 7).
#define FT_ADDR 0x38
#define PIN_SDA 16
#define PIN_SCL 15
#define PIN_RST 18
#define PIN_INT 17

// Default endpoints from the board doc (rotation 0). Overwritten by a stored
// calibration if one exists.
static TouchCal s_cal = {20, 230, 16, 306};
static bool     s_calValid = false;

static bool     s_down = false;
static int      s_x0, s_y0, s_lastY, s_maxMove;
static uint32_t s_downMs;

// --- raw I2C read, with the 0x0FFF garbage guard ---------------------------
static bool ftRaw(int* rx, int* ry) {
  uint8_t d[7];
  Wire.beginTransmission(FT_ADDR);
  Wire.write(0x02);
  if (Wire.endTransmission(false) != 0) return false;
  if ((int)Wire.requestFrom(FT_ADDR, 7) != 7) return false;
  for (int i = 0; i < 7; i++) d[i] = Wire.read();

  if ((d[0] & 0x0F) == 0) return false;                     // no finger
  int x = ((uint16_t)(d[1] & 0x0F) << 8) | d[2];
  int y = ((uint16_t)(d[3] & 0x0F) << 8) | d[4];
  if (x > 800 || y > 800) return false;                     // garbage sample
  *rx = x; *ry = y;
  return true;
}

void touch_init() {
  pinMode(PIN_RST, OUTPUT);
  digitalWrite(PIN_RST, LOW);  delay(10);
  digitalWrite(PIN_RST, HIGH); delay(300);
  Wire.begin(PIN_SDA, PIN_SCL, 400000U);
  pinMode(PIN_INT, INPUT_PULLUP);

  // raise touch threshold a little to cut phantom taps
  Wire.beginTransmission(FT_ADDR);
  Wire.write(0x80);
  Wire.write(40);
  Wire.endTransmission();

  // G_MODE = polling: INT is pulsed low repeatedly while touched (not a single
  // one-shot pulse) so it reliably wakes the MCU from light sleep.
  Wire.beginTransmission(FT_ADDR);
  Wire.write(0xA4);
  Wire.write(0x00);
  Wire.endTransmission();

  Preferences p;
  if (p.begin("grid_touch", true)) {
    // ignore a stored calibration taken at a different panel rotation
    if (p.isKey("xmn") && p.getInt("rot", 0) == SCREEN_ROTATION) {
      s_cal.xMin = p.getInt("xmn", s_cal.xMin);
      s_cal.xMax = p.getInt("xmx", s_cal.xMax);
      s_cal.yMin = p.getInt("ymn", s_cal.yMin);
      s_cal.yMax = p.getInt("ymx", s_cal.yMax);
      s_calValid = true;
    }
    p.end();
  }
}

bool touch_readRaw(int* rx, int* ry) { return ftRaw(rx, ry); }
bool touch_is_touched()              { int x, y; return ftRaw(&x, &y); }
void touch_bus_resume()              { Wire.begin(PIN_SDA, PIN_SCL, 400000U); }
bool touch_isCalibrated()            { return s_calValid; }
TouchCal touch_getCal()              { return s_cal; }

void touch_setCal(const TouchCal& c, bool persist) {
  s_cal = c;
  s_calValid = true;
  if (persist) {
    Preferences p;
    if (p.begin("grid_touch", false)) {
      p.putInt("xmn", c.xMin); p.putInt("xmx", c.xMax);
      p.putInt("ymn", c.yMin); p.putInt("ymx", c.yMax);
      p.putInt("rot", SCREEN_ROTATION);
      p.end();
    }
  }
}

static bool readPoint(int* sx, int* sy) {
  int rx, ry;
  if (!ftRaw(&rx, &ry)) return false;
  long x = map(rx, s_cal.xMin, s_cal.xMax, 0, 240);
  long y = map(ry, s_cal.yMin, s_cal.yMax, 0, 320);
  // Near an edge the finger's contact patch pulls toward screen centre, so
  // edge buttons get missed — nudge taps in the top/bottom strips outward.
  if (y < 48)        y -= (48 - y) / 4;               // up to ~12 px at y=0
  else if (y > 271)  y += (y - 271) / 4;              // up to ~12 px at y=319
  *sx = constrain(x, 0, 239);
  *sy = constrain(y, 0, 319);
  return true;
}

TouchEv touch_poll() {
  TouchEv e;
  int x, y;
  bool touching = readPoint(&x, &y);
  uint32_t now = millis();

  if (touching) {
    if (!s_down) {
      s_down = true;
      s_x0 = x; s_y0 = y; s_lastY = y;
      s_maxMove = 0;
      s_downMs = now;
    } else {
      e.dragDy = y - s_lastY;
      s_lastY = y;
      int dist = abs(x - s_x0) + abs(y - s_y0);
      if (dist > s_maxMove) s_maxMove = dist;
      if (s_maxMove > 16) e.dragging = true;
    }
  } else if (s_down) {
    s_down = false;
    if (s_maxMove < 24 && now - s_downMs < 700) {
      e.tap = true;
      e.x = s_x0;
      e.y = s_y0;
    }
  }
  return e;
}
