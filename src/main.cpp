#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "ui.h"
#include "display.h"
#include "touch.h"
#include "games.h"
#include "net.h"
#include "screens.h"
#include "settings.h"
#include "battery.h"
#include "news.h"
#include "stats.h"
#include "wifi_cfg.h"

#define WAKE_BTN_PIN   0      // BOOT button, active low

static Screen   s_screen   = SCR_BOOT;
static uint32_t s_lastDraw  = 0;
static uint32_t s_lastTouch = 0;

// Standby: backlight + Wi-Fi off, then poll the touch chip until a tap (or
// BOOT). No lcd.sleep() — it can wedge this panel (board doc 4.4). No
// esp_light_sleep_start() — it hangs on this S3 + octal-PSRAM board. Draws
// ~45-55 mA, so ~2-3 days of standby on 3000 mAh; a tap always wakes it.
static void enterSleep() {
  canvas.fillScreen(C_BG);
  canvas.setTextDatum(textdatum_t::middle_center);
  canvas.setFont(&fonts::FreeSansBold12pt7b);
  canvas.setTextColor(C_ACCENT);
  canvas.drawString("Sleeping", 120, 150);
  canvas.setFont(&fonts::Font2);
  canvas.setTextColor(C_DIM);
  canvas.drawString("tap the screen to wake", 120, 178);
  canvas.pushSprite(0, 0);

  g_sleepReq = true;                 // park the network task
  delay(150);
  WiFi.mode(WIFI_OFF);

  uint32_t rel = millis();
  while (touch_is_touched() && millis() - rel < 3000) delay(20);   // wait for release
  delay(60);
  lcd.setBrightness(0);
  Serial.println("[power] standby");

  for (;;) {
    if (touch_is_touched() || digitalRead(WAKE_BTN_PIN) == LOW) break;
    delay(110);
  }

  Serial.println("[power] wake");
  lcd.setBrightness(g_set.brightness);
  WiFi.mode(WIFI_STA);
  touch_bus_resume();
  g_sleepReq = false;
  g_forceRefresh = true;

  rel = millis();
  while (touch_is_touched() && millis() - rel < 2000) delay(20);   // swallow wake tap
  touch_poll();
  s_lastTouch = millis();
  s_lastDraw  = 0;
}

void setup() {
  Serial.begin(115200);
  uint32_t t = millis();
  while (!Serial && millis() - t < 1200) delay(10);   // bounded — see board doc 4.2

  esp_sleep_wakeup_cause_t wc = esp_sleep_get_wakeup_cause();
  Serial.printf("\n[boot] The Game Box (wake cause %d)\n", (int)wc);
  Serial.printf("[boot] PSRAM: %u bytes free\n", (unsigned)ESP.getFreePsram());

  settings_load();
  wifi_cfg_load();
  g_league = g_set.bootLeague;
  g_uiLeague = g_league;

  games_init();
  news_init();
  stats_init();
  battery_init();
  display_init();                 // kills the RGB LED, applies g_set.brightness
  touch_init();
  scr_boot_draw();

  // Calibrate touch if never done, or if a finger is held on the screen now.
  {
    int rx, ry;
    bool held = false;
    uint32_t t0 = millis();
    while (millis() - t0 < 700) {
      if (touch_readRaw(&rx, &ry)) { held = true; break; }
      delay(20);
    }
    if (!touch_isCalibrated() || held) display_calibrate();
    scr_boot_draw();
  }

  if (!wifi_cfg_have()) s_screen = SCR_WIFI;   // no credentials yet -> run the wizard

  s_lastTouch = millis();
  net_start();
}

void loop() {
  if (Serial.available() && Serial.read() == 's') enterSleep();   // 's' = sleep (test)

  TouchEv e = touch_poll();
  uint32_t now = millis();
  if (e.tap || e.dragging) s_lastTouch = now;

  battery_update();
  static uint32_t s_batLog = 0;
  if (now - s_batLog > 60000) {
    s_batLog = now;
    Serial.printf("[bat] %.2f V  %d%%  chg=%d\n",
                  battery_volts(), battery_percent(), battery_charging());
  }

  if (s_screen == SCR_BOOT) {
    if (g_firstCycleDone) {
      s_screen = SCR_LIST;
      g_scoresOnScreen = true;
    } else {
      if (now - s_lastDraw > 180) { scr_boot_draw(); s_lastDraw = now; }
      delay(15);
      return;
    }
  }

  bool active = e.tap || e.dragging;
  if (active || now - s_lastDraw > 500) {
    Screen prev = s_screen;
    switch (s_screen) {
      case SCR_LIST:     s_screen = scr_list(e);     break;
      case SCR_DETAIL:   s_screen = scr_detail(e);   break;
      case SCR_SCHEDULE: s_screen = scr_schedule(e); break;
      case SCR_SETTINGS: s_screen = scr_settings(e); break;
      case SCR_NEWS:     s_screen = scr_news(e);     break;
      case SCR_NEWSITEM: s_screen = scr_newsitem(e); break;
      case SCR_STATS:    s_screen = scr_stats(e);    break;
      case SCR_WIFI:     s_screen = scr_wifi(e);     break;
      case SCR_WIFIKEY:  s_screen = scr_wifikey(e);  break;
      default: break;
    }
    s_lastDraw = now;

    // The net task only polls scores while the list or a game detail is up.
    // Coming back to either from elsewhere triggers an immediate pull.
    bool wasScores = (prev == SCR_LIST || prev == SCR_DETAIL);
    bool nowScores = (s_screen == SCR_LIST || s_screen == SCR_DETAIL);
    g_scoresOnScreen = nowScores;
    if (nowScores && !wasScores) g_forceRefresh = true;
  }

  if (g_wantRecal) {
    g_wantRecal = false;
    display_calibrate();
    s_lastDraw = 0;
  }

  if (g_wantSleep ||
      (g_set.sleepMin && now - s_lastTouch > (uint32_t)g_set.sleepMin * 60000UL)) {
    g_wantSleep = false;
    enterSleep();
  }

  delay(12);
}
