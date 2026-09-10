#pragma once
#include <Arduino.h>

// Active Wi-Fi credentials: an NVS-stored pair set by the on-screen wizard,
// falling back to the compiled WIFI_SSID / WIFI_PASS in credentials.h.
extern char g_wifiSsid[33];
extern char g_wifiPass[65];

void wifi_cfg_load();                 // fill g_wifiSsid/Pass (NVS, else compiled)
bool wifi_cfg_have();                 // have a usable (non-placeholder) SSID?
void wifi_cfg_save(const char* ssid, const char* pass);   // persist + request join
void wifi_cfg_forget();              // wipe the stored pair

// ---- network scan (run by the net task) --------------------------------
struct WifiNet { char ssid[33]; int8_t rssi; bool lock; };
#define WIFI_SCAN_MAX 18

enum { WSCAN_IDLE, WSCAN_BUSY, WSCAN_READY };
extern volatile int g_wifiScanState;

void wifi_scan_request();
bool wifi_scan_take_request();
void wifi_scan_publish(const WifiNet* nets, int n);
int  wifi_scan_get(WifiNet* out, int cap);

// ---- join request (run by the net task) --------------------------------
enum { WJOIN_IDLE, WJOIN_TRYING, WJOIN_OK, WJOIN_FAIL };
extern volatile int g_wifiJoinState;

void wifi_join_request(const char* ssid, const char* pass);
bool wifi_join_take_request(char* ssid, char* pass);   // net side
