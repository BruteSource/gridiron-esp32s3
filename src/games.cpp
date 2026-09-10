#include "games.h"
#include <algorithm>
#include <string.h>
#include <LittleFS.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t s_mtx;
static Game s_nfl[MAX_NFL]; static int s_nflN = 0;
static Game s_cfb[MAX_CFB]; static int s_cfbN = 0;

// Persist the last-known slate so upcoming games / finals survive a reboot while
// a data source is unreachable. Kept in LittleFS (the NVS partition is too small
// for a ~7 KB blob alongside settings/wifi/calibration).
#define CACHE_N   12
#define TV_MAP_N  96
#define CACHE_PATH "/slate.bin"
struct TvEnt  { char key[14]; char tv[14]; };
struct GameCache {
  uint32_t magic; uint8_t nflN, cfbN, tvN;
  Game nfl[CACHE_N]; Game cfb[CACHE_N];
  TvEnt tv[TV_MAP_N];
};
static GameCache s_gc;               // ~10 KB scratch, shared by load & save
static bool s_fsOk = false;

// ---- broadcast map (part of the persisted blob) ----------------------
static TvEnt s_tv[TV_MAP_N];
static int   s_tvN = 0;

void tv_set(const char* away, const char* home, const char* tv) {
  if (!tv || !*tv || !away || !*away || !home || !*home) return;
  char k[14]; snprintf(k, sizeof(k), "%s@%s", away, home);
  for (int i = 0; i < s_tvN; i++)
    if (!strcmp(s_tv[i].key, k)) { strlcpy(s_tv[i].tv, tv, 14); return; }
  if (s_tvN < TV_MAP_N) {
    strlcpy(s_tv[s_tvN].key, k, 14);
    strlcpy(s_tv[s_tvN].tv, tv, 14);
    s_tvN++;
  }
}
const char* tv_get(const char* away, const char* home) {
  char k[14]; snprintf(k, sizeof(k), "%s@%s", away, home);
  for (int i = 0; i < s_tvN; i++) if (!strcmp(s_tv[i].key, k)) return s_tv[i].tv;
  return "";
}

static void cacheFsInit() {
  static bool tried = false;
  if (tried) return;
  tried = true;
  s_fsOk = LittleFS.begin(true);     // format on first use
  if (!s_fsOk) Serial.println("[cache] LittleFS mount failed");
}

void games_cache_load() {
  cacheFsInit();
  if (!s_fsOk) return;
  File f = LittleFS.open(CACHE_PATH, "r");
  if (!f) return;
  size_t got = f.read((uint8_t*)&s_gc, sizeof(s_gc));
  f.close();
  if (got != sizeof(s_gc) || s_gc.magic != 0x47524945) return;   // "GRIE" (v2)
  Serial.printf("[cache] restored %u NFL + %u CFB games, %u TV\n",
                s_gc.nflN, s_gc.cfbN, s_gc.tvN);

  xSemaphoreTake(s_mtx, portMAX_DELAY);
  s_nflN = s_gc.nflN <= CACHE_N ? s_gc.nflN : CACHE_N;
  s_cfbN = s_gc.cfbN <= CACHE_N ? s_gc.cfbN : CACHE_N;
  memcpy(s_nfl, s_gc.nfl, (size_t)s_nflN * sizeof(Game));
  memcpy(s_cfb, s_gc.cfb, (size_t)s_cfbN * sizeof(Game));
  for (int i = 0; i < s_nflN; i++) if (s_nfl[i].state == 1) s_nfl[i].state = 2;  // stale
  for (int i = 0; i < s_cfbN; i++) if (s_cfb[i].state == 1) s_cfb[i].state = 2;
  xSemaphoreGive(s_mtx);

  s_tvN = s_gc.tvN <= TV_MAP_N ? s_gc.tvN : TV_MAP_N;
  memcpy(s_tv, s_gc.tv, (size_t)s_tvN * sizeof(TvEnt));
}

static void games_cache_save() {
  static uint32_t s_lastSave = 0;
  if (!s_fsOk) return;
  if (s_lastSave && millis() - s_lastSave < 300000UL) return;   // <= once / 5 min

  xSemaphoreTake(s_mtx, portMAX_DELAY);
  if (s_nflN == 0 && s_cfbN == 0) { xSemaphoreGive(s_mtx); return; }
  s_gc.magic = 0x47524945;
  s_gc.nflN = s_nflN < CACHE_N ? s_nflN : CACHE_N;
  s_gc.cfbN = s_cfbN < CACHE_N ? s_cfbN : CACHE_N;
  memcpy(s_gc.nfl, s_nfl, (size_t)s_gc.nflN * sizeof(Game));
  memcpy(s_gc.cfb, s_cfb, (size_t)s_gc.cfbN * sizeof(Game));
  xSemaphoreGive(s_mtx);

  s_gc.tvN = (uint8_t)(s_tvN < TV_MAP_N ? s_tvN : TV_MAP_N);
  memcpy(s_gc.tv, s_tv, (size_t)s_gc.tvN * sizeof(TvEnt));

  File f = LittleFS.open(CACHE_PATH, "w");
  if (!f) return;
  size_t w = f.write((uint8_t*)&s_gc, sizeof(s_gc));
  f.close();
  s_lastSave = millis();
  Serial.printf("[cache] saved %u NFL + %u CFB (%u B)\n", s_gc.nflN, s_gc.cfbN, (unsigned)w);
}

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
volatile int      g_uiLeague       = 0;
volatile bool     g_scoresOnScreen = false;
volatile bool     g_sleepReq       = false;
volatile int      g_httpStatus[2]  = {0, 0};
volatile uint32_t g_lastUpdateMs[2] = {0, 0};
char              g_bootMsg[48]    = "Starting up";

// ---- play-by-play + focused-game detail poll -------------------------
static SemaphoreHandle_t s_pmtx;
static char     s_pFocus[12]  = "";
static char     s_pLastKey[40] = "";
static char     s_pPlays[PBP_KEEP][160];
static int      s_pN = 0;
static int      s_dfLeague = 0;
static bool     s_dfLive   = false;
static uint32_t s_dfAt     = 0;

void pbp_focus(const char* gameId) {
  xSemaphoreTake(s_pmtx, portMAX_DELAY);
  if (strcmp(s_pFocus, gameId) != 0) {
    strlcpy(s_pFocus, gameId, sizeof(s_pFocus));
    s_pLastKey[0] = 0;
    s_pN = 0;
  }
  xSemaphoreGive(s_pmtx);
}

// UI: the detail screen is showing this game (call every frame).
void detail_focus(int league, const char* gameId, bool live) {
  pbp_focus(gameId);
  xSemaphoreTake(s_pmtx, portMAX_DELAY);
  s_dfLeague = league;
  s_dfLive   = live;
  s_dfAt     = millis();
  xSemaphoreGive(s_pmtx);
}

// net task: is a live game focused right now (UI still updating it)?
bool detail_focus_get(int* league, char* gameId) {
  xSemaphoreTake(s_pmtx, portMAX_DELAY);
  bool ok = s_dfLive && s_pFocus[0] && (millis() - s_dfAt < 4000);
  if (ok) { *league = s_dfLeague; strcpy(gameId, s_pFocus); }
  xSemaphoreGive(s_pmtx);
  return ok;
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
  games_cache_load();
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

  games_cache_save();                 // rate-limited inside
}

// Patch fresh games into a league's store by id (update matches, append new),
// leaving everything else — used for the degraded fallback when the primary
// source is blocked. Does NOT bump g_lastUpdateMs (the slate is still mostly old).
// Copy one stored game by id into `out`. Returns false if not found.
bool games_find(int league, const char* id, Game* out) {
  bool found = false;
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  const Game* arr = league == 0 ? s_nfl : s_cfb;
  int nn = league == 0 ? s_nflN : s_cfbN;
  for (int i = 0; i < nn; i++)
    if (!strcmp(arr[i].id, id)) { *out = arr[i]; found = true; break; }
  xSemaphoreGive(s_mtx);
  return found;
}

// Patch live in-game fields into one stored game (from the focused-game poll).
// possSide: -1 nobody, 0 away, 1 home. Negative scores mean "leave unchanged".
void games_patch_live(const char* id, uint8_t period, const char* clock,
                      const char* detail, const char* downDist, int possSide,
                      uint8_t toAway, uint8_t toHome, bool redZone,
                      int awayScore, int homeScore) {
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  for (int L = 0; L < 2; L++) {
    Game* arr = L == 0 ? s_nfl : s_cfb;
    int nn = L == 0 ? s_nflN : s_cfbN;
    for (int i = 0; i < nn; i++) {
      if (strcmp(arr[i].id, id)) continue;
      arr[i].state  = 1;
      arr[i].period = period;
      strlcpy(arr[i].clock, clock, sizeof(arr[i].clock));
      strlcpy(arr[i].detail, detail, sizeof(arr[i].detail));
      strlcpy(arr[i].downDist, downDist, sizeof(arr[i].downDist));
      arr[i].toAway = toAway; arr[i].toHome = toHome;
      arr[i].redZone = redZone;
      if (possSide == 0)      strlcpy(arr[i].possId, arr[i].awayId, sizeof(arr[i].possId));
      else if (possSide == 1) strlcpy(arr[i].possId, arr[i].homeId, sizeof(arr[i].possId));
      else                    arr[i].possId[0] = 0;
      if (awayScore >= 0) arr[i].awayScore = (uint8_t)constrain(awayScore, 0, 255);
      if (homeScore >= 0) arr[i].homeScore = (uint8_t)constrain(homeScore, 0, 255);
      g_anyLive = true;
      xSemaphoreGive(s_mtx);
      return;
    }
  }
  xSemaphoreGive(s_mtx);
}

void games_merge(int league, const Game* src, int n) {
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  Game* arr = (league == 0) ? s_nfl : s_cfb;
  int*  cnt = (league == 0) ? &s_nflN : &s_cfbN;
  int   cap = (league == 0) ? MAX_NFL : MAX_CFB;
  for (int i = 0; i < n; i++) {
    int j = -1;
    for (int k = 0; k < *cnt; k++) if (!strcmp(arr[k].id, src[i].id)) { j = k; break; }
    if (j >= 0)            arr[j] = src[i];
    else if (*cnt < cap)   arr[(*cnt)++] = src[i];
  }
  bool live = false;
  for (int i = 0; i < s_nflN; i++) if (s_nfl[i].state == 1) { live = true; break; }
  if (!live) for (int i = 0; i < s_cfbN; i++) if (s_cfb[i].state == 1) { live = true; break; }
  g_anyLive = live;
  xSemaphoreGive(s_mtx);
}
