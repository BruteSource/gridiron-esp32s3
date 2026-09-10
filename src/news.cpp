#include "news.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

volatile int g_newsState = NEWS_IDLE;

static SemaphoreHandle_t s_mtx;
static NewsItem s_items[MAX_NEWS];
static int  s_n = 0;
static int  s_league = 0;
static int  s_reqLeague = -1;
static bool s_reqPending = false;

void news_init() { s_mtx = xSemaphoreCreateMutex(); }

void news_request(int league) {
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  s_reqLeague = league;
  s_reqPending = true;
  xSemaphoreGive(s_mtx);
  g_newsState = NEWS_LOADING;
}

bool news_take_request(int* league) {
  bool have = false;
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  if (s_reqPending) { *league = s_reqLeague; s_reqPending = false; have = true; }
  xSemaphoreGive(s_mtx);
  return have;
}

void news_publish(const NewsItem* src, int n, int league) {
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  if (n > MAX_NEWS) n = MAX_NEWS;
  memcpy(s_items, src, (size_t)n * sizeof(NewsItem));
  s_n = n;
  s_league = league;
  xSemaphoreGive(s_mtx);
  g_newsState = NEWS_READY;
}

void news_fail() { g_newsState = NEWS_ERROR; }

int news_snapshot(NewsItem* out, int cap, int* leagueOut) {
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  int n = s_n; if (n > cap) n = cap;
  memcpy(out, s_items, (size_t)n * sizeof(NewsItem));
  if (leagueOut) *leagueOut = s_league;
  xSemaphoreGive(s_mtx);
  return n;
}
