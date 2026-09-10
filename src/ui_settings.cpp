#include "screens.h"
#include "display.h"
#include "settings.h"
#include "battery.h"
#include "theme.h"

bool g_wantRecal = false;
bool g_wantSleep = false;

#define ST_TOP   26
#define ST_ROW   30
#define ST_FOOT  296
#define NROWS    9

static void fmtInterval(char* b, size_t n, uint32_t ms) {
  if (ms < 60000UL) snprintf(b, n, "%lus", (unsigned long)(ms / 1000));
  else              snprintf(b, n, "%lum", (unsigned long)(ms / 60000UL));
}

static void cycle(uint32_t* v, const uint32_t* arr, int n, int dir) {
  int i = preset_index(arr, n, *v);
  *v = arr[(i + dir + n) % n];
}
static void cycle8(uint8_t* v, const uint8_t* arr, int n, int dir) {
  int i = preset_index8(arr, n, *v);
  *v = arr[(i + dir + n) % n];
}

static void drawValRow(int r, const char* label, const char* value, bool button) {
  int y = ST_TOP + r * ST_ROW;
  int my = y + ST_ROW / 2;
  canvas.fillRect(0, y, 240, ST_ROW - 1, (r & 1) ? C_PANEL2 : C_BG);
  canvas.setFont(&fonts::FreeSansBold9pt7b);

  if (button) {
    canvas.setTextColor(C_ACCENT);
    canvas.setTextDatum(textdatum_t::middle_center);
    canvas.drawString(label, 120, my);
    return;
  }
  canvas.setTextColor(C_TEXT);
  canvas.setTextDatum(textdatum_t::middle_left);
  canvas.drawString(label, 10, my);
  if (value) {
    canvas.setTextColor(C_ACCENT);
    canvas.setTextDatum(textdatum_t::middle_right);
    canvas.drawString(value, 218, my);
    canvas.setTextColor(C_DIM);
    canvas.setTextDatum(textdatum_t::middle_left);
    canvas.drawString(">", 226, my);           // tap the row to cycle
  }
}

static void drawBrightnessRow(int r) {
  int y = ST_TOP + r * ST_ROW;
  int my = y + ST_ROW / 2;
  canvas.fillRect(0, y, 240, ST_ROW - 1, (r & 1) ? C_PANEL2 : C_BG);
  canvas.setFont(&fonts::FreeSansBold9pt7b);
  canvas.setTextDatum(textdatum_t::middle_left);
  canvas.setTextColor(C_TEXT);
  canvas.drawString("Brightness", 10, my);

  canvas.setTextDatum(textdatum_t::middle_center);
  canvas.setTextColor(C_ACCENT);
  canvas.drawString("-", 132, my);
  canvas.drawString("+", 226, my);
  int bx = 146, bw = 66;
  canvas.drawRect(bx, my - 6, bw, 12, C_DIM);
  int fill = (int)((long)(g_set.brightness - 10) * (bw - 2) / (255 - 10));
  canvas.fillRect(bx + 1, my - 5, fill, 10, C_ACCENT);
}

Screen scr_settings(const TouchEv& e) {
  bool changed = false;
  static uint32_t s_restartArmed = 0;

  if (e.tap) {
    if (e.y >= ST_FOOT) {
      settings_save();
      if (e.x < 80)       { g_wantSleep = true; return SCR_SETTINGS; }
      if (e.x < 162)      return SCR_WIFI;
      return SCR_LIST;
    }
    int r = (e.y - ST_TOP) / ST_ROW;
    if (e.y >= ST_TOP && r == 8) {                       // Recalibrate | Restart row
      if (e.x < 120) { g_wantRecal = true; s_restartArmed = 0; }
      else if (s_restartArmed && millis() - s_restartArmed < 4000) {
        settings_save(); delay(60); ESP.restart();
      } else s_restartArmed = millis();
    } else if (e.y >= ST_TOP && r >= 0 && r < NROWS) {
      s_restartArmed = 0;
      // brightness has -/+ zones; every other row cycles forward on any tap
      switch (r) {
        case 0:
          if (e.x < 140)      { g_set.brightness = g_set.brightness > 25 ? g_set.brightness - 15 : 10; changed = true; }
          else if (e.x > 200) { g_set.brightness = g_set.brightness < 240 ? g_set.brightness + 15 : 255; changed = true; }
          if (changed) lcd.setBrightness(g_set.brightness);
          break;
        case 1: g_set.theme = (g_set.theme + 1) % THEME_COUNT; theme_set(g_set.theme); changed = true; break;
        case 2: cycle(&g_set.liveMs, LIVE_PRESETS, 4, +1); changed = true; break;
        case 3: cycle(&g_set.idleMs, IDLE_PRESETS, 3, +1); changed = true; break;
        case 4: g_set.bootLeague ^= 1; changed = true; break;
        case 5: g_set.cfbTodayOnly = !g_set.cfbTodayOnly; g_forceRefresh = true; changed = true; break;
        case 6: g_set.scoreAlerts = !g_set.scoreAlerts; changed = true; break;
        case 7: cycle8(&g_set.sleepMin, SLEEP_PRESETS, 5, +1); changed = true; break;
      }
      if (changed) settings_save();
    }
  }

  canvas.fillScreen(C_BG);
  canvas.fillRect(0, 0, 240, ST_TOP - 2, C_PANEL);
  int hy = (ST_TOP - 2) / 2;
  canvas.setFont(&fonts::FreeSansBold9pt7b);
  canvas.setTextDatum(textdatum_t::middle_left);
  canvas.setTextColor(C_TEXT);
  canvas.drawString("Settings", 10, hy);

  char bat[20];
  int bp = battery_percent();
  snprintf(bat, sizeof(bat), "%.2fV  %d%%%s", battery_volts(), bp,
           battery_charging() == 1 ? " +" : "");
  canvas.setFont(&fonts::Font2);
  canvas.setTextColor(bp <= 12 ? C_LIVE : (bp <= 30 ? C_WARN : C_DIM));
  canvas.setTextDatum(textdatum_t::middle_right);
  canvas.drawString(bat, 232, hy);

  char live[8], idle[8], slp[8];
  fmtInterval(live, sizeof(live), g_set.liveMs);
  fmtInterval(idle, sizeof(idle), g_set.idleMs);
  if (g_set.sleepMin == 0) strcpy(slp, "Off");
  else snprintf(slp, sizeof(slp), "%um", g_set.sleepMin);

  drawBrightnessRow(0);
  drawValRow(1, "Theme",         THEMES[g_set.theme].name, false);
  drawValRow(2, "Live refresh",  live, false);
  drawValRow(3, "Idle refresh",  idle, false);
  drawValRow(4, "Boot league",   g_set.bootLeague ? "NCAAF" : "NFL", false);
  drawValRow(5, "NCAAF today",    g_set.cfbTodayOnly ? "ON" : "OFF", false);
  drawValRow(6, "Score alerts",  g_set.scoreAlerts ? "ON" : "OFF", false);
  drawValRow(7, "Auto-sleep",    slp, false);
  {                                                    // row 8: two buttons
    int y = ST_TOP + 8 * ST_ROW, my = y + ST_ROW / 2;
    canvas.fillRect(0, y, 240, ST_ROW - 1, C_PANEL2);
    canvas.drawFastVLine(120, y + 4, ST_ROW - 9, C_PANEL);
    canvas.setFont(&fonts::FreeSansBold9pt7b);
    canvas.setTextDatum(textdatum_t::middle_center);
    canvas.setTextColor(C_ACCENT);
    canvas.drawString("Recalibrate", 60, my);
    bool armed = s_restartArmed && millis() - s_restartArmed < 4000;
    canvas.setTextColor(armed ? C_LIVE : C_ACCENT);
    canvas.drawString(armed ? "Confirm?" : "Restart", 180, my);
  }

  canvas.fillRect(0, ST_FOOT, 240, 24, C_PANEL2);
  canvas.setFont(&fonts::Font2);
  canvas.setTextDatum(textdatum_t::middle_center);
  canvas.setTextColor(C_DIM);
  canvas.drawString("SLEEP", 40, ST_FOOT + 12);
  canvas.drawString("WI-FI", 121, ST_FOOT + 12);
  canvas.setTextColor(C_ACCENT);
  canvas.drawString("DONE", 202, ST_FOOT + 12);
  canvas.drawFastVLine(80, ST_FOOT + 4, 16, C_PANEL);
  canvas.drawFastVLine(162, ST_FOOT + 4, 16, C_PANEL);

  canvas.pushSprite(0, 0);
  return SCR_SETTINGS;
}
