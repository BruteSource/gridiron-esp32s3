#include "anim.h"
#include "display.h"
#include "ui.h"
#include <Arduino.h>
#include <math.h>

#define DUR 1300           // ms

static LGFX_Sprite s_bg(&lcd);   // frozen copy of the screen under the overlay

static uint16_t lerp565(uint16_t a, uint16_t b, float t) {
  if (t < 0) t = 0; if (t > 1) t = 1;
  int r1 = (a >> 11) & 31, g1 = (a >> 5) & 63, b1 = a & 31;
  int r2 = (b >> 11) & 31, g2 = (b >> 5) & 63, b2 = b & 31;
  return (uint16_t)(((r1 + (int)((r2 - r1) * t)) << 11) |
                    ((g1 + (int)((g2 - g1) * t)) << 5) |
                     (b1 + (int)((b2 - b1) * t)));
}

static uint16_t contrastOn(uint16_t c) {
  int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
  int lum = r * 77 / 31 + g * 150 / 63 + b * 29 / 31;
  return lum > 130 ? C_BG : C_TEXT;
}

static void setLabelFont(int len) {
  if (len <= 5)      canvas.setFont(&fonts::FreeSansBold18pt7b);
  else if (len <= 9) canvas.setFont(&fonts::FreeSansBold12pt7b);
  else               canvas.setFont(&fonts::FreeSansBold9pt7b);
}

static const char* labelFor(int pts) {
  if (pts >= 8) return "TD +2";
  if (pts >= 6) return "TOUCHDOWN";
  if (pts == 3) return "FIELD GOAL";
  if (pts == 2) return "SAFETY";
  if (pts == 1) return "EXTRA PT";
  return "SCORE";
}

// ease-out overshoot for the pop-in
static float popScale(float t) {
  if (t >= 1) return 1.0f;
  float s = 1.70158f;
  t -= 1.0f;
  return t * t * ((s + 1) * t + s) + 1.0f;
}

void anim_score(int side, uint16_t color, int points, int style) {
  style = ((style % 3) + 3) % 3;
  const char* label = labelFor(points);
  char pb[8]; snprintf(pb, sizeof(pb), "+%d", points);

  if (!s_bg.getBuffer()) {
    s_bg.setColorDepth(16);
    s_bg.setPsram(true);
    s_bg.createSprite(240, 320);
  }
  canvas.pushSprite(&s_bg, 0, 0);        // freeze whatever is on screen now

  const int CW = 186, CH = 78;
  const int cx = 120;
  uint16_t shadow = lerp565(color, C_BG, 0.55f);
  uint16_t border = contrastOn(color);

  // confetti state (style 2)
  static float fx[46], fy[46], fv[46];
  static uint16_t fc[46];
  for (int i = 0; i < 46; i++) {
    fx[i] = random(0, 240);
    fy[i] = random(-260, -6);
    fv[i] = random(170, 360);
    int r = random(0, 3);
    fc[i] = r == 0 ? color : (r == 1 ? lerp565(color, C_TEXT, 0.45f)
                                     : lerp565(color, 0xFFFF, 0.2f));
  }

  uint32_t t0 = millis(), prev = t0;
  for (;;) {
    uint32_t now = millis();
    float t = (now - t0) / (float)DUR;
    float dt = (now - prev) / 1000.0f;
    prev = now;
    if (t >= 1.0f) break;

    s_bg.pushSprite(&canvas, 0, 0);       // restore background

    // brief coloured edge flash
    if (t < 0.20f) {
      int th = (int)((1.0f - t / 0.20f) * 9) + 2;
      canvas.fillRect(0, 0, 240, th, color);
      canvas.fillRect(0, 320 - th, 240, th, color);
      canvas.fillRect(0, 0, th, 320, color);
      canvas.fillRect(240 - th, 0, th, 320, color);
    }

    // card vertical position: holds centre, then slides up and out
    float cy = 158;
    float slide = t > 0.80f ? (t - 0.80f) / 0.20f : 0.0f;
    cy -= slide * 150.0f;

    if (style == 2) {
      for (int i = 0; i < 46; i++) {
        fy[i] += fv[i] * dt;
        if (fy[i] > 320) { fy[i] = -10; fx[i] = random(0, 240); }
        canvas.fillRect((int)fx[i], (int)fy[i], (i & 1) ? 4 : 5, (i & 1) ? 8 : 5, fc[i]);
      }
    }
    if (style == 1) {
      float p = t < 0.75f ? t / 0.75f : 1.0f;
      for (int k = 0; k < 3; k++) {
        float rr = p * 1.25f - k * 0.24f;
        if (rr <= 0) continue;
        int rad = (int)(rr * 180);
        uint16_t rc = lerp565(color, C_BG, rr);
        canvas.drawCircle(cx, (int)cy, rad, rc);
        canvas.drawCircle(cx, (int)cy, rad + 1, rc);
      }
    }

    float sc = t < 0.22f ? popScale(t / 0.22f) : 1.0f;
    if (slide > 0) sc *= (1.0f - slide * 0.35f);
    int w = (int)(CW * sc), h = (int)(CH * sc);
    int x = cx - w / 2, y = (int)cy - h / 2;

    canvas.fillRoundRect(x + 4, y + 5, w, h, 9, shadow);
    canvas.fillRoundRect(x, y, w, h, 9, color);
    canvas.drawRoundRect(x, y, w, h, 9, border);

    if (sc > 0.55f && slide < 0.5f) {
      canvas.setTextDatum(textdatum_t::middle_center);
      canvas.setTextColor(contrastOn(color));
      setLabelFont((int)strlen(label));
      int sh = (t > 0.2f && t < 0.45f) ? (int)random(-1, 2) : 0;
      canvas.drawString(label, cx + sh, (int)cy - 12);
      canvas.setFont(&fonts::FreeSansBold12pt7b);
      canvas.drawString(pb, cx, (int)cy + 20);
    }

    canvas.pushSprite(0, 0);
    delay(6);
  }

  s_bg.pushSprite(&canvas, 0, 0);         // leave the clean screen for the caller
}
