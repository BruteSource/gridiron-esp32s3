#pragma once
#include <Arduino.h>
#include <time.h>
#include "config.h"

// A single game, flattened from the ESPN scoreboard JSON.
struct Game {
  char     id[12];
  char     away[6],  home[6];        // team abbreviations (e.g. "KC")
  char     awayId[8], homeId[8];     // ESPN team ids (for schedule lookup)
  char     awayName[22], homeName[22]; // short display names
  uint8_t  awayScore, homeScore;
  uint8_t  awayRank,  homeRank;      // AP rank 1..25, 0 = unranked
  uint16_t awayColor, homeColor;     // RGB565 team color
  uint8_t  state;                    // 0 = pre, 1 = in progress, 2 = final
  uint8_t  period;                   // quarter (5+ = OT)
  char     clock[8];                 // "8:34"
  char     detail[36];               // ESPN status.type.shortDetail
  char     tv[14];                   // broadcast network
  char     venue[42];
  time_t   kickoff;                  // epoch seconds (UTC), 0 if unknown
  uint8_t  league;                   // 0 = NFL, 1 = CFB
  // live situation (only meaningful when state == 1)
  char     downDist[42];             // "3rd & 7 at DAL 45"
  char     possId[8];                // team id with the ball (== away/homeId)
  uint8_t  toHome, toAway;           // timeouts remaining (0-3)
  bool     redZone;
  char     weather[22];              // "72F Sunny" ("" if none / indoor)
};

// One game (or bye) in a team's season schedule.
struct SchedGame {
  char    opp[6], oppName[22];
  bool    home;                 // true = "vs", false = "@"
  uint8_t state;                // 0 pre / 1 in / 2 post
  bool    win;                  // valid when state == 2
  uint8_t teamScore, oppScore;
  char    weekLabel[12];        // "Wk 5", "Bowl", ...
  uint8_t week;                 // numeric week (0 if unknown)
  bool    reg;                  // regular-season game
  bool    bye;                  // synthesized bye-week row
  time_t  date;
  char    tv[12];
};
#define MAX_SCHED 32

enum { SCH_IDLE, SCH_LOADING, SCH_READY, SCH_ERROR };
extern volatile int g_schedState;

void games_init();
void games_cache_load();   // restore the last-known slate + TV map (called by games_init)

// Broadcast map, keyed by "AWY@HOM", persisted with the slate cache so networks
// stay visible even when we can't poll the media feed.
void        tv_set(const char* away, const char* home, const char* tv);
const char* tv_get(const char* away, const char* home);

// ---- play-by-play for the game currently open on the detail screen -------
#define PBP_KEEP 8
void pbp_focus(const char* gameId);                          // UI: now viewing
void pbp_note(const char* gameId, const char* playId, const char* text);  // net
int  pbp_get(char out[][160], int cap);                      // UI: newest first

// UI marks which game the detail screen is on; the net task polls ESPN for its
// live clock / play-by-play / situation only while a *live* game is focused.
void detail_focus(int league, const char* gameId, bool live);
bool detail_focus_get(int* league, char* gameId);           // net side
bool games_find(int league, const char* id, Game* out);
void games_patch_live(const char* id, uint8_t period, const char* clock,
                      const char* detail, const char* downDist, int possSide,
                      uint8_t toAway, uint8_t toHome, bool redZone,
                      int awayScore, int homeScore);

// ---- team schedule (on-demand, one request at a time) -------------------
// UI: fire a request (sets state = LOADING) and read what team/league it wants.
void schedule_request(int league, const char* teamId, const char* teamName);
bool schedule_take_request(int* league, char* teamId, char* teamName);  // net side
void schedule_publish(const SchedGame* src, int n, int firstUpcoming,
                      const char* teamName, const char* record);
void schedule_fail();
// UI read; returns count, fills name/record/firstUpcoming if non-null.
int  schedule_snapshot(SchedGame* out, int cap, char* nameOut, char* recordOut,
                       int* firstUpcomingOut);

// Copy one league's games into `out` (up to `cap`), sorted for display
// (live first, then upcoming by kickoff, then finals). Returns the count.
// Safe to call from the UI task.
int  games_snapshot(int league, Game* out, int cap);

// Replace a league's stored games. Called from the network task.
void games_publish(int league, const Game* src, int n);

// Patch fresh games into a league's store by id (degraded-fallback path).
void games_merge(int league, const Game* src, int n);

// ---- shared status flags (display only; benign races are acceptable) -----
extern volatile bool     g_wifiConnected;
extern volatile bool     g_timeSynced;
extern volatile bool     g_firstCycleDone;   // net task finished its first pass
extern volatile bool     g_anyLive;          // any game in progress right now
extern volatile bool     g_forceRefresh;     // UI asks the net task to poll now
extern volatile int      g_uiLeague;         // league on screen (net polls this one)
extern volatile bool     g_scoresOnScreen;   // UI is on the list or a game detail
extern volatile bool     g_sleepReq;         // UI wants the net task quiet (sleeping)
extern volatile int      g_httpStatus[2];    // last HTTP code per league
extern volatile uint32_t g_lastUpdateMs[2];  // millis() of last good fetch
extern char              g_bootMsg[48];      // status line for the boot screen
