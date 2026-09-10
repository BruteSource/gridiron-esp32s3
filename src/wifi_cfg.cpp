#include "wifi_cfg.h"
#include "config.h"          // WIFI_SSID / WIFI_PASS fallback
#include <Preferences.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

char g_wifiSsid[33] = "";
char g_wifiPass[65] = "";

volatile int g_wifiScanState = WSCAN_IDLE;
volatile int g_wifiJoinState = WJOIN_IDLE;

static SemaphoreHandle_t s_mtx;
static WifiNet s_nets[WIFI_SCAN_MAX];
static int     s_nNets = 0;
static bool    s_scanReq = false;
static bool    s_joinReq = false;
static char    s_joinSsid[33] = "";
static char    s_joinPass[65] = "";

static void ensureMtx() { if (!s_mtx) s_mtx = xSemaphoreCreateMutex(); }

void wifi_cfg_load() {
  ensureMtx();
  Preferences p;
  if (p.begin("grid_wifi", true)) {
    p.getString("ssid", g_wifiSsid, sizeof(g_wifiSsid));
    p.getString("pass", g_wifiPass, sizeof(g_wifiPass));
    p.end();
  }
  if (!g_wifiSsid[0]) {                       // nothing stored -> compiled default
    strlcpy(g_wifiSsid, WIFI_SSID, sizeof(g_wifiSsid));
    strlcpy(g_wifiPass, WIFI_PASS, sizeof(g_wifiPass));
  }
}

bool wifi_cfg_have() {
  return g_wifiSsid[0] &&
         strcmp(g_wifiSsid, "your-network-name") != 0;
}

void wifi_cfg_save(const char* ssid, const char* pass) {
  Preferences p;
  if (p.begin("grid_wifi", false)) {
    p.putString("ssid", ssid);
    p.putString("pass", pass);
    p.end();
  }
  wifi_join_request(ssid, pass);
}

void wifi_cfg_forget() {
  Preferences p;
  if (p.begin("grid_wifi", false)) { p.clear(); p.end(); }
  g_wifiSsid[0] = g_wifiPass[0] = 0;
}

// ---- scan --------------------------------------------------------------
void wifi_scan_request() {
  ensureMtx();
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  s_scanReq = true;
  xSemaphoreGive(s_mtx);
  g_wifiScanState = WSCAN_BUSY;
}

bool wifi_scan_take_request() {
  ensureMtx();
  bool r = false;
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  if (s_scanReq) { s_scanReq = false; r = true; }
  xSemaphoreGive(s_mtx);
  return r;
}

void wifi_scan_publish(const WifiNet* nets, int n) {
  ensureMtx();
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  if (n > WIFI_SCAN_MAX) n = WIFI_SCAN_MAX;
  memcpy(s_nets, nets, (size_t)n * sizeof(WifiNet));
  s_nNets = n;
  xSemaphoreGive(s_mtx);
  g_wifiScanState = WSCAN_READY;
}

int wifi_scan_get(WifiNet* out, int cap) {
  ensureMtx();
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  int n = s_nNets; if (n > cap) n = cap;
  memcpy(out, s_nets, (size_t)n * sizeof(WifiNet));
  xSemaphoreGive(s_mtx);
  return n;
}

// ---- join -------------------------------------------------------------
void wifi_join_request(const char* ssid, const char* pass) {
  ensureMtx();
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  strlcpy(s_joinSsid, ssid, sizeof(s_joinSsid));
  strlcpy(s_joinPass, pass, sizeof(s_joinPass));
  s_joinReq = true;
  xSemaphoreGive(s_mtx);
  g_wifiJoinState = WJOIN_TRYING;
}

bool wifi_join_take_request(char* ssid, char* pass) {
  ensureMtx();
  bool r = false;
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  if (s_joinReq) {
    s_joinReq = false;
    strcpy(ssid, s_joinSsid);
    strcpy(pass, s_joinPass);
    r = true;
  }
  xSemaphoreGive(s_mtx);
  return r;
}
