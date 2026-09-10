#pragma once
#include <Arduino.h>

// Team box-score comparison for one game, fetched on demand from ESPN's
// /summary endpoint (big response, filtered down to boxscore.teams).
#define MAX_STAT_ROWS 12
#define MAX_PLAYERS   22

struct StatRow {
  char label[20];
  char away[12];
  char home[12];
};
struct PlayerRow {
  uint8_t side;      // 0 = away, 1 = home
  char    cat;       // 'P' passing, 'R' rushing, 'C' receiving
  char    name[16];  // "D. Maye"
  char    line[22];  // "23/33  178yd  1TD"
};
struct GameStats {
  char      away[6], home[6];
  StatRow   rows[MAX_STAT_ROWS];
  int       n;
  PlayerRow players[MAX_PLAYERS];
  int       pn;
};

enum { STATS_IDLE, STATS_LOADING, STATS_READY, STATS_ERROR };
extern volatile int g_statsState;

void stats_init();
void stats_request(int league, const char* gameId);   // UI: fire a fetch
void stats_reload();                                   // UI: re-fetch last game
bool stats_take_request(int* league, char* gameId);    // net task: claim request
void stats_publish(const GameStats* src);
void stats_fail();
bool stats_snapshot(GameStats* out);                   // true if READY
