#include "screens.h"
#include "display.h"
#include "battery.h"
#include "news.h"
#include <time.h>
#include <math.h>

int g_league = 0;

// ---- layout -------------------------------------------------------------
#define HDR_H   30
#define ROW_H   44
#define FOOT_Y  296
#define FOOT_H  24
#define VIS     6            // (FOOT_Y - HDR_H) / ROW_H

static Game s_list[MAX_CFB];
static int  s_n = 0;
static int  s_scroll = 0;
static int  s_dragPix = 0;

// ---- shared formatters -----------------------------------------------------
void periodLabel(char* b, size_t n, int p) {
  if (p <= 1) strlcpy(b, "1st", n);
  else if (p == 2) strlcpy(b, "2nd", n);
  else if (p == 3) strlcpy(b, "3rd", n);
  else if (p == 4) strlcpy(b, "4th", n);
  else if (p == 5) strlcpy(b, "OT", n);
  else snprintf(b, n, "%dOT", p - 4);
}

bool breakLabel(char* b, size_t n, int period, const char* detail, const char* clock) {
  if (detail && *detail) {
    if (strstr(detail, "Halftime")) { strlcpy(b, "Halftime", n); return true; }
    if (!strncmp(detail, "End of", 6)) {
      if (strstr(detail, "Regulation")) { strlcpy(b, "End of Reg.", n); return true; }
      strlcpy(b, detail, n);                       // "End of 1st Quarter" -> "End of 1st"
      char* q = strstr(b, " Quarter"); if (q) *q = 0;
      return true;
    }
  }
  if (clock && !strcmp(clock, "0:00")) {           // clock ran out, ESPN detail lagging
    if (period == 2) { strlcpy(b, "Halftime",   n); return true; }
    if (period == 1) { strlcpy(b, "End of 1st", n); return true; }
    if (period == 3) { strlcpy(b, "End of 3rd", n); return true; }
  }
  return false;
}

void kickShort(char* b, size_t n, time_t k) {
  if (!k) { strlcpy(b, "--", n); return; }
  struct tm lt; localtime_r(&k, &lt);
  static const char* dow[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  int h = lt.tm_hour % 12; if (h == 0) h = 12;
  snprintf(b, n, "%s %d:%02d%c", dow[lt.tm_wday], h, lt.tm_min,
           lt.tm_hour < 12 ? 'a' : 'p');
}

void kickLong(char* b, size_t n, time_t k) {
  if (!k) { strlcpy(b, "Time TBD", n); return; }
  struct tm lt; localtime_r(&k, &lt);
  static const char* dow[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  int h = lt.tm_hour % 12; if (h == 0) h = 12;
  snprintf(b, n, "%s %d/%d  %d:%02d %s", dow[lt.tm_wday], lt.tm_mon + 1,
           lt.tm_mday, h, lt.tm_min, lt.tm_hour < 12 ? "AM" : "PM");
}

void dateShort(char* b, size_t n, time_t k) {
  if (!k) { strlcpy(b, "TBD", n); return; }
  struct tm lt; localtime_r(&k, &lt);
  static const char* dow[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  snprintf(b, n, "%s %d/%d", dow[lt.tm_wday], lt.tm_mon + 1, lt.tm_mday);
}

// ---- drawing -------------------------------------------------------------
static void drawWifi(int x, int y, bool ok) {
  uint16_t c = ok ? C_ACCENT : C_LOSE;
  for (int i = 0; i < 3; i++)
    canvas.fillRect(x + i * 4, y + 6 - i * 3, 3, 3 + i * 3, c);
}

static void drawGear(int cx, int cy, uint16_t col) {
  for (int a = 0; a < 360; a += 60) {
    float r = a * 3.14159265f / 180.0f;
    canvas.fillRect(cx + (int)(7 * cosf(r)) - 1, cy + (int)(7 * sinf(r)) - 1, 3, 3, col);
  }
  canvas.fillCircle(cx, cy, 5, col);
  canvas.fillCircle(cx, cy, 2, C_PANEL);
}

// small battery glyph, top-left corner at (x, y); 18x9 incl. terminal nub
static void drawBattery(int x, int y, int pct, int chg) {
  if (pct < 0) pct = 0; if (pct > 100) pct = 100;
  uint16_t c = pct <= 12 ? C_LIVE : (pct <= 30 ? C_WARN : C_GOOD);
  if (chg == 1) c = C_ACCENT;
  canvas.drawRect(x, y, 16, 9, C_DIM);
  canvas.fillRect(x + 16, y + 3, 2, 3, C_DIM);
  int w = (pct * 14 + 50) / 100;
  if (w > 0) canvas.fillRect(x + 1, y + 1, w, 7, c);
  if (chg == 1) {                                   // lightning bolt
    canvas.drawLine(x + 9, y + 1, x + 6, y + 5, C_BG);
    canvas.drawLine(x + 6, y + 5, x + 9, y + 4, C_BG);
    canvas.drawLine(x + 9, y + 4, x + 6, y + 8, C_BG);
  }
}

static void drawNews(int x, int y, uint16_t col) {   // tiny newspaper
  canvas.drawRect(x, y, 16, 13, col);
  canvas.drawFastHLine(x + 2, y + 3, 11, col);
  canvas.drawFastHLine(x + 2, y + 6, 11, col);
  canvas.drawFastHLine(x + 2, y + 9, 7, col);
}

static void drawHeader() {
  canvas.fillRect(0, 0, 240, HDR_H, C_PANEL);
  canvas.setFont(&fonts::FreeSansBold9pt7b);
  canvas.setTextDatum(textdatum_t::middle_left);

  canvas.setTextColor(g_league == 0 ? C_TEXT : C_DIM);
  canvas.drawString("NFL", 10, HDR_H / 2);
  canvas.setTextColor(g_league == 1 ? C_TEXT : C_DIM);
  canvas.drawString("NCAAF", 52, HDR_H / 2);
  if (g_league == 0) canvas.fillRect(10, HDR_H - 3, 30, 3, C_ACCENT);
  else               canvas.fillRect(52, HDR_H - 3, 50, 3, C_ACCENT);

  drawWifi(146, HDR_H / 2 - 5, g_wifiConnected);
  drawBattery(162, HDR_H / 2 - 4, battery_percent(), battery_charging());
  drawNews(190, HDR_H / 2 - 6, C_DIM);
  drawGear(222, HDR_H / 2, C_DIM);
}

static void drawRow(int i, int y) {
  const Game& g = s_list[i];
  canvas.fillRect(0, y, 240, ROW_H - 1, (i & 1) ? C_PANEL2 : C_BG);
  if (g.state == 1) canvas.fillRect(0, y, 4, ROW_H - 1, C_LIVE);

  bool finalA = g.state == 2 && g.awayScore >= g.homeScore;
  bool finalH = g.state == 2 && g.homeScore >= g.awayScore;

  // team names + ranks
  canvas.setFont(&fonts::FreeSansBold9pt7b);
  canvas.setTextDatum(textdatum_t::middle_left);
  char aw[16], hm[16];
  if (g.awayRank) snprintf(aw, sizeof(aw), "%d %s", g.awayRank, g.away);
  else            strlcpy(aw, g.away, sizeof(aw));
  if (g.homeRank) snprintf(hm, sizeof(hm), "%d %s", g.homeRank, g.home);
  else            strlcpy(hm, g.home, sizeof(hm));

  canvas.setTextColor(g.state == 2 && !finalA ? C_LOSE : C_TEXT);
  canvas.drawString(aw, 12, y + ROW_H / 2 - 10);
  canvas.setTextColor(g.state == 2 && !finalH ? C_LOSE : C_TEXT);
  canvas.drawString(hm, 12, y + ROW_H / 2 + 10);

  // scores
  if (g.state != 0) {
    char sc[5];
    canvas.setTextDatum(textdatum_t::middle_right);
    canvas.setTextColor(g.state == 2 && !finalA ? C_LOSE : C_TEXT);
    snprintf(sc, sizeof(sc), "%u", g.awayScore);
    canvas.drawString(sc, 150, y + ROW_H / 2 - 10);
    canvas.setTextColor(g.state == 2 && !finalH ? C_LOSE : C_TEXT);
    snprintf(sc, sizeof(sc), "%u", g.homeScore);
    canvas.drawString(sc, 150, y + ROW_H / 2 + 10);
  }

  // status block (x 156..236)
  canvas.setTextDatum(textdatum_t::middle_center);
  canvas.setFont(&fonts::Font2);
  if (g.state == 1) {
    char brk[24];
    canvas.setTextColor(C_LIVE);
    if (breakLabel(brk, sizeof(brk), g.period, g.detail, g.clock)) {
      canvas.drawString(brk, 196, y + ROW_H / 2);
    } else {
      char q[8]; periodLabel(q, sizeof(q), g.period);
      canvas.drawString(q, 196, y + ROW_H / 2 - 9);
      canvas.drawString(g.clock[0] ? g.clock : "LIVE", 196, y + ROW_H / 2 + 9);
    }
  } else if (g.state == 2) {
    canvas.setTextColor(C_DIM);
    canvas.drawString(g.period > 4 ? "Final/OT" : "Final", 196, y + ROW_H / 2);
  } else {
    char t[14]; kickShort(t, sizeof(t), g.kickoff);
    canvas.setTextColor(C_DIM);
    canvas.drawString(t, 196, y + ROW_H / 2 - 9);
    if (g.tv[0]) canvas.drawString(g.tv, 196, y + ROW_H / 2 + 9);
  }
}

static void drawFooter(int maxScroll) {
  canvas.fillRect(0, FOOT_Y, 240, FOOT_H, C_PANEL);
  uint16_t up = s_scroll > 0 ? C_TEXT : C_LOSE;
  uint16_t dn = s_scroll < maxScroll ? C_TEXT : C_LOSE;
  canvas.fillTriangle(16, FOOT_Y + 16, 32, FOOT_Y + 16, 24, FOOT_Y + 7, up);
  canvas.fillTriangle(208, FOOT_Y + 7, 224, FOOT_Y + 7, 216, FOOT_Y + 16, dn);

  canvas.setFont(&fonts::Font2);
  canvas.setTextDatum(textdatum_t::middle_center);
  // no fresh fetch this session but we have cached games -> flag it, tap to retry
  if (s_n > 0 && g_lastUpdateMs[g_league] == 0) {
    canvas.setTextColor(C_WARN);
    canvas.drawString("last saved - tap to retry", 120, FOOT_Y + FOOT_H / 2);
  } else {
    char b[20];
    int shown = s_n ? min(VIS, s_n - s_scroll) : 0;
    snprintf(b, sizeof(b), "%d-%d / %d", s_n ? s_scroll + 1 : 0, s_scroll + shown, s_n);
    canvas.setTextColor(C_DIM);
    canvas.drawString(b, 120, FOOT_Y + FOOT_H / 2);
  }
}

// ---- entry -------------------------------------------------------------
Screen scr_list(const TouchEv& e) {
  s_n = games_snapshot(g_league, s_list, MAX_CFB);
  int maxScroll = max(0, s_n - VIS);
  if (s_scroll > maxScroll) s_scroll = maxScroll;

  if (e.dragging) {
    s_dragPix -= e.dragDy;
    while (s_dragPix >= ROW_H)  { s_dragPix -= ROW_H; if (s_scroll < maxScroll) s_scroll++; }
    while (s_dragPix <= -ROW_H) { s_dragPix += ROW_H; if (s_scroll > 0) s_scroll--; }
  }

  if (e.tap) {
    if (e.y < HDR_H + 8) {                       // generous — edge taps read low
      if (e.x >= 206) return SCR_SETTINGS;
      if (e.x >= 172) { news_request(g_league); return SCR_NEWS; }
      int want = (e.x < 48) ? 0 : (e.x < 120 ? 1 : g_league);
      if (want != g_league) {
        g_league = want; g_uiLeague = want; g_forceRefresh = true;
        s_scroll = 0; s_dragPix = 0;
      }
    } else if (e.y >= FOOT_Y) {
      if (e.x < 80)       { if (s_scroll > 0) s_scroll--; }
      else if (e.x > 160) { if (s_scroll < maxScroll) s_scroll++; }
      else               g_forceRefresh = true;        // tap centre = refresh now
    } else {
      int idx = s_scroll + (e.y - HDR_H) / ROW_H;
      if (idx >= 0 && idx < s_n) {
        g_detailSel = idx;
        strlcpy(g_detailId, s_list[idx].id, sizeof(g_detailId));
        return SCR_DETAIL;
      }
    }
    s_n = games_snapshot(g_league, s_list, MAX_CFB);
    maxScroll = max(0, s_n - VIS);
    if (s_scroll > maxScroll) s_scroll = maxScroll;
  }

  canvas.fillScreen(C_BG);
  drawHeader();
  if (s_n == 0) {
    canvas.setFont(&fonts::Font2);
    canvas.setTextColor(C_DIM);
    canvas.setTextDatum(textdatum_t::middle_center);
    int st = g_httpStatus[g_league];
    bool triedAndFailed = g_lastUpdateMs[g_league] == 0 && st != 0;
    const char* msg = !g_wifiConnected      ? g_bootMsg
                    : (st == 403 || st == 429) ? "Data source rate-limited this IP"
                    : triedAndFailed        ? "Can't reach data source - retrying"
                    : !g_firstCycleDone     ? "Loading scores..."
                    : (g_league ? "No ranked NCAAF games" : "No NFL games today");
    canvas.drawString(msg, 120, 160);
    if (st == 403 || st == 429) {
      canvas.setTextColor(C_DIM);
      canvas.drawString("usually clears within a day", 120, 180);
    }
  } else {
    for (int r = 0; r < VIS && s_scroll + r < s_n; r++)
      drawRow(s_scroll + r, HDR_H + r * ROW_H);
  }
  drawFooter(maxScroll);

  // critical battery banner (blinks), only when actually running on battery
  if (battery_charging() != 1 && battery_volts() < 3.40f && (millis() / 600) % 2) {
    canvas.fillRect(0, HDR_H, 240, 20, C_LIVE);
    canvas.setFont(&fonts::FreeSansBold9pt7b);
    canvas.setTextColor(C_TEXT);
    canvas.setTextDatum(textdatum_t::middle_center);
    canvas.drawString("LOW BATTERY - CHARGE NOW", 120, HDR_H + 10);
  }

  canvas.pushSprite(0, 0);
  return SCR_LIST;
}
