#include "games.h"
#include <algorithm>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t s_mtx;
static Game s_nfl[MAX_NFL]; static int s_nflN = 0;
static Game s_cfb[MAX_CFB]; static int s_cfbN = 0;

// ---- team schedule ------------------------------------------------------
volatile int g_schedState = SCH_IDLE;

static SemaphoreHandle_t s_smtx;
static SchedGame s_sch[MAX_SCHED];
static int  s_schN = 0;
static int  s_schFirstUpcoming = 0;
static char s_schName[26] = "";
static char s_schRecord[16] = "";
static int  s_reqLeague = -1;
static char s_reqId[8] = "";
static char s_reqName[26] = "";
static bool s_reqPending = false;

volatile bool     g_wifiConnected  = false;
volatile bool     g_timeSynced     = false;
volatile bool     g_firstCycleDone = false;
volatile bool     g_anyLive        = false;
volatile bool     g_forceRefresh   = false;
volatile bool     g_sleepReq       = false;
volatile int      g_httpStatus[2]  = {0, 0};
volatile uint32_t g_lastUpdateMs[2] = {0, 0};
char              g_bootMsg[48]    = "Starting up";

// ---- play-by-play (single focused game) -------------------------------
static SemaphoreHandle_t s_pmtx;
static char s_pFocus[12]  = "";
static char s_pLastKey[40] = "";
static char s_pPlays[PBP_KEEP][160];
static int  s_pN = 0;

void pbp_focus(const char* gameId) {
  xSemaphoreTake(s_pmtx, portMAX_DELAY);
  if (strcmp(s_pFocus, gameId) != 0) {
    strlcpy(s_pFocus, gameId, sizeof(s_pFocus));
    s_pLastKey[0] = 0;
    s_pN = 0;
  }
  xSemaphoreGive(s_pmtx);
}

void pbp_note(const char* gameId, const char* playId, const char* text) {
  if (!text || !*text) return;
  xSemaphoreTake(s_pmtx, portMAX_DELAY);
  if (strcmp(gameId, s_pFocus) == 0) {
    const char* key = (playId && *playId) ? playId : text;
    if (strcmp(key, s_pLastKey) != 0) {
      strlcpy(s_pLastKey, key, sizeof(s_pLastKey));
      if (s_pN < PBP_KEEP) s_pN++;
      for (int i = s_pN - 1; i > 0; i--) strcpy(s_pPlays[i], s_pPlays[i - 1]);
      strlcpy(s_pPlays[0], text, sizeof(s_pPlays[0]));
    }
  }
  xSemaphoreGive(s_pmtx);
}

int pbp_get(char out[][160], int cap) {
  xSemaphoreTake(s_pmtx, portMAX_DELAY);
  int n = s_pN < cap ? s_pN : cap;
  for (int i = 0; i < n; i++) strcpy(out[i], s_pPlays[i]);
  xSemaphoreGive(s_pmtx);
  return n;
}

void games_init() {
  s_mtx  = xSemaphoreCreateMutex();
  s_smtx = xSemaphoreCreateMutex();
  s_pmtx = xSemaphoreCreateMutex();
}

// ---- schedule request / response --------------------------------------
void schedule_request(int league, const char* teamId, const char* teamName) {
  xSemaphoreTake(s_smtx, portMAX_DELAY);
  s_reqLeague = league;
  strlcpy(s_reqId, teamId, sizeof(s_reqId));
  strlcpy(s_reqName, teamName, sizeof(s_reqName));
  s_reqPending = true;
  s_schN = 0;
  strlcpy(s_schName, teamName, sizeof(s_schName));
  s_schRecord[0] = 0;
  xSemaphoreGive(s_smtx);
  g_schedState = SCH_LOADING;
}

bool schedule_take_request(int* league, char* teamId, char* teamName) {
  bool have = false;
  xSemaphoreTake(s_smtx, portMAX_DELAY);
  if (s_reqPending) {
    *league = s_reqLeague;
    strcpy(teamId, s_reqId);
    strcpy(teamName, s_reqName);
    s_reqPending = false;
    have = true;
  }
  xSemaphoreGive(s_smtx);
  return have;
}

void schedule_publish(const SchedGame* src, int n, int firstUpcoming,
                      const char* teamName, const char* record) {
  xSemaphoreTake(s_smtx, portMAX_DELAY);
  if (n > MAX_SCHED) n = MAX_SCHED;
  memcpy(s_sch, src, (size_t)n * sizeof(SchedGame));
  s_schN = n;
  s_schFirstUpcoming = firstUpcoming;
  strlcpy(s_schName, teamName, sizeof(s_schName));
  strlcpy(s_schRecord, record, sizeof(s_schRecord));
  xSemaphoreGive(s_smtx);
  g_schedState = SCH_READY;
}

void schedule_fail() { g_schedState = SCH_ERROR; }

int schedule_snapshot(SchedGame* out, int cap, char* nameOut, char* recordOut,
                      int* firstUpcomingOut) {
  xSemaphoreTake(s_smtx, portMAX_DELAY);
  int n = s_schN; if (n > cap) n = cap;
  memcpy(out, s_sch, (size_t)n * sizeof(SchedGame));
  if (nameOut)   strcpy(nameOut, s_schName);
  if (recordOut) strcpy(recordOut, s_schRecord);
  if (firstUpcomingOut) *firstUpcomingOut = s_schFirstUpcoming;
  xSemaphoreGive(s_smtx);
  return n;
}

static int clockSecs(const char* c) {
  int m = 0, s = 0;
  if (sscanf(c, "%d:%d", &m, &s) == 2) return m * 60 + s;
  return 0;
}

// group order: 0 live, 1 upcoming, 2 final
static int groupOf(const Game& g) {
  return g.state == 1 ? 0 : (g.state == 0 ? 1 : 2);
}

static bool cmpGames(const Game& a, const Game& b) {
  int ga = groupOf(a), gb = groupOf(b);
  if (ga != gb) return ga < gb;
  if (ga == 0) {                                   // live
    if (a.period != b.period) return a.period > b.period;
    return clockSecs(a.clock) < clockSecs(b.clock); // closer to end of quarter first
  }
  if (ga == 1) return a.kickoff < b.kickoff;        // soonest kickoff first
  return a.kickoff > b.kickoff;                     // most recent final first
}

int games_snapshot(int league, Game* out, int cap) {
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  const Game* src = league == 0 ? s_nfl : s_cfb;
  int n = league == 0 ? s_nflN : s_cfbN;
  if (n > cap) n = cap;
  memcpy(out, src, (size_t)n * sizeof(Game));
  xSemaphoreGive(s_mtx);
  std::sort(out, out + n, cmpGames);
  return n;
}

void games_publish(int league, const Game* src, int n) {
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  if (league == 0) {
    if (n > MAX_NFL) n = MAX_NFL;
    memcpy(s_nfl, src, (size_t)n * sizeof(Game));
    s_nflN = n;
  } else {
    if (n > MAX_CFB) n = MAX_CFB;
    memcpy(s_cfb, src, (size_t)n * sizeof(Game));
    s_cfbN = n;
  }
  g_lastUpdateMs[league] = millis();

  bool live = false;
  for (int i = 0; i < s_nflN; i++) if (s_nfl[i].state == 1) { live = true; break; }
  if (!live) for (int i = 0; i < s_cfbN; i++) if (s_cfb[i].state == 1) { live = true; break; }
  g_anyLive = live;
  xSemaphoreGive(s_mtx);
}
