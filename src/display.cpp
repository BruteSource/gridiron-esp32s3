#include "display.h"
#include "touch.h"
#include "ui.h"
#include "games.h"
#include "config.h"
#include "settings.h"
#include <math.h>
#include <Adafruit_NeoPixel.h>

LGFX        lcd;
LGFX_Sprite canvas(&lcd);

#if !RGB_LED_ENABLE
static void kill_rgb_led() {
  // The board's WS2812 can power up lit / animating. Drive a clean "all off"
  // frame, then hold the data line low as a plain output.
  Adafruit_NeoPixel px(1, RGB_LED_PIN, NEO_GRB + NEO_KHZ800);
  px.begin();
  px.clear();
  for (int i = 0; i < 4; i++) { px.show(); delay(3); }
  pinMode(RGB_LED_PIN, OUTPUT);
  digitalWrite(RGB_LED_PIN, LOW);
}
#endif

void display_init() {
#if !RGB_LED_ENABLE
  kill_rgb_led();
#endif

  lcd.init();
  lcd.setRotation(SCREEN_ROTATION);   // 0 = USB up, 2 = USB down (see config.h)
  lcd.fillScreen(C_BG);
  lcd.setBrightness(g_set.brightness);

  canvas.setColorDepth(16);
  canvas.setPsram(true);
  if (!canvas.createSprite(240, 320)) {
    lcd.fillScreen(C_BG);
    lcd.setTextColor(C_LIVE);
    lcd.drawString("sprite alloc failed", 10, 10);
  }
}

void scr_boot_draw() {
  // Fixed lime palette (independent of the selected theme — matches the case).
  const uint16_t BG    = 0x0040;   // near-black green
  const uint16_t LIME  = 0xB7E5;   // bright lime
  const uint16_t MID   = 0x6E03;   // mid lime
  const uint16_t DARK  = 0x3B22;   // deep lime
  const uint16_t DIM   = 0x6CE7;   // muted lime
  const uint16_t PIG   = 0x7A44;   // football brown
  const uint16_t INK   = 0xEFFB;   // off-white

  canvas.fillScreen(BG);
  canvas.setTextDatum(textdatum_t::middle_center);

  // ---- isometric "box" logo with a football on the front face ----
  const int fx = 79, fy = 48, fw = 82, fh = 60;   // front face
  const int dx = 21, dy = -14;                     // depth vector
  // right face
  canvas.fillTriangle(fx + fw, fy, fx + fw + dx, fy + dy, fx + fw + dx, fy + fh + dy, DARK);
  canvas.fillTriangle(fx + fw, fy, fx + fw + dx, fy + fh + dy, fx + fw, fy + fh, DARK);
  // top face
  canvas.fillTriangle(fx, fy, fx + dx, fy + dy, fx + fw + dx, fy + dy, LIME);
  canvas.fillTriangle(fx, fy, fx + fw + dx, fy + dy, fx + fw, fy, LIME);
  // front face
  canvas.fillRect(fx, fy, fw, fh, MID);
  // outline
  canvas.drawRect(fx, fy, fw, fh, BG);
  canvas.drawLine(fx, fy, fx + dx, fy + dy, BG);
  canvas.drawLine(fx + fw, fy, fx + fw + dx, fy + dy, BG);
  canvas.drawLine(fx + dx, fy + dy, fx + fw + dx, fy + dy, BG);
  canvas.drawLine(fx + fw + dx, fy + dy, fx + fw + dx, fy + fh + dy, BG);
  canvas.drawLine(fx + fw, fy + fh, fx + fw + dx, fy + fh + dy, BG);
  // football
  int bx = fx + fw / 2, by = fy + fh / 2 + 1;
  canvas.fillEllipse(bx, by, 19, 12, PIG);
  canvas.drawEllipse(bx, by, 19, 12, INK);
  canvas.drawFastHLine(bx - 9, by, 18, INK);
  for (int i = -6; i <= 6; i += 4) canvas.drawFastVLine(bx + i, by - 3, 7, INK);

  // ---- wordmark ----
  canvas.setFont(&fonts::FreeSansBold12pt7b);
  canvas.setTextColor(LIME);
  canvas.drawString("THE GAME BOX", 120, 148);
  canvas.setFont(&fonts::Font2);
  canvas.setTextColor(DIM);
  canvas.drawString("live football scoreboard", 120, 170);

  // ---- status ----
  uint32_t phase = (millis() / 350) % 4;
  char dots[4] = {0};
  for (uint32_t i = 0; i < phase; i++) dots[i] = '.';
  canvas.setTextColor(INK);
  canvas.drawString(g_bootMsg, 120, 202);
  canvas.setTextColor(LIME);
  canvas.drawString(dots, 120, 220);

  // ---- credits ----
  canvas.setTextColor(INK);
  canvas.drawString("Vibecoded with Claude by BruteSource", 120, 262);
  canvas.setTextColor(DIM);
  canvas.drawString("github.com/BruteSource/gridiron-esp32s3", 120, 278);
  canvas.drawString("hold screen to calibrate touch", 120, 302);

  canvas.pushSprite(0, 0);
}

// ---- 4-point touch calibration -----------------------------------------
static void calTarget(int x, int y, const char* msg) {
  canvas.fillScreen(C_BG);
  canvas.setFont(&fonts::Font2);
  canvas.setTextColor(C_TEXT);
  canvas.setTextDatum(textdatum_t::middle_center);
  canvas.drawString("Touch calibration", 120, 150);
  canvas.setTextColor(C_DIM);
  canvas.drawString(msg, 120, 172);
  canvas.drawLine(x - 12, y, x + 12, y, C_LIVE);
  canvas.drawLine(x, y - 12, x, y + 12, C_LIVE);
  canvas.drawCircle(x, y, 7, C_LIVE);
  canvas.pushSprite(0, 0);
}

bool display_calibrate() {
  const int tx[4] = {24, 216, 216, 24};
  const int ty[4] = {28, 28, 292, 292};
  int rx[4], ry[4];
  int dx, dy;

  for (int i = 0; i < 4; i++) {
    char m[20]; snprintf(m, sizeof(m), "tap target %d of 4", i + 1);
    calTarget(tx[i], ty[i], m);

    while (touch_readRaw(&dx, &dy)) delay(20);      // wait for any release
    delay(150);
    while (!touch_readRaw(&dx, &dy)) delay(10);     // wait for the tap

    long sx = 0, sy = 0; int nn = 0;
    uint32_t t0 = millis();
    while (touch_readRaw(&dx, &dy) && millis() - t0 < 2500) {
      sx += dx; sy += dy; nn++;
      delay(10);
    }
    rx[i] = nn ? (int)(sx / nn) : dx;
    ry[i] = nn ? (int)(sy / nn) : dy;

    canvas.fillCircle(tx[i], ty[i], 7, C_ACCENT);
    canvas.pushSprite(0, 0);
    delay(250);
  }

  // average raw value at each screen edge target, then extrapolate to 0/240 & 0/320
  float rawL = (rx[0] + rx[3]) / 2.0f, rawR = (rx[1] + rx[2]) / 2.0f;
  float rawT = (ry[0] + ry[1]) / 2.0f, rawB = (ry[2] + ry[3]) / 2.0f;
  float xPerPx = (rawR - rawL) / (216 - 24);
  float yPerPx = (rawB - rawT) / (292 - 28);

  TouchCal c;
  c.xMin = (int)lroundf(rawL - xPerPx * 24);
  c.xMax = (int)lroundf(rawL + xPerPx * (240 - 24));
  c.yMin = (int)lroundf(rawT - yPerPx * 28);
  c.yMax = (int)lroundf(rawT + yPerPx * (320 - 28));

  bool ok = abs(c.xMax - c.xMin) > 40 && abs(c.yMax - c.yMin) > 40;

  canvas.fillScreen(C_BG);
  canvas.setFont(&fonts::FreeSansBold12pt7b);
  canvas.setTextDatum(textdatum_t::middle_center);
  if (ok) {
    touch_setCal(c, true);
    canvas.setTextColor(C_ACCENT);
    canvas.drawString("calibrated", 120, 160);
  } else {
    canvas.setTextColor(C_LIVE);
    canvas.drawString("calibration failed", 120, 160);
  }
  canvas.pushSprite(0, 0);
  delay(900);
  return ok;
}
