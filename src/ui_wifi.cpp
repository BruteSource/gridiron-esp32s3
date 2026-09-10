#include "screens.h"
#include "display.h"
#include "wifi_cfg.h"
#include <WiFi.h>

// ===========================================================================
// On-screen Wi-Fi setup: pick a network, type the password on a touch keyboard.
// ===========================================================================
static char s_selSsid[33] = "";
static char s_selLock     = 1;
static char s_pw[65]      = "";
static int  s_kbMode      = 0;     // 0 lower, 1 UPPER, 2 symbols
static int  s_scroll      = 0;
static int  s_dragPix     = 0;
static bool s_requested   = false; // asked for a scan since entering the screen

#define W_HDR  28
#define W_ROW  30
#define W_FOOT 296
#define W_VIS  8

static int rssiBars(int r) { return r >= -55 ? 4 : r >= -65 ? 3 : r >= -75 ? 2 : 1; }

// ---- network list --------------------------------------------------------
Screen scr_wifi(const TouchEv& e) {
  int js = g_wifiJoinState;

  if (js == WJOIN_OK) {                       // connected — off to the scores
    g_wifiJoinState = WJOIN_IDLE;
    s_requested = false;
    return SCR_LIST;
  }

  // ---- connecting / failed banners ----
  if (js == WJOIN_TRYING || js == WJOIN_FAIL) {
    canvas.fillScreen(C_BG);
    canvas.setTextDatum(textdatum_t::middle_center);
    canvas.setFont(&fonts::FreeSansBold12pt7b);
    if (js == WJOIN_TRYING) {
      canvas.setTextColor(C_ACCENT);
      canvas.drawString("Connecting", 120, 130);
      canvas.setFont(&fonts::Font2);
      canvas.setTextColor(C_DIM);
      canvas.drawString(s_selSsid, 120, 160);
    } else {
      canvas.setTextColor(C_LIVE);
      canvas.drawString("Couldn't connect", 120, 120);
      canvas.setFont(&fonts::Font2);
      canvas.setTextColor(C_DIM);
      canvas.drawString(s_selSsid, 120, 148);
      canvas.drawString("check the password", 120, 166);
      canvas.fillRect(0, W_FOOT, 240, 24, C_PANEL2);
      canvas.setFont(&fonts::FreeSansBold9pt7b);
      canvas.setTextColor(C_ACCENT);
      canvas.drawString("RETRY", 60, W_FOOT + 12);
      canvas.drawString("PICK OTHER", 168, W_FOOT + 12);
      if (e.tap && e.y >= W_FOOT) {
        g_wifiJoinState = WJOIN_IDLE;
        if (e.x < 120) return SCR_WIFIKEY;      // retry same network
        s_requested = false;                    // rescan
      }
    }
    canvas.pushSprite(0, 0);
    return SCR_WIFI;
  }

  // ---- kick off a scan on first entry ----
  if (!s_requested) { wifi_scan_request(); s_requested = true; s_scroll = 0; }

  WifiNet nets[WIFI_SCAN_MAX];
  int n = (g_wifiScanState == WSCAN_READY) ? wifi_scan_get(nets, WIFI_SCAN_MAX) : 0;
  int maxScroll = max(0, n - W_VIS);
  if (s_scroll > maxScroll) s_scroll = maxScroll;

  if (e.dragging && n) {
    s_dragPix -= e.dragDy;
    while (s_dragPix >= W_ROW)  { s_dragPix -= W_ROW; if (s_scroll < maxScroll) s_scroll++; }
    while (s_dragPix <= -W_ROW) { s_dragPix += W_ROW; if (s_scroll > 0) s_scroll--; }
  }

  if (e.tap) {
    if (e.y >= W_FOOT) {
      if (e.x < 120) { s_requested = false; }               // RESCAN
      else if (wifi_cfg_have()) return SCR_SETTINGS;         // DONE (only if online-capable)
    } else if (e.y >= W_HDR && g_wifiScanState == WSCAN_READY) {
      int idx = s_scroll + (e.y - W_HDR) / W_ROW;
      if (idx >= 0 && idx < n) {
        strlcpy(s_selSsid, nets[idx].ssid, sizeof(s_selSsid));
        s_selLock = nets[idx].lock;
        s_pw[0] = 0; s_kbMode = 0;
        if (!s_selLock) { wifi_cfg_save(s_selSsid, ""); return SCR_WIFI; }
        return SCR_WIFIKEY;
      }
    }
  }

  // ---- render ----
  canvas.fillScreen(C_BG);
  canvas.fillRect(0, 0, 240, W_HDR, C_PANEL);
  canvas.setFont(&fonts::FreeSansBold9pt7b);
  canvas.setTextColor(C_TEXT);
  canvas.setTextDatum(textdatum_t::middle_left);
  canvas.drawString("Wi-Fi Setup", 8, W_HDR / 2);

  if (g_wifiScanState != WSCAN_READY) {
    canvas.setFont(&fonts::Font2);
    canvas.setTextColor(C_DIM);
    canvas.setTextDatum(textdatum_t::middle_center);
    canvas.drawString("Scanning for networks...", 120, 150);
  } else if (n == 0) {
    canvas.setFont(&fonts::Font2);
    canvas.setTextColor(C_DIM);
    canvas.setTextDatum(textdatum_t::middle_center);
    canvas.drawString("No networks found - tap RESCAN", 120, 150);
  } else {
    for (int r = 0; r < W_VIS && s_scroll + r < n; r++) {
      const WifiNet& w = nets[s_scroll + r];
      int y = W_HDR + r * W_ROW;
      canvas.fillRect(0, y, 240, W_ROW - 1, (r & 1) ? C_PANEL2 : C_BG);
      canvas.setFont(&fonts::Font2);
      canvas.setTextColor(C_TEXT);
      canvas.setTextDatum(textdatum_t::middle_left);
      char nm[26]; strlcpy(nm, w.ssid, sizeof(nm));
      canvas.drawString(nm, 10, y + W_ROW / 2);
      // signal bars
      int bars = rssiBars(w.rssi);
      for (int b = 0; b < 4; b++) {
        uint16_t c = b < bars ? C_GOOD : C_PANEL;
        canvas.fillRect(196 + b * 6, y + W_ROW / 2 + 6 - b * 3 - 2, 4, 4 + b * 3, c);
      }
      if (w.lock) {
        canvas.drawRect(180, y + W_ROW / 2 - 4, 8, 8, C_DIM);
        canvas.drawFastHLine(181, y + W_ROW / 2 - 5, 6, C_DIM);
      }
    }
  }

  canvas.fillRect(0, W_FOOT, 240, 24, C_PANEL2);
  canvas.setFont(&fonts::FreeSansBold9pt7b);
  canvas.setTextDatum(textdatum_t::middle_center);
  canvas.setTextColor(C_ACCENT);
  canvas.drawString("RESCAN", 60, W_FOOT + 12);
  if (wifi_cfg_have()) canvas.drawString("DONE", 180, W_FOOT + 12);

  canvas.pushSprite(0, 0);
  return SCR_WIFI;
}

// ---- password keyboard --------------------------------------------------
static const char* KB[3][4] = {
  { "1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm" },
  { "1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM" },
  { "1234567890", "!@#$%^&*()", "-_=+.,:;/", "?'\"[]{}" },
};

#define K_TOP 64
#define K_RH  50
#define K_KW  24

static void key(int x, int y, int w, const char* label, uint16_t bg, uint16_t fg) {
  canvas.fillRoundRect(x + 1, y + 1, w - 2, K_RH - 3, 4, bg);
  canvas.setTextDatum(textdatum_t::middle_center);
  canvas.setFont(&fonts::FreeSansBold9pt7b);
  canvas.setTextColor(fg);
  canvas.drawString(label, x + w / 2, y + K_RH / 2 - 1);
}
static void key(int x, int y, int w, const char* label) {
  key(x, y, w, label, C_PANEL2, C_TEXT);
}

// type 0 = shift (up arrow), 1 = backspace (left arrow with x)
static void glyphKey(int x, int y, int w, int type, uint16_t bg, uint16_t fg) {
  canvas.fillRoundRect(x + 1, y + 1, w - 2, K_RH - 3, 4, bg);
  int cx = x + w / 2, cy = y + K_RH / 2 - 1;
  if (type == 0) {
    canvas.fillTriangle(cx, cy - 8, cx - 9, cy + 1, cx + 9, cy + 1, fg);
    canvas.fillRect(cx - 4, cy + 1, 9, 7, fg);
  } else {
    canvas.fillTriangle(cx - 11, cy, cx - 3, cy - 8, cx - 3, cy + 8, fg);
    canvas.fillRect(cx - 3, cy - 6, 12, 12, fg);
    canvas.drawLine(cx + 1, cy - 3, cx + 6, cy + 2, bg);
    canvas.drawLine(cx + 6, cy - 3, cx + 1, cy + 2, bg);
  }
}

Screen scr_wifikey(const TouchEv& e) {
  if (e.tap) {
    if (e.y < W_HDR) return SCR_WIFI;                       // header = back

    int row = (e.y - K_TOP) / K_RH;
    if (e.y >= K_TOP && row >= 0 && row <= 4) {
      int len = strlen(s_pw);
      if (row <= 1) {
        int col = e.x / K_KW;
        if (col < 10 && len < 64) { s_pw[len] = KB[s_kbMode][row][col]; s_pw[len + 1] = 0; }
      } else if (row == 2) {
        int col = (e.x - 12) / K_KW;
        if (col >= 0 && col < 9 && len < 64) { s_pw[len] = KB[s_kbMode][2][col]; s_pw[len + 1] = 0; }
      } else if (row == 3) {
        if (e.x < 40) { if (s_kbMode < 2) s_kbMode ^= 1; }            // shift (case toggle)
        else if (e.x >= 206) { if (len > 0) s_pw[len - 1] = 0; }      // backspace
        else {
          int col = (e.x - 40) / K_KW;
          const char* r3 = KB[s_kbMode][3];
          if (col >= 0 && col < (int)strlen(r3) && len < 64) { s_pw[len] = r3[col]; s_pw[len + 1] = 0; }
        }
      } else {                                                        // row 4
        if (e.x < 48)       s_kbMode = (s_kbMode == 2) ? 0 : 2;       // ?123 / ABC
        else if (e.x >= 192) {                                        // OK
          wifi_cfg_save(s_selSsid, s_pw);
          return SCR_WIFI;
        } else if (len < 64) { s_pw[len] = ' '; s_pw[len + 1] = 0; }  // space
      }
    }
  }

  canvas.fillScreen(C_BG);
  canvas.fillRect(0, 0, 240, W_HDR, C_PANEL);
  canvas.setFont(&fonts::FreeSansBold9pt7b);
  canvas.setTextColor(C_TEXT);
  canvas.setTextDatum(textdatum_t::middle_left);
  char h[34]; snprintf(h, sizeof(h), "< %s", s_selSsid);
  canvas.drawString(h, 8, W_HDR / 2);

  // password field
  canvas.fillRect(6, 32, 228, 26, C_PANEL2);
  canvas.setFont(&fonts::Font2);
  canvas.setTextColor(C_TEXT);
  canvas.setTextDatum(textdatum_t::middle_left);
  canvas.drawString(s_pw[0] ? s_pw : "enter password", 12, 45);

  // rows 0-2
  for (int r = 0; r <= 2; r++) {
    const char* k = KB[s_kbMode][r];
    int nk = strlen(k);
    int x0 = (r == 2) ? 12 : 0;
    char one[2] = {0, 0};
    for (int c = 0; c < nk; c++) {
      one[0] = k[c];
      key(x0 + c * K_KW, K_TOP + r * K_RH, K_KW, one);
    }
  }
  // row 3: shift (case toggle) | letters | backspace
  int y3 = K_TOP + 3 * K_RH;
  if (s_kbMode == 2)
    key(0, y3, 40, "", C_PANEL2, C_TEXT);                 // no shift on the symbols page
  else if (s_kbMode == 1)
    glyphKey(0, y3, 40, 0, C_LIVE, C_BG);                 // caps active
  else
    glyphKey(0, y3, 40, 0, C_PANEL2, C_TEXT);
  {
    const char* r3 = KB[s_kbMode][3];
    char one[2] = {0, 0};
    for (int c = 0; c < (int)strlen(r3); c++) {
      one[0] = r3[c];
      key(40 + c * K_KW, y3, K_KW, one);
    }
  }
  glyphKey(206, y3, 34, 1, C_PANEL2, C_TEXT);
  // row 4: mode | space | OK
  key(0,   K_TOP + 4 * K_RH, 48,  s_kbMode == 2 ? "ABC" : "?123");
  key(48,  K_TOP + 4 * K_RH, 144, "space");
  key(192, K_TOP + 4 * K_RH, 48,  "OK", C_ACCENT, C_BG);

  canvas.pushSprite(0, 0);
  return SCR_WIFIKEY;
}
