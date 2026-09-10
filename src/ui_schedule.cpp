#include "screens.h"
#include "display.h"
#include <time.h>

#define S_HDR   30
#define S_ROW   38          // two text lines per row
#define S_FOOT  296
#define S_VIS   7           // (S_FOOT - S_HDR) / S_ROW

static SchedGame s_g[MAX_SCHED];
static int  s_n = 0;
static int  s_first = 0;
static char s_name[26] = "";
static char s_record[16] = "";
static char s_lastName[26] = "\x01";   // force first-load scroll
static int  s_scroll = 0;
static int  s_dragPix = 0;

static void timeStr(char* b, size_t n, time_t k) {
  if (!k) { b[0] = 0; return; }
  struct tm lt; localtime_r(&k, &lt);
  int h = lt.tm_hour % 12; if (h == 0) h = 12;
  snprintf(b, n, "%d:%02d %s", h, lt.tm_min, lt.tm_hour < 12 ? "AM" : "PM");
}

static void dateStr2(char* b, size_t n, time_t k) {
  if (!k) { strlcpy(b, "--", n); return; }
  struct tm lt; localtime_r(&k, &lt);
  static const char* dow[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
  snprintf(b, n, "%s %d/%d", dow[lt.tm_wday], lt.tm_mon + 1, lt.tm_mday);
}

static void drawRow(int i, int y) {
  const SchedGame& g = s_g[i];
  bool next = (i == s_first);
  canvas.fillRect(0, y, 240, S_ROW - 1, (i & 1) ? C_PANEL2 : C_BG);
  if (g.state == 1)   canvas.fillRect(0, y, 4, S_ROW - 1, C_LIVE);
  else if (next)      canvas.fillRect(0, y, 4, S_ROW - 1, C_ACCENT);

  int y1 = y + 12;
  int y2 = y + 27;

  // top line: week + date (left), kickoff time (right)
  canvas.setFont(&fonts::Font2);
  canvas.setTextColor(C_DIM);
  canvas.setTextDatum(textdatum_t::middle_left);
  if (g.weekLabel[0]) canvas.drawString(g.weekLabel, 8, y1);
  char d[16]; dateStr2(d, sizeof(d), g.date);
  canvas.drawString(d, 58, y1);
  if (g.date && !g.bye) {
    char t[12]; timeStr(t, sizeof(t), g.date);
    canvas.setTextDatum(textdatum_t::middle_right);
    canvas.drawString(t, 232, y1);
  }

  // bottom line: matchup (left), result / network (right)
  canvas.setFont(&fonts::FreeSansBold9pt7b);
  canvas.setTextDatum(textdatum_t::middle_left);
  if (g.bye) {
    canvas.setTextColor(C_DIM);
    canvas.drawString("BYE WEEK", 12, y2);
    return;
  }
  canvas.setTextColor(C_TEXT);
  char opp[16];
  snprintf(opp, sizeof(opp), "%s %s", g.home ? "vs" : "@", g.opp);
  canvas.drawString(opp, 12, y2);

  canvas.setTextDatum(textdatum_t::middle_right);
  canvas.setFont(&fonts::Font2);
  if (g.state == 2) {
    char r[16];
    snprintf(r, sizeof(r), "%c %u-%u", g.win ? 'W' : 'L', g.teamScore, g.oppScore);
    canvas.setTextColor(g.win ? C_ACCENT : C_DIM);
    canvas.drawString(r, 232, y2);
  } else if (g.state == 1) {
    canvas.setTextColor(C_LIVE);
    canvas.drawString("LIVE", 232, y2);
  } else if (g.tv[0]) {
    canvas.setTextColor(C_DIM);
    canvas.drawString(g.tv, 232, y2);
  }
}

Screen scr_schedule(const TouchEv& e) {
  int st = g_schedState;

  if (st == SCH_LOADING || st == SCH_IDLE) {
    if (e.tap && e.y >= S_FOOT) return SCR_DETAIL;
    canvas.fillScreen(C_BG);
    canvas.setFont(&fonts::Font2);
    canvas.setTextColor(C_DIM);
    canvas.setTextDatum(textdatum_t::middle_center);
    canvas.drawString("Loading schedule...", 120, 150);
    canvas.pushSprite(0, 0);
    return SCR_SCHEDULE;
  }
  if (st == SCH_ERROR) {
    if (e.tap) return SCR_DETAIL;
    canvas.fillScreen(C_BG);
    canvas.setFont(&fonts::Font2);
    canvas.setTextColor(C_LIVE);
    canvas.setTextDatum(textdatum_t::middle_center);
    canvas.drawString("Schedule unavailable", 120, 150);
    canvas.setTextColor(C_DIM);
    canvas.drawString("tap to go back", 120, 172);
    canvas.pushSprite(0, 0);
    return SCR_SCHEDULE;
  }

  // READY
  s_n = schedule_snapshot(s_g, MAX_SCHED, s_name, s_record, &s_first);
  if (strcmp(s_name, s_lastName) != 0) {
    strlcpy(s_lastName, s_name, sizeof(s_lastName));
    int mx = max(0, s_n - S_VIS);
    s_scroll = constrain(s_first, 0, mx);
    s_dragPix = 0;
  }
  int maxScroll = max(0, s_n - S_VIS);
  if (s_scroll > maxScroll) s_scroll = maxScroll;

  if (e.dragging) {
    s_dragPix -= e.dragDy;
    while (s_dragPix >= S_ROW)  { s_dragPix -= S_ROW; if (s_scroll < maxScroll) s_scroll++; }
    while (s_dragPix <= -S_ROW) { s_dragPix += S_ROW; if (s_scroll > 0) s_scroll--; }
  }
  if (e.tap && e.y >= S_FOOT) return SCR_DETAIL;

  canvas.fillScreen(C_BG);

  // header
  canvas.fillRect(0, 0, 240, S_HDR, C_PANEL);
  canvas.setFont(&fonts::FreeSansBold9pt7b);
  canvas.setTextDatum(textdatum_t::middle_left);
  canvas.setTextColor(C_TEXT);
  canvas.drawString(s_name, 8, S_HDR / 2);
  if (s_record[0]) {
    canvas.setFont(&fonts::Font2);
    canvas.setTextColor(C_DIM);
    canvas.setTextDatum(textdatum_t::middle_right);
    canvas.drawString(s_record, 234, S_HDR / 2);
  }

  if (s_n == 0) {
    canvas.setFont(&fonts::Font2);
    canvas.setTextColor(C_DIM);
    canvas.setTextDatum(textdatum_t::middle_center);
    canvas.drawString("No schedule data", 120, 160);
  } else {
    for (int r = 0; r < S_VIS && s_scroll + r < s_n; r++)
      drawRow(s_scroll + r, S_HDR + r * S_ROW);
  }

  // footer
  canvas.fillRect(0, S_FOOT, 240, 24, C_PANEL2);
  canvas.setFont(&fonts::FreeSansBold9pt7b);
  canvas.setTextColor(C_ACCENT);
  canvas.setTextDatum(textdatum_t::middle_center);
  canvas.drawString("< BACK", 120, S_FOOT + 12);

  canvas.pushSprite(0, 0);
  return SCR_SCHEDULE;
}
