#pragma once
#include <stdint.h>

// RGB565 palette. Referenced everywhere via the C_* macros in ui.h.
struct Theme {
  uint16_t bg, panel, panel2, text, dim, live, accent, win, lose, good, warn;
  const char* name;
};

#define THEME_COUNT 7
extern const Theme THEMES[THEME_COUNT];
extern Theme g_th;               // the active theme

void theme_set(int i);
