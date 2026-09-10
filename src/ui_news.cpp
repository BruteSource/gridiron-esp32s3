#include "screens.h"
#include "display.h"
#include "news.h"
#include <time.h>

int g_newsSel = 0;

#define N_HDR   30
#define N_ROW   52
#define N_FOOT  296
#define N_VIS   5

static NewsItem s_items[MAX_NEWS];
static int  s_n = 0;
static int  s_league = 0;
static int  s_scroll = 0;      // list: row offset
static int  s_dragPix = 0;
static int  s_docScroll = 0;   // read view: pixel offset
static int  s_docDrag = 0;

// --- word wrap: fills out[] with lines that fit maxW in the current font ----
int wrapText(const char* s, int maxW, char out[][100], int maxLines) {
  int nl = 0;
  char line[100] = "";
  while (*s && nl < maxLines) {
    while (*s == ' ' || *s == '\n' || *s == '\r' || *s == '\t') s++;
    if (!*s) break;
    const char* ws = s;
    while (*s && *s != ' ' && *s != '\n') s++;
    int wl = s - ws; if (wl > 99) wl = 99;
    char word[100];
    memcpy(word, ws, wl); word[wl] = 0;

    char trial[200];
    if (line[0]) snprintf(trial, sizeof(trial), "%s %s", line, word);
    else         snprintf(trial, sizeof(trial), "%s", word);

    if (!line[0] || (int)canvas.textWidth(trial) <= maxW) {
      strncpy(line, trial, 99); line[99] = 0;
    } else {
      strncpy(out[nl++], line, 99); out[nl - 1][99] = 0;
      strncpy(line, word, 99); line[99] = 0;
    }
  }
  if (line[0] && nl < maxLines) { strncpy(out[nl++], line, 99); out[nl - 1][99] = 0; }
  return nl;
}

static void ageAgo(char* b, size_t n, time_t t) {
  if (!t) { b[0] = 0; return; }
  long d = (long)(time(nullptr) - t);
  if (d < 60)         snprintf(b, n, "now");
  else if (d < 3600)  snprintf(b, n, "%ldm", d / 60);
  else if (d < 86400) snprintf(b, n, "%ldh", d / 3600);
  else                snprintf(b, n, "%ldd", d / 86400);
}

static void header(const char* right) {
  canvas.fillRect(0, 0, 240, N_HDR, C_PANEL);
  canvas.setFont(&fonts::FreeSansBold9pt7b);
  canvas.setTextColor(C_TEXT);
  canvas.setTextDatum(textdatum_t::middle_left);
  canvas.drawString(s_league ? "NCAAF NEWS" : "NFL NEWS", 8, N_HDR / 2);
  if (right && *right) {
    canvas.setFont(&fonts::Font2);
    canvas.setTextColor(C_DIM);
    canvas.setTextDatum(textdatum_t::middle_right);
    canvas.drawString(right, 234, N_HDR / 2);
  }
}

static void footer(const char* label) {
  canvas.fillRect(0, N_FOOT, 240, 24, C_PANEL2);
  canvas.setFont(&fonts::FreeSansBold9pt7b);
  canvas.setTextColor(C_ACCENT);
  canvas.setTextDatum(textdatum_t::middle_center);
  canvas.drawString(label, 120, N_FOOT + 12);
}

// ---- headline list ----------------------------------------------------
Screen scr_news(const TouchEv& e) {
  int st = g_newsState;
  if (st != NEWS_READY) {
    if (e.tap && (st == NEWS_ERROR || e.y >= N_FOOT)) return SCR_LIST;
    canvas.fillScreen(C_BG);
    canvas.setFont(&fonts::Font2);
    canvas.setTextDatum(textdatum_t::middle_center);
    if (st == NEWS_ERROR) {
      canvas.setTextColor(C_LIVE);
      canvas.drawString("News unavailable - tap to go back", 120, 155);
    } else {
      canvas.setTextColor(C_DIM);
      canvas.drawString("Loading news...", 120, 155);
    }
    canvas.pushSprite(0, 0);
    return SCR_NEWS;
  }

  s_n = news_snapshot(s_items, MAX_NEWS, &s_league);
  int maxScroll = max(0, s_n - N_VIS);
  if (s_scroll > maxScroll) s_scroll = maxScroll;

  if (e.dragging) {
    s_dragPix -= e.dragDy;
    while (s_dragPix >= N_ROW)  { s_dragPix -= N_ROW; if (s_scroll < maxScroll) s_scroll++; }
    while (s_dragPix <= -N_ROW) { s_dragPix += N_ROW; if (s_scroll > 0) s_scroll--; }
  }
  if (e.tap) {
    if (e.y >= N_FOOT) return SCR_LIST;
    if (e.y >= N_HDR) {
      int idx = s_scroll + (e.y - N_HDR) / N_ROW;
      if (idx >= 0 && idx < s_n) {
        g_newsSel = idx;
        s_docScroll = 0; s_docDrag = 0;
        return SCR_NEWSITEM;
      }
    }
  }

  canvas.fillScreen(C_BG);
  char hcount[12]; snprintf(hcount, sizeof(hcount), "%d", s_n);
  header(hcount);

  static char lines[3][100];
  for (int r = 0; r < N_VIS && s_scroll + r < s_n; r++) {
    const NewsItem& it = s_items[s_scroll + r];
    int y = N_HDR + r * N_ROW;
    canvas.fillRect(0, y, 240, N_ROW - 1, (r & 1) ? C_PANEL2 : C_BG);

    char ag[8]; ageAgo(ag, sizeof(ag), it.published);
    canvas.setFont(&fonts::Font2);
    canvas.setTextColor(C_DIM);
    canvas.setTextDatum(textdatum_t::top_right);
    if (ag[0]) canvas.drawString(ag, 234, y + 3);

    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.setTextColor(C_TEXT);
    canvas.setTextDatum(textdatum_t::top_left);
    int nl = wrapText(it.headline, 204, lines, 3);
    for (int i = 0; i < nl; i++)
      canvas.drawString(lines[i], 8, y + 5 + i * 15);
  }

  footer("< BACK");
  canvas.pushSprite(0, 0);
  return SCR_NEWS;
}

// ---- read view ------------------------------------------------------
Screen scr_newsitem(const TouchEv& e) {
  if (g_newsState != NEWS_READY) return SCR_NEWS;
  s_n = news_snapshot(s_items, MAX_NEWS, &s_league);
  if (s_n == 0) return SCR_NEWS;
  int idx = constrain(g_newsSel, 0, s_n - 1);

  if (e.tap && e.y >= N_FOOT) {
    if (e.x < 80)       { if (idx > 0) idx--; }
    else if (e.x > 160) { if (idx < s_n - 1) idx++; }
    else                return SCR_NEWS;
    g_newsSel = idx;
    s_docScroll = 0; s_docDrag = 0;
  } else if (e.tap && e.y < N_HDR) {
    return SCR_NEWS;
  }

  const NewsItem& it = s_items[idx];

  // lay out the whole article into lines
  static char hl[5][100], bl[28][100];
  canvas.setFont(&fonts::FreeSansBold9pt7b);
  int nh = wrapText(it.headline, 224, hl, 5);
  canvas.setFont(&fonts::Font2);
  int nb = wrapText(it.desc[0] ? it.desc : "(no summary available)", 228, bl, 28);

  int headBlock = 8 + nh * 18 + 6;              // headline lines
  char meta[48];
  char ag[8]; ageAgo(ag, sizeof(ag), it.published);
  if (it.byline[0] && ag[0]) snprintf(meta, sizeof(meta), "%s  -  %s", it.byline, ag);
  else if (it.byline[0])     snprintf(meta, sizeof(meta), "%s", it.byline);
  else                       snprintf(meta, sizeof(meta), "%s", ag);
  int metaY = headBlock;
  int ruleY = metaY + 18;
  int bodyY = ruleY + 8;
  int contentH = bodyY + nb * 16 + 10;
  int viewH = N_FOOT;
  int maxScroll = max(0, contentH - viewH);

  if (e.dragging) {
    s_docDrag -= e.dragDy;
    s_docScroll = constrain(s_docScroll + (s_docDrag / 1), 0, maxScroll);
    s_docDrag = 0;
  }
  if (s_docScroll > maxScroll) s_docScroll = maxScroll;

  canvas.fillScreen(C_BG);
  int oy = -s_docScroll;

  canvas.setFont(&fonts::FreeSansBold9pt7b);
  canvas.setTextColor(C_TEXT);
  canvas.setTextDatum(textdatum_t::top_left);
  for (int i = 0; i < nh; i++) canvas.drawString(hl[i], 8, oy + 8 + i * 18);

  canvas.setFont(&fonts::Font2);
  canvas.setTextColor(C_DIM);
  canvas.drawString(meta, 8, oy + metaY);
  canvas.drawFastHLine(8, oy + ruleY, 224, C_PANEL);

  canvas.setTextColor(C_TEXT);
  for (int i = 0; i < nb; i++) {
    int ly = oy + bodyY + i * 16;
    if (ly > -16 && ly < N_FOOT) canvas.drawString(bl[i], 8, ly);
  }

  // scroll hint
  if (maxScroll > 0 && s_docScroll < maxScroll) {
    canvas.fillTriangle(230, N_FOOT - 12, 236, N_FOOT - 12, 233, N_FOOT - 6, C_DIM);
  }

  canvas.fillRect(0, N_FOOT, 240, 24, C_PANEL2);
  canvas.setFont(&fonts::FreeSansBold9pt7b);
  canvas.setTextDatum(textdatum_t::middle_center);
  canvas.setTextColor(idx > 0 ? C_TEXT : C_LOSE);
  canvas.drawString("<PREV", 40, N_FOOT + 12);
  canvas.setTextColor(C_ACCENT);
  canvas.drawString("LIST", 120, N_FOOT + 12);
  canvas.setTextColor(idx < s_n - 1 ? C_TEXT : C_LOSE);
  canvas.drawString("NEXT>", 200, N_FOOT + 12);

  canvas.pushSprite(0, 0);
  return SCR_NEWSITEM;
}
