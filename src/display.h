#pragma once
#include "lgfx_conf.h"

extern LGFX        lcd;
extern LGFX_Sprite canvas;   // 240x320 off-screen buffer (PSRAM)

void display_init();
void scr_boot_draw();        // splash / connecting screen
bool display_calibrate();    // 4-point touch calibration; stores result in flash
