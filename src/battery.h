#pragma once

void  battery_init();
void  battery_update();    // call from the main loop (self rate-limited)
float battery_volts();     // smoothed battery terminal voltage
int   battery_percent();   // 0-100 estimate from a LiPo discharge curve
int   battery_charging();  // 1 charging, 0 full, -1 discharging
