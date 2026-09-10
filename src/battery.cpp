#include "battery.h"
#include "config.h"
#include <Arduino.h>

// GPIO9 = "Battery voltage ADC value acquisition input" (LCDWIKI ES3C28P spec).
// ADC1_CH8 on the ESP32-S3.
#define BAT_ADC_PIN 9

static float    s_v = 3.9f;            // smoothed battery volts
static float    s_hist[10];            // ~30 s apart -> ~5 min trend window
static int      s_hi = 0;
static bool     s_histFull = false;
static uint32_t s_lastRead = 0, s_lastSample = 0;

static float readRaw() {
  uint32_t sum = 0;
  for (int i = 0; i < 24; i++) sum += analogReadMilliVolts(BAT_ADC_PIN);
  return (sum / 24.0f / 1000.0f) * BAT_DIVIDER + BAT_CAL_OFFSET;
}

void battery_init() {
  analogSetPinAttenuation(BAT_ADC_PIN, ADC_11db);   // ~0..3.1 V usable range
  s_v = readRaw();
  for (int i = 0; i < 10; i++) s_hist[i] = s_v;
}

void battery_update() {
  uint32_t now = millis();
  if (now - s_lastRead < 2000) return;
  s_lastRead = now;

  float r = readRaw();
  if (r > 2.0f && r < 5.0f) s_v += (r - s_v) * 0.25f;

  if (now - s_lastSample > 60000) {          // 10 slots x 60 s = ~10 min trend
    s_lastSample = now;
    s_hist[s_hi] = s_v;
    s_hi = (s_hi + 1) % 10;
    if (s_hi == 0) s_histFull = true;
  }
}

float battery_volts() { return s_v; }

int battery_charging() {
  if (s_v >= 4.17f) return 0;                       // topped off on the charger
  if (s_v >= 4.05f) return 1;                       // above rest-full -> on charger
  float oldest = s_hist[s_hi];                      // up to ~10 min ago
  if ((s_histFull || s_hi > 3) && s_v - oldest > 0.012f) return 1;  // steady climb
  return -1;
}

int battery_percent() {
  float v = s_v;
  static const float lut[][2] = {
    {4.20f, 100}, {4.10f, 92}, {4.00f, 82}, {3.90f, 68}, {3.83f, 55},
    {3.78f, 45},  {3.73f, 35}, {3.68f, 24}, {3.62f, 15}, {3.52f, 7},
    {3.40f, 2},   {3.20f, 0},
  };
  const int n = sizeof(lut) / sizeof(lut[0]);
  if (v >= lut[0][0]) return 100;
  for (int i = 1; i < n; i++) {
    if (v >= lut[i][0]) {
      float f = (v - lut[i][0]) / (lut[i - 1][0] - lut[i][0]);
      return (int)(lut[i][1] + f * (lut[i - 1][1] - lut[i][1]) + 0.5f);
    }
  }
  return 0;
}
