#include "settings.h"
#include "config.h"
#include "theme.h"
#include <Preferences.h>

Settings g_set;

const uint32_t LIVE_PRESETS[4] = {20000UL, 30000UL, 60000UL, 120000UL};
const uint32_t IDLE_PRESETS[3] = {60000UL, 300000UL, 900000UL};
const uint8_t  SLEEP_PRESETS[5] = {0, 5, 15, 30, 60};

int preset_index(const uint32_t* arr, int n, uint32_t v) {
  for (int i = 0; i < n; i++) if (arr[i] == v) return i;
  return 0;
}
int preset_index8(const uint8_t* arr, int n, uint8_t v) {
  for (int i = 0; i < n; i++) if (arr[i] == v) return i;
  return 0;
}

void settings_load() {
  g_set.brightness   = BACKLIGHT_LEVEL;
  g_set.liveMs       = POLL_LIVE_MS;
  g_set.idleMs       = POLL_IDLE_MS;
  g_set.bootLeague   = 0;
  g_set.cfbTodayOnly = false;
  g_set.scoreAlerts  = true;
  g_set.sleepMin     = 0;
  g_set.theme        = 0;

  Preferences p;
  if (p.begin("grid_cfg", true)) {
    g_set.brightness   = p.getUChar("bri",  g_set.brightness);
    g_set.liveMs       = p.getULong("live", g_set.liveMs);
    g_set.idleMs       = p.getULong("idle", g_set.idleMs);
    g_set.bootLeague   = p.getUChar("lg",   g_set.bootLeague);
    g_set.cfbTodayOnly = p.getBool ("cfd",  g_set.cfbTodayOnly);
    g_set.scoreAlerts  = p.getBool ("alrt", g_set.scoreAlerts);
    g_set.sleepMin     = p.getUChar("slp",  g_set.sleepMin);
    g_set.theme        = p.getUChar("thm",  g_set.theme);
    p.end();
  }

  if (g_set.brightness < 10) g_set.brightness = 10;
  if (g_set.liveMs < 20000UL) g_set.liveMs = 20000UL;   // keep the API load sane
  if (g_set.bootLeague > 1)  g_set.bootLeague = 0;
  if (g_set.theme >= THEME_COUNT) g_set.theme = 0;
  theme_set(g_set.theme);
}

void settings_save() {
  Preferences p;
  if (p.begin("grid_cfg", false)) {
    p.putUChar("bri",  g_set.brightness);
    p.putULong("live", g_set.liveMs);
    p.putULong("idle", g_set.idleMs);
    p.putUChar("lg",   g_set.bootLeague);
    p.putBool ("cfd",  g_set.cfbTodayOnly);
    p.putBool ("alrt", g_set.scoreAlerts);
    p.putUChar("slp",  g_set.sleepMin);
    p.putUChar("thm",  g_set.theme);
    p.end();
  }
}
