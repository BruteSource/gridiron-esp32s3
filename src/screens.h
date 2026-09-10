#pragma once
#include "ui.h"
#include "games.h"

// Current league shown in the UI: 0 = NFL, 1 = College Top 25.
extern int  g_league;

// Selection carried into the detail screen.
extern int  g_detailSel;
extern char g_detailId[12];

// set by the settings screen; main.cpp acts on these
extern bool g_wantRecal;   // run display_calibrate()
extern bool g_wantSleep;   // enter deep sleep now

extern int g_newsSel;

// Each returns the screen to show next.
Screen scr_list(const TouchEv& e);
Screen scr_detail(const TouchEv& e);
Screen scr_schedule(const TouchEv& e);
Screen scr_settings(const TouchEv& e);
Screen scr_news(const TouchEv& e);
Screen scr_newsitem(const TouchEv& e);
Screen scr_stats(const TouchEv& e);
Screen scr_wifi(const TouchEv& e);
Screen scr_wifikey(const TouchEv& e);

// word-wrap `s` into out[] lines that fit maxW in the *current* canvas font
// (defined in ui_news.cpp). Returns line count.
int wrapText(const char* s, int maxW, char out[][100], int maxLines);

// shared little formatters (defined in ui_list.cpp)
void periodLabel(char* b, size_t n, int period);
// between-periods label ("Halftime", "End of 1st"...). true if `b` was filled;
// false means the caller should show period + clock as usual.
bool breakLabel(char* b, size_t n, int period, const char* detail, const char* clock);
void kickShort(char* b, size_t n, time_t k);   // "Sun 7:30p"
void kickLong(char* b, size_t n, time_t k);    // "Sun 9/10 7:30 PM"
void dateShort(char* b, size_t n, time_t k);   // "Sun 10/5"
