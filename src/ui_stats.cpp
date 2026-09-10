#include "screens.h"
#include "display.h"
#include "stats.h"

#define T_HDR   30
#define T_ROW   24
#define T_FOOT  296

static GameStats s_gs;
static int s_mode    = 0;      // 0 = team stats, 1 = player stats
static int s_scroll  = 0;      // team view: row offset
static int s_dragPix = 0;
static int s_pScroll = 0;      // player view: pixel offset
static int s_pDrag   = 0;

static void drawFooter() {
  canvas.fillRect(0, T_FOOT, 240, 24, C_PANEL2);
  canvas.drawFastVLine(80, T_FOOT + 4, 16, C_BG);
  canvas.drawFastVLine(166, T_FOOT + 4, 16, C_BG);
  canvas.setFont(&fonts::Font2);
  canvas.setTextDatum(textdatum_t::middle_center);
  canvas.setTextColor(C_ACCENT);
  canvas.drawString("< BACK", 40, T_FOOT + 12);
  canvas.drawString(s_mode ? "TEAM STATS" : "PLAYERS", 124, T_FOOT + 12);
  canvas.drawString("REFRESH", 204, T_FOOT + 12);
}

// ---- team stats: two-column table -----------------------------------------
static void drawTeamView() {
  int vis = (T_FOOT - T_HDR) / T_ROW;
  int maxScroll = max(0, s_gs.n - vis);
  if (s_scroll > maxScroll) s_scroll = maxScroll;

  canvas.fillRect(0, 0, 240, T_HDR, C_PANEL);
  canvas.setFont(&fonts::FreeSansBold9pt7b);
  canvas.setTextColor(C_TEXT);
  canvas.setTextDatum(textdatum_t::middle_left);
  canvas.drawString("TEAM STATS", 8, T_HDR / 2);
  canvas.setFont(&fonts::Font2);
  canvas.setTextDatum(textdatum_t::middle_center);
  canvas.setTextColor(C_ACCENT);
  canvas.drawString(s_gs.away[0] ? s_gs.away : "AWY", 158, T_HDR / 2);
  canvas.drawString(s_gs.home[0] ? s_gs.home : "HOM", 212, T_HDR / 2);

  for (int r = 0; r < vis && s_scroll + r < s_gs.n; r++) {
    const StatRow& sr = s_gs.rows[s_scroll + r];
    int y = T_HDR + r * T_ROW;
    canvas.fillRect(0, y, 240, T_ROW - 1, (r & 1) ? C_PANEL2 : C_BG);
    canvas.setFont(&fonts::Font2);
    canvas.setTextColor(C_DIM);
    canvas.setTextDatum(textdatum_t::middle_left);
    canvas.drawString(sr.label, 8, y + T_ROW / 2);
    canvas.setTextColor(C_TEXT);
    canvas.setTextDatum(textdatum_t::middle_center);
    canvas.drawString(sr.away, 158, y + T_ROW / 2);
    canvas.drawString(sr.home, 212, y + T_ROW / 2);
  }
  if (maxScroll > 0 && s_scroll < maxScroll)
    canvas.fillTriangle(230, T_FOOT - 12, 236, T_FOOT - 12, 233, T_FOOT - 6, C_DIM);
}

// ---- player stats: grouped by team, then passing / rushing / receiving ----
static const char* catName(char c) {
  return c == 'P' ? "PASSING" : c == 'R' ? "RUSHING" : "RECEIVING";
}

static int playerContentH() {
  int h = 0, prevSide = -1; char prevCat = 0;
  for (int i = 0; i < s_gs.pn; i++) {
    if (s_gs.players[i].side != prevSide) { h += 22; prevSide = s_gs.players[i].side; prevCat = 0; }
    if (s_gs.players[i].cat  != prevCat)  { h += 15; prevCat  = s_gs.players[i].cat; }
    h += 19;
  }
  return h;
}

static void drawPlayerView() {
  int viewH = T_FOOT - T_HDR;
  int maxS = max(0, playerContentH() - viewH);
  if (s_pScroll > maxS) s_pScroll = maxS;
  if (s_pScroll < 0) s_pScroll = 0;

  canvas.setClipRect(0, T_HDR, 240, viewH);
  int y = T_HDR - s_pScroll;
  int prevSide = -1; char prevCat = 0;

  for (int i = 0; i < s_gs.pn; i++) {
    const PlayerRow& p = s_gs.players[i];
    if (p.side != prevSide) {
      prevSide = p.side; prevCat = 0;
      if (y + 22 > T_HDR && y < T_FOOT) {
        canvas.fillRect(0, y, 240, 22, C_PANEL);
        canvas.setFont(&fonts::FreeSansBold9pt7b);
        canvas.setTextColor(C_TEXT);
        canvas.setTextDatum(textdatum_t::middle_left);
        canvas.drawString(p.side ? s_gs.home : s_gs.away, 8, y + 11);
      }
      y += 22;
    }
    if (p.cat != prevCat) {
      prevCat = p.cat;
      if (y + 15 > T_HDR && y < T_FOOT) {
        canvas.setFont(&fonts::Font2);
        canvas.setTextColor(C_ACCENT);
        canvas.setTextDatum(textdatum_t::middle_left);
        canvas.drawString(catName(p.cat), 10, y + 8);
      }
      y += 15;
    }
    if (y + 19 > T_HDR && y < T_FOOT) {
      canvas.setFont(&fonts::Font2);
      canvas.setTextColor(C_TEXT);
      canvas.setTextDatum(textdatum_t::middle_left);
      canvas.drawString(p.name, 16, y + 10);
      canvas.setTextColor(C_DIM);
      canvas.setTextDatum(textdatum_t::middle_right);
      canvas.drawString(p.line, 232, y + 10);
    }
    y += 19;
  }
  canvas.clearClipRect();

  canvas.fillRect(0, 0, 240, T_HDR, C_PANEL);
  canvas.setFont(&fonts::FreeSansBold9pt7b);
  canvas.setTextColor(C_TEXT);
  canvas.setTextDatum(textdatum_t::middle_left);
  canvas.drawString("PLAYER STATS", 8, T_HDR / 2);

  if (s_gs.pn == 0) {
    canvas.setFont(&fonts::Font2);
    canvas.setTextColor(C_DIM);
    canvas.setTextDatum(textdatum_t::middle_center);
    canvas.drawString("no player stats yet", 120, 150);
  } else if (maxS > 0 && s_pScroll < maxS) {
    canvas.fillTriangle(230, T_FOOT - 12, 236, T_FOOT - 12, 233, T_FOOT - 6, C_DIM);
  }
}

// -------------------------------------------------------------------------
Screen scr_stats(const TouchEv& e) {
  int st = g_statsState;
  if (st != STATS_READY) {
    if (e.tap && (st == STATS_ERROR || e.y >= T_FOOT)) return SCR_DETAIL;
    canvas.fillScreen(C_BG);
    canvas.setFont(&fonts::Font2);
    canvas.setTextDatum(textdatum_t::middle_center);
    if (st == STATS_ERROR) {
      canvas.setTextColor(C_LIVE);
      canvas.drawString("Stats unavailable - tap to go back", 120, 150);
    } else {
      canvas.setTextColor(C_DIM);
      canvas.drawString("Loading game stats...", 120, 150);
    }
    canvas.pushSprite(0, 0);
    return SCR_STATS;
  }

  if (!stats_snapshot(&s_gs) || (s_gs.n == 0 && s_gs.pn == 0)) return SCR_DETAIL;

  if (e.tap && e.y >= T_FOOT) {
    if (e.x < 80)  return SCR_DETAIL;
    if (e.x < 166) {                              // toggle view
      s_mode ^= 1;
      s_scroll = s_dragPix = s_pScroll = s_pDrag = 0;
      return SCR_STATS;
    }
    stats_reload();
    s_scroll = s_dragPix = s_pScroll = s_pDrag = 0;
    return SCR_STATS;
  }
  if (e.tap && e.y < T_HDR) return SCR_DETAIL;

  if (s_mode == 0) {
    if (e.dragging) {
      s_dragPix -= e.dragDy;
      int maxScroll = max(0, s_gs.n - (T_FOOT - T_HDR) / T_ROW);
      while (s_dragPix >= T_ROW)  { s_dragPix -= T_ROW; if (s_scroll < maxScroll) s_scroll++; }
      while (s_dragPix <= -T_ROW) { s_dragPix += T_ROW; if (s_scroll > 0) s_scroll--; }
    }
  } else if (e.dragging) {
    s_pDrag -= e.dragDy;
    s_pScroll += s_pDrag;
    s_pDrag = 0;
  }

  canvas.fillScreen(C_BG);
  if (s_mode == 0) drawTeamView();
  else             drawPlayerView();
  drawFooter();

  canvas.pushSprite(0, 0);
  return SCR_STATS;
}
