#include "stats.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

volatile int g_statsState = STATS_IDLE;

static SemaphoreHandle_t s_mtx;
static GameStats s_data;
static int  s_reqLeague = -1;
static char s_reqId[12]  = "";
static bool s_reqPending  = false;
static int  s_lastLeague = 0;
static char s_lastId[12] = "";

void stats_init() { s_mtx = xSemaphoreCreateMutex(); }

void stats_request(int league, const char* gameId) {
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  s_reqLeague = league;
  strlcpy(s_reqId, gameId, sizeof(s_reqId));
  s_lastLeague = league;
  strlcpy(s_lastId, gameId, sizeof(s_lastId));
  s_reqPending = true;
  xSemaphoreGive(s_mtx);
  g_statsState = STATS_LOADING;
}

void stats_reload() {
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  if (s_lastId[0]) {
    s_reqLeague = s_lastLeague;
    strlcpy(s_reqId, s_lastId, sizeof(s_reqId));
    s_reqPending = true;
    g_statsState = STATS_LOADING;
  }
  xSemaphoreGive(s_mtx);
}

bool stats_take_request(int* league, char* gameId) {
  bool have = false;
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  if (s_reqPending) {
    *league = s_reqLeague;
    strcpy(gameId, s_reqId);
    s_reqPending = false;
    have = true;
  }
  xSemaphoreGive(s_mtx);
  return have;
}

void stats_publish(const GameStats* src) {
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  memcpy(&s_data, src, sizeof(GameStats));
  xSemaphoreGive(s_mtx);
  g_statsState = STATS_READY;
}

void stats_fail() { g_statsState = STATS_ERROR; }

bool stats_snapshot(GameStats* out) {
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  memcpy(out, &s_data, sizeof(GameStats));
  bool ok = g_statsState == STATS_READY;
  xSemaphoreGive(s_mtx);
  return ok;
}
