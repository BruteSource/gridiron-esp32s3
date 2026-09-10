#pragma once
#include <stdint.h>

// Full-screen celebration when a team scores.
//   side   : 0 = away (left), 1 = home (right)
//   color  : that team's RGB565 color
//   points : score delta (drives the label: TOUCHDOWN / FIELD GOAL / ...)
//   style  : effect selector (mod 3) — cycle it for variety / testing
void anim_score(int side, uint16_t color, int points, int style);
