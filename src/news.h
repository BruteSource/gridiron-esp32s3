#pragma once
#include <Arduino.h>
#include <time.h>

struct NewsItem {
  char   headline[112];
  char   desc[300];
  char   byline[28];
  time_t published;
};
#define MAX_NEWS 16

enum { NEWS_IDLE, NEWS_LOADING, NEWS_READY, NEWS_ERROR };
extern volatile int g_newsState;

void news_init();
void news_request(int league);              // UI: fire a fetch (sets LOADING)
bool news_take_request(int* league);        // net task: claim a pending request
void news_publish(const NewsItem* src, int n, int league);
void news_fail();
int  news_snapshot(NewsItem* out, int cap, int* leagueOut);
