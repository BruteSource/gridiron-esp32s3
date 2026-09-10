#pragma once
#include <Arduino.h>

struct Settings {
  uint8_t  brightness;     // 10..255
  uint32_t liveMs;         // refresh interval while a game is live
  uint32_t idleMs;         // refresh interval when nothing is live
  uint8_t  bootLeague;     // 0 = NFL, 1 = NCAAF
  bool     cfbTodayOnly;   // NCAAF: fetch only today's slate (much smaller)
  bool     scoreAlerts;    // full-screen animation on a score change
  uint8_t  sleepMin;       // deep-sleep after N min idle (0 = never)
  uint8_t  theme;          // 0..THEME_COUNT-1
};

extern Settings g_set;

void settings_load();   // from NVS "grid_cfg", defaults from config.h
void settings_save();

// selectable presets for the settings UI
extern const uint32_t LIVE_PRESETS[4];
extern const uint32_t IDLE_PRESETS[3];
extern const uint8_t  SLEEP_PRESETS[5];
int  preset_index(const uint32_t* arr, int n, uint32_t v);
int  preset_index8(const uint8_t* arr, int n, uint8_t v);
