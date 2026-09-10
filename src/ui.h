#pragma once
#include <Arduino.h>
#include "theme.h"

// ---- palette: resolves to the active Theme's fields (see theme.cpp) -------
#define C_BG      g_th.bg
#define C_PANEL   g_th.panel
#define C_PANEL2  g_th.panel2
#define C_TEXT    g_th.text
#define C_DIM     g_th.dim
#define C_LIVE    g_th.live
#define C_ACCENT  g_th.accent
#define C_WIN     g_th.win
#define C_LOSE    g_th.lose
#define C_GOOD    g_th.good
#define C_WARN    g_th.warn

enum Screen {
  SCR_BOOT, SCR_LIST, SCR_DETAIL, SCR_SCHEDULE, SCR_SETTINGS,
  SCR_NEWS, SCR_NEWSITEM, SCR_STATS, SCR_WIFI, SCR_WIFIKEY
};

// One poll of the touch panel, reduced to a gesture.
struct TouchEv {
  bool tap      = false;   // finger lifted after a short, stationary press
  bool dragging = false;   // finger currently moving
  int  x = 0, y = 0;       // tap location (screen px, rotation 0)
  int  dragDy = 0;         // vertical delta since previous poll (px)
};
