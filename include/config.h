#pragma once
#include "credentials.h"   // WIFI_SSID, WIFI_PASS

// ---------------------------------------------------------------------------
// Local time — POSIX TZ string. Kickoff times are shown in this zone.
//   US Eastern : "EST5EDT,M3.2.0,M11.1.0"
//   US Central : "CST6CDT,M3.2.0,M11.1.0"
//   US Mountain: "MST7MDT,M3.2.0,M11.1.0"
//   US Pacific : "PST8PDT,M3.2.0,M11.1.0"
// ---------------------------------------------------------------------------
#define TZ_STRING   "PST8PDT,M3.2.0,M11.1.0"

// Poll intervals (milliseconds)
#define POLL_LIVE_MS   20000UL     // when a game is in progress
#define POLL_IDLE_MS   300000UL    // when nothing is live
#define POLL_RETRY_MS  15000UL     // after a failed fetch

// Optional: force a specific slate for testing, e.g. "20250906" (YYYYMMDD).
// Leave "" to use today's games.
#define DATES_OVERRIDE ""

// Screen brightness 0-255
#define BACKLIGHT_LEVEL 220

// On-board WS2812 RGB LED (GPIO 42). Held off to save power; set to 1 to allow
// using it later.
#define RGB_LED_PIN 42
#define RGB_LED_ENABLE 0

// Battery sense on GPIO9 (ADC1_CH8). BAT_DIVIDER = Vbattery / V(GPIO9); the
// on-board resistor divider is almost certainly 2.0 (calibrate against a known
// battery voltage — watch the [bat] serial line). Small linear trim via offset.
#define BAT_DIVIDER     1.955f   // calibrated: meter 3.50 V vs pin ~1.79 V on this unit
#define BAT_CAL_OFFSET  0.0f

// Capacity limits (RAM cost ~200 bytes/game)
#define MAX_NFL 24
#define MAX_CFB 64

// ESPN public scoreboard endpoints (no API key required)
#define NFL_URL "https://site.api.espn.com/apis/site/v2/sports/football/nfl/scoreboard"
#define CFB_URL "https://site.api.espn.com/apis/site/v2/sports/football/college-football/scoreboard?groups=80&limit=300"
