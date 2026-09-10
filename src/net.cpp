#include "net.h"
#include "games.h"
#include "news.h"
#include "config.h"
#include "settings.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <string.h>
#include "esp_heap_caps.h"

// ---------------------------------------------------------------------------
// ArduinoJson allocator that puts the parse pool in PSRAM
// ---------------------------------------------------------------------------
struct SpiRamAllocator : ArduinoJson::Allocator {
  void* allocate(size_t n) override { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM); }
  void  deallocate(void* p) override { heap_caps_free(p); }
  void* reallocate(void* p, size_t n) override {
    return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM);
  }
};

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

// Days-from-civil (Howard Hinnant) -> UTC epoch. Avoids timegm(), which newlib
// on ESP32 does not provide.
static time_t utc_to_epoch(int y, int mo, int d, int h, int mi, int s) {
  y -= mo <= 2;
  int era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = (unsigned)(y - era * 400);
  unsigned doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  long days = (long)era * 146097 + (long)doe - 719468;
  return (time_t)days * 86400 + h * 3600 + mi * 60 + s;
}

static time_t parseIso(const char* s) {
  if (!s || !*s) return 0;
  int Y, Mo, D, h, mi;
  if (sscanf(s, "%d-%d-%dT%d:%d", &Y, &Mo, &D, &h, &mi) == 5)
    return utc_to_epoch(Y, Mo, D, h, mi, 0);
  return 0;
}

// Shorten a few long network names for the small display.
static void normalizeTv(char* tv) {
  if (strstr(tv, "Prime") || strstr(tv, "Amazon")) { strcpy(tv, "Prime"); return; }
  if (strstr(tv, "Peacock")) { strcpy(tv, "Peacock"); return; }
  if (!strncmp(tv, "ABC/", 4)) { strcpy(tv, "ABC"); return; }
}

static uint16_t colFromHex(const char* hex, uint16_t fallback) {
  if (!hex || strlen(hex) < 6) return fallback;
  uint32_t v = strtoul(hex, nullptr, 16);
  uint8_t r = (v >> 16) & 0xFF, g = (v >> 8) & 0xFF, b = v & 0xFF;
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// ---------------------------------------------------------------------------
// Buffered HTTPS GET -> PSRAM buffer (caller frees). Reading straight off the
// TLS stream is one byte at a time and pegs the core for seconds on big
// responses -> idle-task watchdog reset. This chunks it and yields.
// ---------------------------------------------------------------------------
static char* httpGetBody(const char* url, size_t& outLen, int* httpCode = nullptr) {
  outLen = 0;
  if (httpCode) *httpCode = 0;
  if (WiFi.status() != WL_CONNECTED) return nullptr;

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(10);

  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(10000);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) { Serial.println("[net] http.begin failed"); return nullptr; }
  http.useHTTP10(true);

  int code = http.GET();
  if (httpCode) *httpCode = code;
  if (code != HTTP_CODE_OK) {
    Serial.printf("[net] HTTP %d\n", code);
    http.end();
    return nullptr;
  }

  WiFiClient* st = http.getStreamPtr();
  int clen = http.getSize();
  size_t bcap = (clen > 0) ? (size_t)clen + 1 : 64 * 1024;
  char* body = (char*)heap_caps_malloc(bcap, MALLOC_CAP_SPIRAM);
  if (!body) { http.end(); Serial.println("[net] body malloc failed"); return nullptr; }

  size_t blen = 0;
  uint32_t lastData = millis();
  for (;;) {
    size_t avail = st->available();
    if (avail) {
      if (blen + avail + 1 > bcap) {
        size_t ncap = blen + avail + 1 + 32 * 1024;
        char* nb = (char*)heap_caps_realloc(body, ncap, MALLOC_CAP_SPIRAM);
        if (!nb) { heap_caps_free(body); http.end(); return nullptr; }
        body = nb; bcap = ncap;
      }
      blen += st->readBytes(body + blen, avail);
      lastData = millis();
    } else if ((clen >= 0 && blen >= (size_t)clen) || !http.connected()) {
      break;
    } else if (millis() - lastData > 10000) {
      Serial.println("[net] body read stalled");
      break;
    } else {
      vTaskDelay(pdMS_TO_TICKS(3));
    }
  }
  http.end();

  if (blen < 32) { heap_caps_free(body); return nullptr; }
  body[blen] = 0;
  outLen = blen;
  return body;
}

// ---------------------------------------------------------------------------
// Scoreboard
// ---------------------------------------------------------------------------
static void buildScoreFilter(JsonDocument& f) {
  f["events"][0]["id"] = true;
  f["events"][0]["date"] = true;
  f["events"][0]["status"]["period"] = true;
  f["events"][0]["status"]["displayClock"] = true;
  f["events"][0]["status"]["type"]["state"] = true;
  f["events"][0]["status"]["type"]["shortDetail"] = true;
  f["events"][0]["competitions"][0]["competitors"][0]["homeAway"] = true;
  f["events"][0]["competitions"][0]["competitors"][0]["score"] = true;
  f["events"][0]["competitions"][0]["competitors"][0]["curatedRank"]["current"] = true;
  f["events"][0]["competitions"][0]["competitors"][0]["team"]["id"] = true;
  f["events"][0]["competitions"][0]["competitors"][0]["team"]["abbreviation"] = true;
  f["events"][0]["competitions"][0]["competitors"][0]["team"]["shortDisplayName"] = true;
  f["events"][0]["competitions"][0]["competitors"][0]["team"]["displayName"] = true;
  f["events"][0]["competitions"][0]["competitors"][0]["team"]["color"] = true;
  f["events"][0]["competitions"][0]["broadcasts"][0]["names"] = true;
  f["events"][0]["competitions"][0]["geoBroadcasts"][0]["media"]["shortName"] = true;
  f["events"][0]["competitions"][0]["venue"]["fullName"] = true;
  f["events"][0]["competitions"][0]["situation"]["downDistanceText"] = true;
  f["events"][0]["competitions"][0]["situation"]["shortDownDistanceText"] = true;
  f["events"][0]["competitions"][0]["situation"]["possession"] = true;
  f["events"][0]["competitions"][0]["situation"]["isRedZone"] = true;
  f["events"][0]["competitions"][0]["situation"]["homeTimeouts"] = true;
  f["events"][0]["competitions"][0]["situation"]["awayTimeouts"] = true;
  f["events"][0]["competitions"][0]["situation"]["lastPlay"]["id"] = true;
  f["events"][0]["competitions"][0]["situation"]["lastPlay"]["text"] = true;
  f["events"][0]["competitions"][0]["weather"]["displayValue"] = true;
  f["events"][0]["competitions"][0]["weather"]["shortDisplayName"] = true;
  f["events"][0]["competitions"][0]["weather"]["temperature"] = true;
  f["events"][0]["weather"]["displayValue"] = true;
  f["events"][0]["weather"]["temperature"] = true;
}

static bool fetchLeague(int league) {
  String url = (league == 0) ? NFL_URL : CFB_URL;

  char dates[10] = "";
  if (strlen(DATES_OVERRIDE) > 0) {
    strlcpy(dates, DATES_OVERRIDE, sizeof(dates));
  } else if (league == 1 && g_set.cfbTodayOnly) {
    time_t now = time(nullptr);
    struct tm lt; localtime_r(&now, &lt);
    snprintf(dates, sizeof(dates), "%04d%02d%02d",
             lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
  }
  if (dates[0]) {
    url += (url.indexOf('?') >= 0 ? "&dates=" : "?dates=");
    url += dates;
  }

  int code = 0;
  size_t blen = 0;
  char* body = httpGetBody(url.c_str(), blen, &code);
  g_httpStatus[league] = code;
  if (!body) return false;

  SpiRamAllocator alloc;
  JsonDocument filter;
  buildScoreFilter(filter);
  JsonDocument doc(&alloc);
  DeserializationError err = deserializeJson(
      doc, body, blen,
      DeserializationOption::Filter(filter),
      DeserializationOption::NestingLimit(24));
  heap_caps_free(body);
  if (err) {
    Serial.printf("[net] %s JSON error: %s\n", league ? "CFB" : "NFL", err.c_str());
    return false;
  }

  static Game tmp[MAX_CFB];
  int n = 0;
  int cap = (league == 0) ? MAX_NFL : MAX_CFB;

  for (JsonObject ev : doc["events"].as<JsonArray>()) {
    if (n >= cap) break;
    JsonObject comp = ev["competitions"][0];
    JsonArray  cs   = comp["competitors"].as<JsonArray>();
    if (cs.size() < 2) continue;

    Game g = {};
    g.league = (uint8_t)league;
    strlcpy(g.id, ev["id"] | "", sizeof(g.id));
    g.kickoff = parseIso(ev["date"] | "");

    const char* st = ev["status"]["type"]["state"] | "pre";
    g.state  = !strcmp(st, "in") ? 1 : (!strcmp(st, "post") ? 2 : 0);
    g.period = ev["status"]["period"] | 0;
    strlcpy(g.clock,  ev["status"]["displayClock"] | "", sizeof(g.clock));
    strlcpy(g.detail, ev["status"]["type"]["shortDetail"] | "", sizeof(g.detail));

    for (JsonObject c : cs) {
      bool home = !strcmp(c["homeAway"] | "", "home");
      const char* id = c["team"]["id"] | "";
      const char* ab = c["team"]["abbreviation"] | "";
      const char* nm = c["team"]["shortDisplayName"] | "";
      if (!*nm) nm = c["team"]["displayName"] | "";
      int sc = c["score"].as<int>();
      int cr = c["curatedRank"]["current"] | 99;
      uint8_t rank = (cr >= 1 && cr <= 25) ? (uint8_t)cr : 0;
      uint16_t col = colFromHex(c["team"]["color"] | "", 0x8410);

      if (home) {
        strlcpy(g.homeId, id, sizeof(g.homeId));
        strlcpy(g.home, ab, sizeof(g.home));
        strlcpy(g.homeName, nm, sizeof(g.homeName));
        g.homeScore = (uint8_t)constrain(sc, 0, 255);
        g.homeRank = rank; g.homeColor = col;
      } else {
        strlcpy(g.awayId, id, sizeof(g.awayId));
        strlcpy(g.away, ab, sizeof(g.away));
        strlcpy(g.awayName, nm, sizeof(g.awayName));
        g.awayScore = (uint8_t)constrain(sc, 0, 255);
        g.awayRank = rank; g.awayColor = col;
      }
    }

    const char* tv = comp["broadcasts"][0]["names"][0] | "";
    if (!*tv) tv = comp["geoBroadcasts"][0]["media"]["shortName"] | "";
    strlcpy(g.tv, tv, sizeof(g.tv));
    normalizeTv(g.tv);
    strlcpy(g.venue, comp["venue"]["fullName"] | "", sizeof(g.venue));

    g.toHome = g.toAway = 3;
    JsonObject sit = comp["situation"];
    if (!sit.isNull()) {
      const char* dd = sit["downDistanceText"] | "";
      if (!*dd) dd = sit["shortDownDistanceText"] | "";
      strlcpy(g.downDist, dd, sizeof(g.downDist));
      strlcpy(g.possId, sit["possession"] | "", sizeof(g.possId));
      g.redZone = sit["isRedZone"] | false;
      g.toHome = (uint8_t)constrain((int)(sit["homeTimeouts"] | 3), 0, 3);
      g.toAway = (uint8_t)constrain((int)(sit["awayTimeouts"] | 3), 0, 3);
      if (g.state == 1)
        pbp_note(g.id, sit["lastPlay"]["id"] | "", sit["lastPlay"]["text"] | "");
    }

    {
      JsonObject wx = comp["weather"];
      if (wx.isNull()) wx = ev["weather"];
      bool hasT = !wx["temperature"].isNull();
      int  wt   = wx["temperature"].as<int>();
      const char* wc = wx["displayValue"] | "";
      if (!*wc) wc = wx["shortDisplayName"] | "";
      char cond[16] = "";
      if (*wc) { strlcpy(cond, wc, sizeof(cond)); cond[0] = toupper((unsigned char)cond[0]); }
      if (hasT && *cond)  snprintf(g.weather, sizeof(g.weather), "%dF %s", wt, cond);
      else if (*cond)     strlcpy(g.weather, cond, sizeof(g.weather));
      else if (hasT)      snprintf(g.weather, sizeof(g.weather), "%dF", wt);
    }

    if (league == 1 && g.homeRank == 0 && g.awayRank == 0) continue;   // NCAAF: ranked only

    tmp[n++] = g;
  }

  games_publish(league, tmp, n);
  Serial.printf("[net] %s: %d games (%u KB body)  heap=%u psram=%u\n",
                league ? "CFB" : "NFL", n, (unsigned)(blen / 1024),
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
  return true;
}

// ---------------------------------------------------------------------------
// Team schedule
// ---------------------------------------------------------------------------
static void buildSchedFilter(JsonDocument& f) {
  f["team"]["recordSummary"] = true;
  f["team"]["record"]["items"][0]["summary"] = true;
  f["events"][0]["date"] = true;
  f["events"][0]["week"]["number"] = true;
  f["events"][0]["seasonType"]["name"] = true;
  f["events"][0]["seasonType"]["type"] = true;
  f["events"][0]["competitions"][0]["date"] = true;
  f["events"][0]["competitions"][0]["competitors"][0]["homeAway"] = true;
  f["events"][0]["competitions"][0]["competitors"][0]["winner"] = true;
  f["events"][0]["competitions"][0]["competitors"][0]["score"] = true;
  f["events"][0]["competitions"][0]["competitors"][0]["team"]["id"] = true;
  f["events"][0]["competitions"][0]["competitors"][0]["team"]["abbreviation"] = true;
  f["events"][0]["competitions"][0]["competitors"][0]["team"]["shortDisplayName"] = true;
  f["events"][0]["competitions"][0]["competitors"][0]["team"]["displayName"] = true;
  f["events"][0]["competitions"][0]["status"]["type"]["state"] = true;
  f["events"][0]["competitions"][0]["broadcasts"][0]["media"]["shortName"] = true;
}

static int scoreOf(JsonObject c) {
  int v = c["score"]["value"].as<int>();
  if (v == 0) v = c["score"].as<int>();          // if score is a bare number/string
  return v;
}

static void fetchSchedule(int league, const char* teamId, const char* teamName) {
  char url[176];
  snprintf(url, sizeof(url),
           "https://site.api.espn.com/apis/site/v2/sports/football/%s/teams/%s/schedule",
           league == 0 ? "nfl" : "college-football", teamId);

  size_t blen = 0;
  char* body = httpGetBody(url, blen);
  if (!body) { schedule_fail(); return; }

  SpiRamAllocator alloc;
  JsonDocument filter;
  buildSchedFilter(filter);
  JsonDocument doc(&alloc);
  DeserializationError err = deserializeJson(
      doc, body, blen,
      DeserializationOption::Filter(filter),
      DeserializationOption::NestingLimit(24));
  heap_caps_free(body);
  if (err) {
    Serial.printf("[net] sched JSON error: %s\n", err.c_str());
    schedule_fail();
    return;
  }

  char record[16] = "";
  strlcpy(record, doc["team"]["recordSummary"] | "", sizeof(record));
  if (!record[0])
    strlcpy(record, doc["team"]["record"]["items"][0]["summary"] | "", sizeof(record));

  static SchedGame sg[MAX_SCHED];
  int n = 0;
  for (JsonObject ev : doc["events"].as<JsonArray>()) {
    if (n >= MAX_SCHED) break;
    JsonObject comp = ev["competitions"][0];
    JsonArray  cs   = comp["competitors"].as<JsonArray>();
    if (cs.size() < 2) continue;

    SchedGame g = {};
    g.date = parseIso(ev["date"] | (comp["date"] | ""));

    int wn = ev["week"]["number"] | 0;
    int sty = ev["seasonType"]["type"] | 0;
    const char* stn = ev["seasonType"]["name"] | "";
    g.week = (uint8_t)constrain(wn, 0, 255);
    g.reg  = (sty == 2) || strstr(stn, "Regular");
    if (strstr(stn, "Post") || strstr(stn, "Bowl"))
      strlcpy(g.weekLabel, "Bowl", sizeof(g.weekLabel));
    else if (strstr(stn, "Pre"))
      snprintf(g.weekLabel, sizeof(g.weekLabel), "Pre %d", wn);
    else if (wn > 0)
      snprintf(g.weekLabel, sizeof(g.weekLabel), "Wk %d", wn);

    const char* stt = comp["status"]["type"]["state"] | "pre";
    g.state = !strcmp(stt, "in") ? 1 : (!strcmp(stt, "post") ? 2 : 0);

    JsonObject me, opp;
    bool foundMe = false;
    for (JsonObject c : cs) {
      if (!strcmp(c["team"]["id"] | "", teamId)) { me = c; foundMe = true; }
      else opp = c;
    }
    if (!foundMe) continue;

    g.home = !strcmp(me["homeAway"] | "", "home");
    g.win  = me["winner"] | false;
    g.teamScore = (uint8_t)constrain(scoreOf(me),  0, 255);
    g.oppScore  = (uint8_t)constrain(scoreOf(opp), 0, 255);

    const char* oab = opp["team"]["abbreviation"] | "";
    const char* onm = opp["team"]["shortDisplayName"] | "";
    if (!*onm) onm = opp["team"]["displayName"] | "";
    strlcpy(g.opp, oab, sizeof(g.opp));
    strlcpy(g.oppName, onm, sizeof(g.oppName));
    strlcpy(g.tv, comp["broadcasts"][0]["media"]["shortName"] | "", sizeof(g.tv));
    normalizeTv(g.tv);

    sg[n++] = g;
  }

  // Interleave bye-week rows: an NFL team plays every regular-season week except
  // one, so a +2 jump in week number between two regular-season games is a bye.
  static SchedGame out[MAX_SCHED];
  int m = 0;
  for (int i = 0; i < n && m < MAX_SCHED; i++) {
    out[m++] = sg[i];
    if (league == 0 && i + 1 < n && m < MAX_SCHED &&
        sg[i].reg && sg[i + 1].reg &&
        sg[i].week >= 1 && sg[i + 1].week <= 19 &&
        sg[i + 1].week == sg[i].week + 2) {
      SchedGame b = {};
      b.bye  = true;
      b.week = sg[i].week + 1;
      b.reg  = true;
      b.date = sg[i].date ? sg[i].date + 7L * 86400L : 0;
      snprintf(b.weekLabel, sizeof(b.weekLabel), "Wk %u", b.week);
      out[m++] = b;
    }
  }

  int firstUp = m > 0 ? m - 1 : 0;
  for (int i = 0; i < m; i++) if (out[i].state != 2) { firstUp = i; break; }

  schedule_publish(out, m, firstUp, teamName, record);
  Serial.printf("[net] sched %s: %d rows (%u KB)  heap=%u\n",
                teamName, m, (unsigned)(blen / 1024), (unsigned)ESP.getFreeHeap());
}

static void serviceSchedule() {
  int lg; char id[8]; char name[26];
  if (schedule_take_request(&lg, id, name)) {
    if (WiFi.status() == WL_CONNECTED) fetchSchedule(lg, id, name);
    else schedule_fail();
  }
}

// ---------------------------------------------------------------------------
// News headlines
// ---------------------------------------------------------------------------
static void buildNewsFilter(JsonDocument& f) {
  f["articles"][0]["headline"] = true;
  f["articles"][0]["description"] = true;
  f["articles"][0]["published"] = true;
  f["articles"][0]["byline"] = true;
  f["articles"][0]["type"] = true;
}

// decode the common HTML entities and strip any stray tags, in place
static void sanitizeText(char* s) {
  static const struct { const char* ent; char ch; } E[] = {
    {"&amp;", '&'},  {"&#38;", '&'},
    {"&#39;", '\''}, {"&apos;", '\''}, {"&rsquo;", '\''}, {"&lsquo;", '\''},
    {"&quot;", '"'}, {"&ldquo;", '"'}, {"&rdquo;", '"'},
    {"&nbsp;", ' '}, {"&mdash;", '-'}, {"&ndash;", '-'}, {"&#8217;", '\''},
  };
  char* r = s;
  char* w = s;
  while (*r) {
    if (*r == '<') {                                   // drop "<...>"
      while (*r && *r != '>') r++;
      if (*r) r++;
      continue;
    }
    if (*r == '&') {
      bool hit = false;
      for (auto& e : E) {
        size_t l = strlen(e.ent);
        if (!strncmp(r, e.ent, l)) { *w++ = e.ch; r += l; hit = true; break; }
      }
      if (hit) continue;
      char* semi = strchr(r, ';');                      // any other &...; -> "?"
      if (semi && semi - r <= 8) { *w++ = '?'; r = semi + 1; continue; }
    }
    *w++ = *r++;
  }
  *w = 0;
}

static void fetchNews(int league) {
  char url[128];
  snprintf(url, sizeof(url),
           "https://site.api.espn.com/apis/site/v2/sports/football/%s/news",
           league == 0 ? "nfl" : "college-football");

  size_t blen = 0;
  char* body = httpGetBody(url, blen);
  if (!body) { news_fail(); return; }

  SpiRamAllocator alloc;
  JsonDocument filter;
  buildNewsFilter(filter);
  JsonDocument doc(&alloc);
  DeserializationError err = deserializeJson(
      doc, body, blen,
      DeserializationOption::Filter(filter),
      DeserializationOption::NestingLimit(16));
  heap_caps_free(body);
  if (err) {
    Serial.printf("[net] news JSON error: %s\n", err.c_str());
    news_fail();
    return;
  }

  static NewsItem items[MAX_NEWS];
  int n = 0;
  for (JsonObject a : doc["articles"].as<JsonArray>()) {
    if (n >= MAX_NEWS) break;
    const char* hl = a["headline"] | "";
    if (!*hl) continue;

    NewsItem it = {};
    strlcpy(it.headline, hl, sizeof(it.headline));
    strlcpy(it.desc, a["description"] | "", sizeof(it.desc));
    strlcpy(it.byline, a["byline"] | "", sizeof(it.byline));
    it.published = parseIso(a["published"] | "");
    sanitizeText(it.headline);
    sanitizeText(it.desc);
    sanitizeText(it.byline);
    items[n++] = it;
  }

  news_publish(items, n, league);
  Serial.printf("[net] news %s: %d items (%u KB)  heap=%u\n",
                league ? "CFB" : "NFL", n, (unsigned)(blen / 1024),
                (unsigned)ESP.getFreeHeap());
}

static void serviceNews() {
  int lg;
  if (news_take_request(&lg)) {
    if (WiFi.status() == WL_CONNECTED) fetchNews(lg);
    else news_fail();
  }
}

// ---------------------------------------------------------------------------
// Wi-Fi + time
// ---------------------------------------------------------------------------
static void wifiConnect() {
  if (WiFi.status() == WL_CONNECTED) { g_wifiConnected = true; return; }

  snprintf(g_bootMsg, sizeof(g_bootMsg), "Joining %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) delay(250);

  g_wifiConnected = (WiFi.status() == WL_CONNECTED);
  if (g_wifiConnected)
    snprintf(g_bootMsg, sizeof(g_bootMsg), "Wi-Fi: %s", WiFi.localIP().toString().c_str());
  else
    snprintf(g_bootMsg, sizeof(g_bootMsg), "Wi-Fi failed - retrying");
}

static void ntpSync() {
  snprintf(g_bootMsg, sizeof(g_bootMsg), "Syncing clock");
  configTzTime(TZ_STRING, "pool.ntp.org", "time.nist.gov", "time.google.com");

  struct tm tmnow;
  uint32_t t0 = millis();
  while (!getLocalTime(&tmnow, 500) && millis() - t0 < 15000) delay(250);
  g_timeSynced = getLocalTime(&tmnow, 100);
}

// ---------------------------------------------------------------------------
// Task
// ---------------------------------------------------------------------------
static void netTask(void*) {
  // Long CPU bursts (TLS handshake, JSON parse) on core 0; drop the idle-0
  // watchdog. The fetch loop has its own stall timeouts.
  disableCore0WDT();

  for (;;) {
    if (g_sleepReq) { vTaskDelay(pdMS_TO_TICKS(200)); continue; }

    wifiConnect();
    if (g_wifiConnected && !g_timeSynced) ntpSync();
    serviceSchedule();
    serviceNews();

    if (g_wifiConnected) {
      snprintf(g_bootMsg, sizeof(g_bootMsg), "Loading scores");
      bool a = fetchLeague(0);
      vTaskDelay(pdMS_TO_TICKS(1200));
      serviceSchedule();
      serviceNews();
      bool b = fetchLeague(1);
      g_firstCycleDone = true;

      uint32_t wait = (!a && !b) ? POLL_RETRY_MS
                                 : (g_anyLive ? g_set.liveMs : g_set.idleMs);
      uint32_t t0 = millis();
      while (millis() - t0 < wait) {
        if (WiFi.status() != WL_CONNECTED) break;
        if (g_forceRefresh) { g_forceRefresh = false; break; }
        serviceSchedule();
        serviceNews();
        vTaskDelay(pdMS_TO_TICKS(300));
      }
    } else {
      g_firstCycleDone = true;
      vTaskDelay(pdMS_TO_TICKS(POLL_RETRY_MS));
    }
  }
}

void net_start() {
  xTaskCreatePinnedToCore(netTask, "net", 16384, nullptr, 1, nullptr, 0);
}
