#include "screens.h"
#include "display.h"
#include "settings.h"
#include "anim.h"
#include "stats.h"
#include <time.h>

int  g_detailSel = 0;
char g_detailId[12] = "";

static Game s_dl[MAX_CFB];
static int  s_dn = 0;

// score-change tracking for the celebration animation
static char    s_prevId[12] = "";
static uint8_t s_prevA = 0, s_prevH = 0;
static int     s_style = 0;

static void teamBlock(int cy, const char* name, const char* abbr, int rank,
                      int score, uint16_t col, bool showScore, bool loser) {
  canvas.fillRect(0, cy - 26, 8, 52, col);

  canvas.setTextDatum(textdatum_t::middle_left);
  canvas.setFont(&fonts::FreeSansBold12pt7b);
  canvas.setTextColor(loser ? C_LOSE : C_TEXT);
  char nm[26];
  if (rank) snprintf(nm, sizeof(nm), "#%d %s", rank, name);
  else      strlcpy(nm, name, sizeof(nm));
  canvas.drawString(nm, 16, cy - 6);

  canvas.setFont(&fonts::Font2);
  canvas.setTextColor(C_DIM);
  canvas.drawString(abbr, 16, cy + 15);

  if (showScore) {
    canvas.setTextDatum(textdatum_t::middle_right);
    canvas.setFont(&fonts::FreeSansBold24pt7b);
    canvas.setTextColor(loser ? C_LOSE : C_TEXT);
    char sc[5]; snprintf(sc, sizeof(sc), "%d", score);
    canvas.drawString(sc, 232, cy);
  }
}

// ---- compact live view with play-by-play ------------------------------
static void liveRow(int cy, const char* name, const char* abbr, int rank,
                    int score, uint16_t col, bool poss) {
  canvas.fillRect(0, cy - 14, 6, 28, col);
  canvas.setTextDatum(textdatum_t::middle_left);
  canvas.setFont(&fonts::FreeSansBold12pt7b);
  canvas.setTextColor(C_TEXT);
  char nm[28];
  if (rank) snprintf(nm, sizeof(nm), "#%d %s", rank, name);
  else      strlcpy(nm, name, sizeof(nm));
  canvas.drawString(nm, poss ? 24 : 12, cy);
  if (poss) canvas.fillTriangle(12, cy - 5, 12, cy + 5, 20, cy, C_LIVE);
  canvas.setTextDatum(textdatum_t::middle_right);
  canvas.setFont(&fonts::FreeSansBold18pt7b);
  char sc[5]; snprintf(sc, sizeof(sc), "%d", score);
  canvas.drawString(sc, 232, cy);
}

static void fitLine(char* dst, size_t cap, const char* src, int maxW) {
  strlcpy(dst, src, cap);
  bool cut = false;
  while ((int)canvas.textWidth(dst) > maxW && strlen(dst) > 4) {
    dst[strlen(dst) - 1] = 0;
    cut = true;
  }
  if (cut) { size_t l = strlen(dst); if (l + 2 < cap) { dst[l] = dst[l + 1] = '.'; dst[l + 2] = 0; } }
}

static void drawLiveBody(const Game& g) {
  bool aPoss = g.possId[0] && !strcmp(g.possId, g.awayId);
  bool hPoss = g.possId[0] && !strcmp(g.possId, g.homeId);

  liveRow(22, g.awayName, g.away, g.awayRank, g.awayScore, g.awayColor, aPoss);
  liveRow(50, g.homeName, g.home, g.homeRank, g.homeScore, g.homeColor, hPoss);
  canvas.drawFastHLine(0, 68, 240, C_PANEL);

  char cl[24];
  if (!breakLabel(cl, sizeof(cl), g.period, g.detail, g.clock)) {
    char q[8]; periodLabel(q, sizeof(q), g.period);
    if (g.clock[0]) snprintf(cl, sizeof(cl), "%s  %s", q, g.clock);
    else            snprintf(cl, sizeof(cl), "%s  LIVE", q);   // CFBD: no clock
  }
  canvas.setFont(&fonts::FreeSansBold12pt7b);
  canvas.setTextColor(C_LIVE);
  canvas.setTextDatum(textdatum_t::middle_left);
  canvas.drawString(cl, 10, 83);
  if (g.redZone) {
    canvas.fillRect(120, 75, 62, 16, C_LIVE);
    canvas.setFont(&fonts::Font2);
    canvas.setTextColor(C_TEXT);
    canvas.setTextDatum(textdatum_t::middle_center);
    canvas.drawString("RED ZONE", 151, 83);
  }
  // STATS button (tap zone handled in scr_detail)
  canvas.fillRoundRect(186, 73, 48, 20, 4, C_PANEL2);
  canvas.setFont(&fonts::Font2);
  canvas.setTextColor(C_ACCENT);
  canvas.setTextDatum(textdatum_t::middle_center);
  canvas.drawString("STATS", 210, 83);

  int y = 102;
  canvas.setTextDatum(textdatum_t::middle_left);
  if (g.downDist[0]) {
    canvas.setFont(&fonts::FreeSansBold9pt7b);
    canvas.setTextColor(C_TEXT);
    canvas.drawString(g.downDist, 10, y);
    y += 17;
  }

  // team | timeout dots | ... | weather
  canvas.setFont(&fonts::Font2);
  canvas.setTextColor(C_DIM);
  canvas.setTextDatum(textdatum_t::middle_left);
  int lx = 10;
  for (int t = 0; t < 2; t++) {
    const char* ab = t == 0 ? g.away : g.home;
    uint8_t left  = t == 0 ? g.toAway : g.toHome;
    canvas.drawString(ab, lx, y);
    lx += canvas.textWidth(ab) + 6;
    for (int i = 0; i < 3; i++) {
      if (i < left) canvas.fillCircle(lx + i * 7, y, 2, C_TEXT);
      else          canvas.drawCircle(lx + i * 7, y, 2, C_DIM);
    }
    lx += 3 * 7 + 14;
  }
  if (g.weather[0]) {
    static char wxb[24];
    fitLine(wxb, sizeof(wxb), g.weather, 234 - lx);
    canvas.setTextDatum(textdatum_t::middle_right);
    canvas.drawString(wxb, 234, y);
  }
  y += 13;
  canvas.drawFastHLine(10, y, 220, C_PANEL);
  y += 8;

  static char plays[PBP_KEEP][160];
  static char wl[4][100];
  static char one[164];
  int np = pbp_get(plays, PBP_KEEP);

  canvas.setTextDatum(textdatum_t::top_left);
  canvas.setFont(&fonts::Font2);
  if (np == 0) {
    canvas.setTextColor(C_DIM);
    canvas.drawString("waiting for the next play...", 10, y + 6);
    return;
  }

  canvas.setTextColor(C_DIM);
  canvas.drawString("LATEST PLAY", 10, y);
  y += 15;
  canvas.setTextColor(C_TEXT);
  int nl = wrapText(plays[0], 228, wl, 4);
  for (int k = 0; k < nl; k++) { canvas.drawString(wl[k], 10, y); y += 15; }
  y += 6;

  if (np > 1 && y < 264) {
    canvas.drawFastHLine(10, y, 220, C_PANEL);
    y += 6;
    canvas.setTextColor(C_DIM);
    for (int i = 1; i < np && y < 290; i++) {
      fitLine(one, sizeof(one), plays[i], 224);
      canvas.drawString(one, 10, y);
      y += 14;
    }
  }
}

// ---------------------------------------------------------------------
Screen scr_detail(const TouchEv& e) {
  s_dn = games_snapshot(g_league, s_dl, MAX_CFB);
  if (s_dn == 0) return SCR_LIST;

  int idx = -1;
  for (int i = 0; i < s_dn; i++)
    if (!strcmp(s_dl[i].id, g_detailId)) { idx = i; break; }
  if (idx < 0) idx = constrain(g_detailSel, 0, s_dn - 1);

  bool demo = false;
  if (e.tap) {
    const Game& gt = s_dl[idx];
    bool live = gt.state == 1;
    int aY0 = live ? 4  : 34,  aY1 = live ? 38 : 116;
    int hY0 = live ? 38 : 120, hY1 = live ? 68 : 198;
    if (e.y >= 296) {
      if (e.x < 80)       { if (idx > 0) idx--; }
      else if (e.x > 160) { if (idx < s_dn - 1) idx++; }
      else                { return SCR_LIST; }
      g_detailSel = idx;
      strlcpy(g_detailId, s_dl[idx].id, sizeof(g_detailId));
    } else if (live && e.y >= 70 && e.y < 98 && e.x >= 176) {
      stats_request(gt.league, gt.id);
      return SCR_STATS;
    } else if (e.y >= aY0 && e.y < aY1 && gt.awayId[0]) {
      schedule_request(gt.league, gt.awayId, gt.awayName);
      return SCR_SCHEDULE;
    } else if (e.y >= hY0 && e.y < hY1 && gt.homeId[0]) {
      schedule_request(gt.league, gt.homeId, gt.homeName);
      return SCR_SCHEDULE;
    } else if (!live && gt.state == 2 && e.y >= 200 && e.y < 256) {
      stats_request(gt.league, gt.id);    // final: tap the score band for the box score
      return SCR_STATS;
    } else if (!live && e.y >= 200 && e.y < 256) {
      demo = true;                        // pre-game: tap score band to preview an animation
    } else if (!live) {
      return SCR_LIST;
    }
  }

  const Game& g = s_dl[idx];
  detail_focus(g.league, g.id, g.state == 1);

  int animSide = -1, animPts = 0;
  bool sameGame = !strcmp(g.id, s_prevId);
  if (demo) {
    animSide = s_style & 1;
    animPts  = (s_style % 2) ? 3 : 7;
  } else if (g_set.scoreAlerts && sameGame && g.state == 1) {
    if (g.awayScore > s_prevA)      { animSide = 0; animPts = g.awayScore - s_prevA; }
    else if (g.homeScore > s_prevH) { animSide = 1; animPts = g.homeScore - s_prevH; }
  }
  strlcpy(s_prevId, g.id, sizeof(s_prevId));
  s_prevA = g.awayScore;
  s_prevH = g.homeScore;

  bool aLose = g.state == 2 && g.awayScore < g.homeScore;
  bool hLose = g.state == 2 && g.homeScore < g.awayScore;

  canvas.fillScreen(C_BG);

  if (g.state == 1) {
    drawLiveBody(g);
  } else {
    bool showScore = g.state != 0;
    teamBlock(62,  g.awayName, g.away, g.awayRank, g.awayScore, g.awayColor, showScore, aLose);
    canvas.setTextDatum(textdatum_t::middle_center);
    canvas.setFont(&fonts::Font2);
    canvas.setTextColor(C_DIM);
    canvas.drawString("at", 120, 118);
    teamBlock(150, g.homeName, g.home, g.homeRank, g.homeScore, g.homeColor, showScore, hLose);

    canvas.setFont(&fonts::Font2);
    canvas.setTextColor(C_DIM);
    canvas.setTextDatum(textdatum_t::middle_center);
    canvas.drawString("tap a team for schedule", 120, 189);

    canvas.fillRect(0, 200, 240, 52, C_PANEL);
    canvas.setTextDatum(textdatum_t::middle_center);
    canvas.setFont(&fonts::FreeSansBold12pt7b);
    if (g.state == 2) {
      canvas.setTextColor(C_TEXT);
      canvas.drawString(g.period > 4 ? "FINAL / OT" : "FINAL", 120, 220);
    } else {
      char kb[26]; kickLong(kb, sizeof(kb), g.kickoff);
      canvas.setTextColor(C_TEXT);
      canvas.drawString(kb, 120, 220);
    }
    canvas.setFont(&fonts::Font2);
    canvas.setTextColor(g.state == 2 ? C_ACCENT : C_DIM);
    canvas.drawString(g.state == 2 ? "tap for box score & player stats"
                                   : "tap to demo score alert", 120, 242);

    canvas.setTextDatum(textdatum_t::top_left);
    int iy = 258;
    if (g.tv[0]) {
      char b[32]; snprintf(b, sizeof(b), "TV: %s", g.tv);
      canvas.drawString(b, 10, iy); iy += 16;
    }
    if (g.weather[0]) { canvas.drawString(g.weather, 10, iy); iy += 16; }
    if (g.venue[0])   canvas.drawString(g.venue, 10, iy);
  }

  // nav footer
  canvas.fillRect(0, 296, 240, 24, C_PANEL2);
  canvas.setFont(&fonts::FreeSansBold9pt7b);
  canvas.setTextDatum(textdatum_t::middle_center);
  canvas.setTextColor(idx > 0 ? C_TEXT : C_LOSE);
  canvas.drawString("<PREV", 40, 308);
  canvas.setTextColor(C_ACCENT);
  canvas.drawString("LIST", 120, 308);
  canvas.setTextColor(idx < s_dn - 1 ? C_TEXT : C_LOSE);
  canvas.drawString("NEXT>", 200, 308);

  if (animSide >= 0) {
    canvas.pushSprite(0, 0);
    anim_score(animSide, animSide ? g.homeColor : g.awayColor, animPts, s_style++);
  }

  canvas.pushSprite(0, 0);
  return SCR_DETAIL;
}
