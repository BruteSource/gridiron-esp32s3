#pragma once
#include "ui.h"

struct TouchCal { int xMin, xMax, yMin, yMax; };   // raw FT6336 value at screen edges

void     touch_init();
TouchEv  touch_poll();                      // gesture, calibrated screen coords

bool     touch_readRaw(int* rx, int* ry);   // raw uncalibrated point, true if touched
bool     touch_is_touched();                // quick "finger down?" check (for sleep poll)
void     touch_bus_resume();                // re-init the I2C bus (after light sleep)
bool     touch_isCalibrated();              // true if a calibration is stored in flash
TouchCal touch_getCal();
void     touch_setCal(const TouchCal& c, bool persist);
