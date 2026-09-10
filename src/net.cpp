#include "net.h"
#include "games.h"
#include "news.h"
#include "stats.h"
#include "config.h"
#include "settings.h"
#include "cfbd_teams.h"
#include "nfl_teams.h"
#include "wifi_cfg.h"

#define TSDB_BASE   "https://www.thesportsdb.com/api/v1/json/3/"
#define TSDB_NFL_ID "4391"

// site.api.espn.com is 403-blocked and cdn.espn.com now 202s us; site.web.api
// (what espn.com's own frontend uses) still serves. Same paths/schema as site.api.
#define ESPNW "https://site.web.api.espn.com/apis/site/v2/sports/football/"

#ifndef CFBD_KEY
#define CFBD_KEY ""
#endif
#ifndef BDL_KEY
#define BDL_KEY ""
#endif

#include <Arduino.h>
#include <algorithm>
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
  if (strstr(tv, "Netflix")) { strcpy(tv, "Netflix"); return; }
  if (!strncmp(tv, "ABC/", 4)) { strcpy(tv, "ABC"); return; }
}

// Broadcast map lives in games.cpp (persisted with the slate). Refreshed ~daily.
static void tvSet(const char* away, const char* home, const char* tv) {
  if (!tv || !*tv) return;
  char clean[14]; strlcpy(clean, tv, sizeof(clean));
  normalizeTv(clean);
  tv_set(away, home, clean);
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
static char* httpGetBody(const char* url, size_t& outLen, int* httpCode = nullptr,
                         const char* authHeader = nullptr) {
  outLen = 0;
  if (httpCode) *httpCode = 0;
  if (WiFi.status() != WL_CONNECTED) return nullptr;

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(12);

  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(12000);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) { Serial.println("[net] http.begin failed"); return nullptr; }
  http.useHTTP10(true);
  // A default "ESP32HTTPClient" UA gets flagged/rate-limited harder by Akamai.
  http.setUserAgent("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
                    "(KHTML, like Gecko) Chrome/122.0 Safari/537.36");
  http.addHeader("Accept", "application/json, text/plain, */*");
  if (authHeader && *authHeader) http.addHeader("Authorization", authHeader);

  int code = http.GET();
  if (code == 202) {                       // cdn.espn.com cache-miss: warm + retry once
    http.end();
    vTaskDelay(pdMS_TO_TICKS(900));
    if (!http.begin(client, url)) return nullptr;
    http.useHTTP10(true);
    http.setUserAgent("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
                      "(KHTML, like Gecko) Chrome/122.0 Safari/537.36");
    if (authHeader && *authHeader) http.addHeader("Authorization", authHeader);
    code = http.GET();
  }
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
    } else if (millis() - lastData > 15000) {
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

// forward decls (alternate data sources are defined further down)
static bool cfbd_available();
static bool cfbd_poll_college();
static void cfbdFetchSchedule(const char* teamName);
static bool tsdb_poll_nfl();
static bool bdl_available();
static bool bdl_poll_nfl();
static void bdlFetchSchedule(const char* teamId, const char* teamName);

// Shared parse scratch — the net task is single-threaded, so ESPN and CFBD
// fetches never build these at the same time. Keeps ~20 KB of RAM off the map.
static Game      s_gameBuf[MAX_CFB];
static SchedGame s_schBufA[MAX_SCHED];
static SchedGame s_schBufB[MAX_SCHED];

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

  Game* tmp = s_gameBuf;
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

  SchedGame* sg = s_schBufA;
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
  SchedGame* out = s_schBufB;
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
  if (!schedule_take_request(&lg, id, name)) return;
  if (WiFi.status() != WL_CONNECTED) { schedule_fail(); return; }
  if (lg == 1 && cfbd_available())      cfbdFetchSchedule(name);
  else if (lg == 0 && bdl_available())  bdlFetchSchedule(id, name);
  else                                 fetchSchedule(lg, id, name);
}

// ---------------------------------------------------------------------------
// News headlines  (ESPN RSS on www.espn.com — a different host from the
// site.api.espn.com scoreboard that Akamai rate-limits)
// ---------------------------------------------------------------------------
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

// inner text of <tag>..</tag> within `item`, CDATA unwrapped, into `out`
static void rssTag(const char* item, const char* tag, char* out, size_t cap) {
  out[0] = 0;
  char open[20], close[20];
  snprintf(open, sizeof(open), "<%s>", tag);
  snprintf(close, sizeof(close), "</%s>", tag);
  const char* s = strstr(item, open);
  if (!s) return;
  s += strlen(open);
  const char* e = strstr(s, close);
  if (!e) return;
  if (!strncmp(s, "<![CDATA[", 9)) {
    s += 9;
    const char* cd = strstr(s, "]]>");
    if (cd && cd < e) e = cd;
  }
  size_t len = (size_t)(e - s);
  if (len >= cap) len = cap - 1;
  memcpy(out, s, len);
  out[len] = 0;
}

static time_t parseRfc822(const char* s) {          // "Thu, 10 Sep 2026 11:03:03 EST"
  static const char* M[] = {"Jan","Feb","Mar","Apr","May","Jun",
                            "Jul","Aug","Sep","Oct","Nov","Dec"};
  int d, y, h, mi, se = 0; char mn[8] = "";
  const char* c = strchr(s, ',');
  if (c) s = c + 1;
  while (*s == ' ') s++;
  if (sscanf(s, "%d %3s %d %d:%d:%d", &d, mn, &y, &h, &mi, &se) < 5) return 0;
  int mo = 0;
  for (int i = 0; i < 12; i++) if (!strncmp(mn, M[i], 3)) { mo = i + 1; break; }
  if (!mo) return 0;
  return utc_to_epoch(y, mo, d, h, mi, se) + 5 * 3600;   // feed is US Eastern-ish
}

static void fetchNews(int league) {
  char url[80];
  snprintf(url, sizeof(url), "https://www.espn.com/espn/rss/%s/news",
           league == 0 ? "nfl" : "ncf");

  size_t blen = 0;
  char* body = httpGetBody(url, blen);
  if (!body) { news_fail(); return; }

  static NewsItem items[MAX_NEWS];
  int n = 0;
  char* p = body;
  while (n < MAX_NEWS) {
    p = strstr(p, "<item>");
    if (!p) break;
    char* end = strstr(p, "</item>");
    if (!end) break;
    *end = 0;

    NewsItem it = {};
    rssTag(p, "title", it.headline, sizeof(it.headline));
    rssTag(p, "description", it.desc, sizeof(it.desc));
    char dt[40]; rssTag(p, "pubDate", dt, sizeof(dt));
    it.published = parseRfc822(dt);
    sanitizeText(it.headline);
    sanitizeText(it.desc);
    if (it.headline[0]) items[n++] = it;

    *end = '<';
    p = end + 7;
  }
  heap_caps_free(body);

  if (n == 0) { news_fail(); return; }
  news_publish(items, n, league);
  Serial.printf("[net] news %s (RSS): %d items (%u KB)\n",
                league ? "CFB" : "NFL", n, (unsigned)(blen / 1024));
}

static void serviceNews() {
  int lg;
  if (news_take_request(&lg)) {
    if (WiFi.status() == WL_CONNECTED) fetchNews(lg);
    else news_fail();
  }
}

// ---------------------------------------------------------------------------
// Live team stats (ESPN game summary -> boxscore.teams)
// ---------------------------------------------------------------------------
static void buildStatsFilter(JsonDocument& f) {
  JsonObject bx = f["boxscore"].to<JsonObject>();
  bx["teams"][0]["homeAway"] = true;
  bx["teams"][0]["team"]["abbreviation"] = true;
  bx["teams"][0]["statistics"][0]["name"] = true;
  bx["teams"][0]["statistics"][0]["label"] = true;
  bx["teams"][0]["statistics"][0]["displayValue"] = true;

  bx["players"][0]["team"]["abbreviation"] = true;
  bx["players"][0]["statistics"][0]["name"] = true;
  bx["players"][0]["statistics"][0]["labels"] = true;
  bx["players"][0]["statistics"][0]["athletes"][0]["athlete"]["shortName"] = true;
  bx["players"][0]["statistics"][0]["athletes"][0]["athlete"]["displayName"] = true;
  bx["players"][0]["statistics"][0]["athletes"][0]["stats"] = true;
}

static const char* statValue(JsonObject team, const char* name) {
  for (JsonObject s : team["statistics"].as<JsonArray>())
    if (!strcmp(s["name"] | "", name)) return s["displayValue"] | "";
  return "";
}

// value of the column titled `label` from one athlete's parallel stats[] array
static const char* athCol(JsonArray labels, JsonArray stats, const char* label) {
  int i = 0;
  for (JsonVariant l : labels) {
    if (!strcmp(l | "", label)) return stats[i] | "";
    i++;
  }
  return "";
}

// "Drake Maye" -> "D. Maye"
static void shortName(const char* full, char* out, size_t cap) {
  const char* sp = strrchr(full, ' ');
  if (sp && sp != full && full[0]) snprintf(out, cap, "%c. %s", full[0], sp + 1);
  else                             strlcpy(out, full, cap);
}

// Pull the skill-position lines (QB/RB/WR) for both teams into gs.players[].
static void parsePlayers(JsonObject boxscore, GameStats& gs) {
  JsonArray pteams = boxscore["players"].as<JsonArray>();
  for (int side = 0; side < 2; side++) {                 // away first, then home
    for (JsonObject pt : pteams) {
      const char* ab = pt["team"]["abbreviation"] | "";
      if ((!strcmp(ab, gs.home) ? 1 : 0) != side) continue;

      for (JsonObject cat : pt["statistics"].as<JsonArray>()) {
        const char* cn = cat["name"] | "";
        char letter; int cap;
        if      (!strcmp(cn, "passing"))   { letter = 'P'; cap = 2; }
        else if (!strcmp(cn, "rushing"))   { letter = 'R'; cap = 3; }
        else if (!strcmp(cn, "receiving")) { letter = 'C'; cap = 4; }
        else continue;

        JsonArray labels = cat["labels"].as<JsonArray>();
        int added = 0;
        for (JsonObject a : cat["athletes"].as<JsonArray>()) {
          if (added >= cap || gs.pn >= MAX_PLAYERS) break;
          JsonArray st = a["stats"].as<JsonArray>();
          const char* nm = a["athlete"]["shortName"] | "";
          if (!*nm) nm = a["athlete"]["displayName"] | "";
          if (!*nm) continue;

          char line[22] = "";
          if (letter == 'P') {
            const char* ca = athCol(labels, st, "C/ATT");
            const char* yd = athCol(labels, st, "YDS");
            const char* td = athCol(labels, st, "TD");
            const char* in = athCol(labels, st, "INT");
            if (!*ca || !strcmp(ca, "0/0")) continue;
            int w = snprintf(line, sizeof(line), "%s %syd", ca, yd);
            if (*td && strcmp(td, "0")) w += snprintf(line + w, sizeof(line) - w, " %sTD", td);
            if (*in && strcmp(in, "0")) snprintf(line + w, sizeof(line) - w, " %sIN", in);
          } else {
            const char* c1 = athCol(labels, st, letter == 'R' ? "CAR" : "REC");
            const char* yd = athCol(labels, st, "YDS");
            const char* td = athCol(labels, st, "TD");
            if (!*c1 || !strcmp(c1, "0")) continue;
            int w = snprintf(line, sizeof(line), "%s-%s", c1, yd);
            if (*td && strcmp(td, "0")) snprintf(line + w, sizeof(line) - w, " %sTD", td);
          }

          PlayerRow& pr = gs.players[gs.pn++];
          pr.side = (uint8_t)side;
          pr.cat  = letter;
          shortName(nm, pr.name, sizeof(pr.name));
          strlcpy(pr.line, line, sizeof(pr.line));
          added++;
        }
      }
    }
  }
}

static void fetchStats(int league, const char* gameId) {
  char url[160];
  snprintf(url, sizeof(url), ESPNW "%s/summary?event=%s",
           league == 0 ? "nfl" : "college-football", gameId);

  size_t blen = 0;
  char* body = httpGetBody(url, blen);
  if (!body) { stats_fail(); return; }

  SpiRamAllocator alloc;
  JsonDocument filter;
  buildStatsFilter(filter);
  JsonDocument doc(&alloc);
  DeserializationError err = deserializeJson(
      doc, body, blen,
      DeserializationOption::Filter(filter),
      DeserializationOption::NestingLimit(50));
  heap_caps_free(body);
  if (err) {
    Serial.printf("[net] stats JSON error: %s\n", err.c_str());
    stats_fail();
    return;
  }

  JsonObject bx = doc["boxscore"];
  JsonArray teams = bx["teams"].as<JsonArray>();
  if (teams.size() < 2) { stats_fail(); return; }

  JsonObject t0 = teams[0], t1 = teams[1];
  bool t0home = !strcmp(t0["homeAway"] | "", "home");
  JsonObject away = t0home ? t1 : t0;
  JsonObject home = t0home ? t0 : t1;

  // Curated stats, in display order. `label` from ESPN is verbose, so name them.
  static const struct { const char* name; const char* label; } WANT[] = {
    { "firstDowns",          "First Downs"  },
    { "totalYards",          "Total Yards"  },
    { "netPassingYards",     "Passing Yds"  },
    { "rushingYards",        "Rushing Yds"  },
    { "thirdDownEff",        "3rd Down"     },
    { "fourthDownEff",       "4th Down"     },
    { "totalPenaltiesYards", "Penalties"    },
    { "turnovers",           "Turnovers"    },
    { "sacksYardsLost",      "Sacks"        },
    { "possessionTime",      "Possession"   },
  };

  static GameStats gs;
  memset(&gs, 0, sizeof(gs));
  strlcpy(gs.away, away["team"]["abbreviation"] | "", sizeof(gs.away));
  strlcpy(gs.home, home["team"]["abbreviation"] | "", sizeof(gs.home));
  for (auto& w : WANT) {
    if (gs.n >= MAX_STAT_ROWS) break;
    const char* av = statValue(away, w.name);
    const char* hv = statValue(home, w.name);
    if (!*av && !*hv) continue;
    StatRow& r = gs.rows[gs.n++];
    strlcpy(r.label, w.label, sizeof(r.label));
    strlcpy(r.away, *av ? av : "-", sizeof(r.away));
    strlcpy(r.home, *hv ? hv : "-", sizeof(r.home));
  }

  parsePlayers(bx, gs);

  if (gs.n == 0 && gs.pn == 0) { stats_fail(); return; }
  stats_publish(&gs);
  Serial.printf("[net] stats %s: %d team + %d player rows (%u KB)  heap=%u\n",
                gameId, gs.n, gs.pn, (unsigned)(blen / 1024), (unsigned)ESP.getFreeHeap());
}

static void serviceStats() {
  int lg; char id[12];
  if (stats_take_request(&lg, id)) {
    if (WiFi.status() == WL_CONNECTED) fetchStats(lg, id);
    else stats_fail();
  }
}

// ===========================================================================
// Focused live game + weekly TV, from site.web.api.espn.com (still un-blocked).
//  - the NFL scoreboard (once/day) gives the balldontlie<->ESPN id map + TV
//  - the /summary of the focused game (every ~30 s while its live detail is up)
//    gives clock, play-by-play, down & distance, possession, timeouts
// ===========================================================================
static struct { char bdl[12]; char espn[12]; } s_nflMap[MAX_NFL];
static int      s_nflMapN = 0;
static struct { char espn[12]; char aw[8]; char hm[8]; } s_nflSb[24];
static int      s_nflSbN  = 0;
static uint32_t s_nflSbAt = 0;

static bool abbrEq(const char* a, const char* b) {
  if (!strcasecmp(a, b)) return true;
  struct { const char* x; const char* y; } A[] = {
    {"WAS","WSH"}, {"LAR","LA"}, {"JAX","JAC"}, {"LV","LVR"}
  };
  for (auto& p : A) if ((!strcasecmp(a, p.x) && !strcasecmp(b, p.y)) ||
                        (!strcasecmp(a, p.y) && !strcasecmp(b, p.x))) return true;
  return false;
}

// Pull the NFL scoreboard once/day: fills the id map (keyed by nothing yet —
// resolved on demand by team match) and the TV map.
static void nflRefreshScoreboard(bool force) {
  if (!force && s_nflSbAt && millis() - s_nflSbAt < 20UL * 3600 * 1000) return;
  size_t blen = 0;
  char* body = httpGetBody(ESPNW "nfl/scoreboard", blen);
  if (!body) return;

  SpiRamAllocator alloc;
  JsonDocument filter;
  JsonObject e = filter["events"][0].to<JsonObject>();
  e["id"] = true;
  JsonObject c = e["competitions"][0].to<JsonObject>();
  c["competitors"][0]["homeAway"] = true;
  c["competitors"][0]["team"]["abbreviation"] = true;
  c["broadcasts"][0]["names"] = true;
  c["geoBroadcasts"][0]["media"]["shortName"] = true;
  JsonDocument doc(&alloc);
  if (deserializeJson(doc, body, blen, DeserializationOption::Filter(filter),
                      DeserializationOption::NestingLimit(24))) { heap_caps_free(body); return; }
  heap_caps_free(body);

  s_nflSbAt = millis();
  s_nflSbN = 0;
  int tv = 0;
  for (JsonObject ev : doc["events"].as<JsonArray>()) {
    JsonObject cc = ev["competitions"][0];
    JsonArray cs = cc["competitors"].as<JsonArray>();
    if (cs.size() < 2) continue;
    char aw[8] = "", hm[8] = "";
    for (JsonObject cp : cs) {
      const char* ab = cp["team"]["abbreviation"] | "";
      if (!strcmp(cp["homeAway"] | "", "home")) strlcpy(hm, ab, sizeof(hm));
      else                                      strlcpy(aw, ab, sizeof(aw));
    }
    const char* eid = ev["id"] | "";
    if (s_nflSbN < 24 && *eid && *aw && *hm) {
      strlcpy(s_nflSb[s_nflSbN].espn, eid, 12);
      strlcpy(s_nflSb[s_nflSbN].aw, aw, 8);
      strlcpy(s_nflSb[s_nflSbN].hm, hm, 8);
      s_nflSbN++;
    }
    const char* t = cc["broadcasts"][0]["names"][0] | "";
    if (!*t) t = cc["geoBroadcasts"][0]["media"]["shortName"] | "";
    // TV map wants the balldontlie abbr form; ESPN's is close enough via tvSet key
    if (*aw && *hm && *t) { tvSet(aw, hm, t); tv++; }
  }
  Serial.printf("[espn] NFL scoreboard: %d games, %d TV (%u KB)\n",
                s_nflSbN, tv, (unsigned)(blen / 1024));
}

static bool nflEspnId(const Game& g, char* out, size_t cap) {
  for (int i = 0; i < s_nflMapN; i++)
    if (!strcmp(s_nflMap[i].bdl, g.id)) { strlcpy(out, s_nflMap[i].espn, cap); return true; }

  nflRefreshScoreboard(false);
  for (int i = 0; i < s_nflSbN; i++) {
    bool match = (abbrEq(s_nflSb[i].aw, g.away) && abbrEq(s_nflSb[i].hm, g.home)) ||
                 (abbrEq(s_nflSb[i].aw, g.home) && abbrEq(s_nflSb[i].hm, g.away));
    if (!match) continue;
    if (s_nflMapN < MAX_NFL) {
      strlcpy(s_nflMap[s_nflMapN].bdl, g.id, 12);
      strlcpy(s_nflMap[s_nflMapN].espn, s_nflSb[i].espn, 12);
      s_nflMapN++;
    }
    strlcpy(out, s_nflSb[i].espn, cap);
    return true;
  }
  return false;
}

static void fetchGameLive(int league, const char* storeId, const char* espnId) {
  char url[160];
  snprintf(url, sizeof(url), ESPNW "%s/summary?event=%s",
           league == 0 ? "nfl" : "college-football", espnId);
  size_t blen = 0;
  char* body = httpGetBody(url, blen);
  if (!body) return;

  SpiRamAllocator alloc;
  JsonDocument filter;
  JsonObject c = filter["header"]["competitions"][0].to<JsonObject>();
  c["status"]["period"] = true;
  c["status"]["displayClock"] = true;
  c["status"]["type"]["state"] = true;
  c["status"]["type"]["shortDetail"] = true;
  c["competitors"][0]["homeAway"] = true;
  c["competitors"][0]["id"] = true;
  c["competitors"][0]["score"] = true;
  c["situation"]["downDistanceText"] = true;
  c["situation"]["shortDownDistanceText"] = true;
  c["situation"]["possession"] = true;
  c["situation"]["isRedZone"] = true;
  c["situation"]["homeTimeouts"] = true;
  c["situation"]["awayTimeouts"] = true;
  c["situation"]["lastPlay"]["id"] = true;
  c["situation"]["lastPlay"]["text"] = true;
  JsonObject sit = filter["situation"].to<JsonObject>();      // some feeds put it top-level
  sit["downDistanceText"] = true;
  sit["possession"] = true;
  sit["isRedZone"] = true;
  sit["homeTimeouts"] = true;
  sit["awayTimeouts"] = true;
  sit["lastPlay"]["id"] = true;
  sit["lastPlay"]["text"] = true;
  filter["drives"]["current"]["plays"][0]["id"] = true;
  filter["drives"]["current"]["plays"][0]["text"] = true;

  JsonDocument doc(&alloc);
  DeserializationError err = deserializeJson(doc, body, blen,
      DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(40));
  heap_caps_free(body);
  if (err) { Serial.printf("[live] JSON error: %s\n", err.c_str()); return; }

  JsonObject comp = doc["header"]["competitions"][0];
  JsonObject stt  = comp["status"];
  if (strcmp(stt["type"]["state"] | "", "in") != 0) return;   // not live -> list poll handles it

  uint8_t period = (uint8_t)(stt["period"] | 0);
  const char* clock = stt["displayClock"] | "";
  char detail[36]; strlcpy(detail, stt["type"]["shortDetail"] | "", sizeof(detail));

  JsonObject situ = comp["situation"];
  if (situ.isNull()) situ = doc["situation"];
  char dd[42];
  strlcpy(dd, situ["downDistanceText"] | (situ["shortDownDistanceText"] | ""), sizeof(dd));
  const char* possId = situ["possession"] | "";
  bool redZone = situ["isRedZone"] | false;
  uint8_t toHome = (uint8_t)constrain((int)(situ["homeTimeouts"] | 3), 0, 3);
  uint8_t toAway = (uint8_t)constrain((int)(situ["awayTimeouts"] | 3), 0, 3);

  int possSide = -1, aScore = -1, hScore = -1;
  for (JsonObject cp : comp["competitors"].as<JsonArray>()) {
    bool home = !strcmp(cp["homeAway"] | "", "home");
    int sc = cp["score"].as<int>();
    if (home) hScore = sc; else aScore = sc;
    if (*possId && !strcmp(cp["id"] | "", possId)) possSide = home ? 1 : 0;
  }

  games_patch_live(storeId, period, clock, detail, dd, possSide,
                   toAway, toHome, redZone, aScore, hScore);

  for (JsonObject pl : doc["drives"]["current"]["plays"].as<JsonArray>())
    pbp_note(storeId, pl["id"] | "", pl["text"] | "");
  pbp_note(storeId, situ["lastPlay"]["id"] | "", situ["lastPlay"]["text"] | "");

  Serial.printf("[live] %s Q%d %s (%u KB) heap=%u\n", espnId, period, clock,
                (unsigned)(blen / 1024), (unsigned)ESP.getFreeHeap());
}

static void serviceDetailFocus() {
  static uint32_t s_at = 0;
  int lg; char id[12];
  if (!detail_focus_get(&lg, id)) return;
  if (s_at && millis() - s_at < 28000) return;
  s_at = millis();
  if (WiFi.status() != WL_CONNECTED) return;

  Game g;
  if (!games_find(lg, id, &g)) return;

  char espnId[12] = "";
  if (lg == 1) strlcpy(espnId, id, sizeof(espnId));       // CFBD id == ESPN id
  else if (!nflEspnId(g, espnId, sizeof(espnId))) return;

  fetchGameLive(lg, id, espnId);
}

// ===========================================================================
// College via CollegeFootballData.com  (spreads load off ESPN)
//   /calendar  -> current week            (cached ~12 h)
//   /rankings  -> AP Top 25 school->rank  (cached ~60 min)
//   /games     -> the week's slate        (every poll, ~65 KB vs ESPN's 1.5 MB)
// Free tier has no live clock, so a live game shows score + estimated quarter;
// the detail screen pulls the real clock/PBP from ESPN on demand.
// ===========================================================================
static bool cfbd_available() {
  const char* k = CFBD_KEY;
  return k[0] && strcmp(k, "your-cfbd-key") != 0;
}

static int      s_cfbYear = 0, s_cfbWeek = 0;
static char     s_cfbType[12] = "regular";
static uint32_t s_calAt = 0, s_rankAt = 0;
static struct { char school[28]; uint8_t rank; } s_rank[25];
static int      s_rankN = 0;

static int cfbdSeasonYear() {
  time_t now = time(nullptr);
  struct tm lt; localtime_r(&now, &lt);
  int y = lt.tm_year + 1900;
  return (lt.tm_mon == 0) ? y - 1 : y;          // January = tail of last season
}

static uint8_t cfbdRankOf(const char* school) {
  if (!school || !*school) return 0;
  for (int i = 0; i < s_rankN; i++)
    if (!strcmp(s_rank[i].school, school)) return s_rank[i].rank;
  return 0;
}

static char* cfbdGet(const char* path, size_t& blen, int* code = nullptr) {
  char url[224];
  snprintf(url, sizeof(url), "https://api.collegefootballdata.com%s", path);
  static char auth[96];
  snprintf(auth, sizeof(auth), "Bearer %s", CFBD_KEY);
  return httpGetBody(url, blen, code, auth);
}

static void cfbdRefreshCalendar() {
  char path[48];
  snprintf(path, sizeof(path), "/calendar?year=%d", s_cfbYear);
  size_t blen = 0;
  char* body = cfbdGet(path, blen);
  time_t now = time(nullptr);

  if (body) {
    SpiRamAllocator alloc;
    JsonDocument filter;
    filter[0]["week"] = true;
    filter[0]["seasonType"] = true;
    filter[0]["startDate"] = true;
    filter[0]["endDate"] = true;
    JsonDocument doc(&alloc);
    DeserializationError err = deserializeJson(doc, body, blen,
        DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(8));
    heap_caps_free(body);
    if (!err) {
      int lastWk = 0; const char* lastType = "regular";
      for (JsonObject w : doc.as<JsonArray>()) {
        int wk = w["week"] | 0;
        const char* ty = w["seasonType"] | "regular";
        time_t s = parseIso(w["startDate"] | ""), e = parseIso(w["endDate"] | "");
        if (wk > 0) { lastWk = wk; lastType = ty; }
        if (s && e && now >= s && now < e) {
          s_cfbWeek = wk; strlcpy(s_cfbType, ty, sizeof(s_cfbType));
          Serial.printf("[cfbd] week %d %s\n", s_cfbWeek, s_cfbType);
          return;
        }
      }
      if (lastWk) { s_cfbWeek = lastWk; strlcpy(s_cfbType, lastType, sizeof(s_cfbType)); return; }
    }
  }
  // fallback: weeks since ~Aug 23
  time_t seasonStart = utc_to_epoch(s_cfbYear, 8, 23, 0, 0, 0);
  int wk = (int)((now - seasonStart) / (7 * 86400)) + 1;
  s_cfbWeek = wk < 1 ? 1 : (wk > 16 ? 16 : wk);
  strlcpy(s_cfbType, "regular", sizeof(s_cfbType));
  Serial.printf("[cfbd] week %d (estimated)\n", s_cfbWeek);
}

static bool cfbdRefreshRanks() {
  char path[72];
  snprintf(path, sizeof(path), "/rankings?year=%d&week=%d&seasonType=%s",
           s_cfbYear, s_cfbWeek, s_cfbType);
  size_t blen = 0;
  char* body = cfbdGet(path, blen);
  if (!body) return false;

  SpiRamAllocator alloc;
  JsonDocument filter;
  filter[0]["polls"][0]["poll"] = true;
  filter[0]["polls"][0]["ranks"][0]["rank"] = true;
  filter[0]["polls"][0]["ranks"][0]["school"] = true;
  JsonDocument doc(&alloc);
  DeserializationError err = deserializeJson(doc, body, blen,
      DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(12));
  heap_caps_free(body);
  if (err) { Serial.printf("[cfbd] rankings error: %s\n", err.c_str()); return false; }

  int n = 0;
  for (JsonObject wk : doc.as<JsonArray>()) {
    for (JsonObject poll : wk["polls"].as<JsonArray>()) {
      const char* pn = poll["poll"] | "";
      if (strcmp(pn, "AP Top 25") != 0) continue;
      for (JsonObject r : poll["ranks"].as<JsonArray>()) {
        if (n >= 25) break;
        strlcpy(s_rank[n].school, r["school"] | "", sizeof(s_rank[n].school));
        s_rank[n].rank = (uint8_t)(r["rank"] | 0);
        n++;
      }
    }
  }
  s_rankN = n;
  Serial.printf("[cfbd] AP Top 25: %d teams\n", n);
  return n > 0;
}

static bool cfbdFetchGames() {
  char path[96];
  snprintf(path, sizeof(path),
           "/games?year=%d&week=%d&seasonType=%s&classification=fbs",
           s_cfbYear, s_cfbWeek, s_cfbType);
  int code = 0;
  size_t blen = 0;
  char* body = cfbdGet(path, blen, &code);
  g_httpStatus[1] = code;
  if (!body) return false;

  SpiRamAllocator alloc;
  JsonDocument filter;
  filter[0]["id"] = true;          filter[0]["startDate"] = true;
  filter[0]["completed"] = true;   filter[0]["venue"] = true;
  filter[0]["homeId"] = true;      filter[0]["homeTeam"] = true;
  filter[0]["homePoints"] = true;  filter[0]["homeLineScores"] = true;
  filter[0]["awayId"] = true;      filter[0]["awayTeam"] = true;
  filter[0]["awayPoints"] = true;  filter[0]["awayLineScores"] = true;
  JsonDocument doc(&alloc);
  DeserializationError err = deserializeJson(doc, body, blen,
      DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(10));
  heap_caps_free(body);
  if (err) { Serial.printf("[cfbd] games error: %s\n", err.c_str()); return false; }

  Game* tmp = s_gameBuf;
  int n = 0;
  time_t now = time(nullptr);
  struct tm today; localtime_r(&now, &today);
  for (JsonObject ev : doc.as<JsonArray>()) {
    if (n >= MAX_CFB) break;
    const char* hs = ev["homeTeam"] | "";
    const char* as = ev["awayTeam"] | "";
    uint8_t hr = cfbdRankOf(hs), ar = cfbdRankOf(as);
    if (!hr && !ar) continue;                     // NCAAF tab = ranked teams only

    Game g = {};
    g.league = 1;
    snprintf(g.id, sizeof(g.id), "%ld", (long)(ev["id"] | 0));
    g.kickoff = parseIso(ev["startDate"] | "");

    bool completed = ev["completed"] | false;
    bool hasPts = !ev["homePoints"].isNull();
    if (completed)                                  g.state = 2;
    else if (g.kickoff && now >= g.kickoff - 300)   g.state = 1;
    else                                            g.state = 0;

    int hq = ev["homeLineScores"].isNull() ? 0 : (int)ev["homeLineScores"].as<JsonArray>().size();
    int aq = ev["awayLineScores"].isNull() ? 0 : (int)ev["awayLineScores"].as<JsonArray>().size();
    int q = hq > aq ? hq : aq;
    g.period = g.state == 0 ? 0 : (uint8_t)(q < 1 ? 1 : q);

    strlcpy(g.homeName, hs, sizeof(g.homeName));
    strlcpy(g.awayName, as, sizeof(g.awayName));
    cfbdAbbr(hs, g.home, sizeof(g.home));
    cfbdAbbr(as, g.away, sizeof(g.away));
    g.homeColor = cfbdColor(hs);
    g.awayColor = cfbdColor(as);
    snprintf(g.homeId, sizeof(g.homeId), "%ld", (long)(ev["homeId"] | 0));
    snprintf(g.awayId, sizeof(g.awayId), "%ld", (long)(ev["awayId"] | 0));
    g.homeScore = (uint8_t)constrain(hasPts ? (int)(ev["homePoints"] | 0) : 0, 0, 255);
    g.awayScore = (uint8_t)constrain(hasPts ? (int)(ev["awayPoints"] | 0) : 0, 0, 255);
    g.homeRank = hr;
    g.awayRank = ar;
    strlcpy(g.venue, ev["venue"] | "", sizeof(g.venue));
    g.toHome = g.toAway = 3;
    g.clock[0] = 0;                               // no live clock on the free tier
    { const char* _t = tv_get(g.away, g.home); if (*_t) strlcpy(g.tv, _t, sizeof(g.tv)); }

    tmp[n++] = g;
  }

  // "NCAAF today only" — narrow to today's slate, but only if that leaves
  // something; on a weekday the whole week is better than a blank screen.
  if (g_set.cfbTodayOnly) {
    int t = 0;
    for (int i = 0; i < n; i++) {
      struct tm k;
      if (tmp[i].kickoff) localtime_r(&tmp[i].kickoff, &k);
      if (!tmp[i].kickoff ||
          (k.tm_yday == today.tm_yday && k.tm_year == today.tm_year))
        tmp[t++] = tmp[i];
    }
    if (t > 0) n = t;
  }

  games_publish(1, tmp, n);
  Serial.printf("[cfbd] %d ranked games (%u KB)  heap=%u\n",
                n, (unsigned)(blen / 1024), (unsigned)ESP.getFreeHeap());
  return true;
}

static uint32_t s_cfbMediaAt = 0;
static void cfbdRefreshMedia() {
  if (s_cfbMediaAt && millis() - s_cfbMediaAt < 20UL * 3600 * 1000) return;
  char path[104];
  snprintf(path, sizeof(path),
           "/games/media?year=%d&week=%d&seasonType=%s&classification=fbs",
           s_cfbYear, s_cfbWeek, s_cfbType);
  size_t blen = 0;
  char* body = cfbdGet(path, blen);
  if (!body) return;
  SpiRamAllocator alloc;
  JsonDocument filter;
  filter[0]["homeTeam"] = true;
  filter[0]["awayTeam"] = true;
  filter[0]["mediaType"] = true;
  filter[0]["outlet"] = true;
  JsonDocument doc(&alloc);
  if (deserializeJson(doc, body, blen, DeserializationOption::Filter(filter),
                      DeserializationOption::NestingLimit(8))) { heap_caps_free(body); return; }
  heap_caps_free(body);
  int set = 0;
  for (JsonObject m : doc.as<JsonArray>()) {
    if (strcmp(m["mediaType"] | "", "tv")) continue;
    const char* hs = m["homeTeam"] | "";
    const char* as = m["awayTeam"] | "";
    if (!cfbdRankOf(hs) && !cfbdRankOf(as)) continue;   // only games that show in the list
    char aw[6], hm[6];
    cfbdAbbr(as, aw, sizeof(aw));
    cfbdAbbr(hs, hm, sizeof(hm));
    tvSet(aw, hm, m["outlet"] | ""); set++;
  }
  s_cfbMediaAt = millis();
  Serial.printf("[tv] CFB: %d networks (%u KB)\n", set, (unsigned)(blen / 1024));
}

static bool cfbd_poll_college() {
  int yr = cfbdSeasonYear();
  if (yr != s_cfbYear || s_cfbWeek == 0 || millis() - s_calAt > 12UL * 3600 * 1000) {
    s_cfbYear = yr;
    cfbdRefreshCalendar();
    s_calAt = millis();
  }
  if (s_rankN == 0 || millis() - s_rankAt > 60UL * 60 * 1000) {
    if (cfbdRefreshRanks()) s_rankAt = millis();
  }
  if (s_cfbWeek == 0) return false;
  cfbdRefreshMedia();
  return cfbdFetchGames();
}

// One team's season, from CFBD, into the schedule store.
static void cfbdFetchSchedule(const char* teamName) {
  char path[128];
  snprintf(path, sizeof(path), "/games?year=%d&seasonType=both&team=%s",
           cfbdSeasonYear(), teamName);
  // spaces -> %20
  for (char* p = path; *p; p++) if (*p == ' ') *p = '+';

  size_t blen = 0;
  char* body = cfbdGet(path, blen);
  if (!body) { schedule_fail(); return; }

  SpiRamAllocator alloc;
  JsonDocument filter;
  filter[0]["startDate"] = true;   filter[0]["week"] = true;
  filter[0]["seasonType"] = true;  filter[0]["completed"] = true;
  filter[0]["homeTeam"] = true;    filter[0]["homePoints"] = true;
  filter[0]["awayTeam"] = true;    filter[0]["awayPoints"] = true;
  JsonDocument doc(&alloc);
  DeserializationError err = deserializeJson(doc, body, blen,
      DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(10));
  heap_caps_free(body);
  if (err) { schedule_fail(); return; }

  SchedGame* sg = s_schBufA;
  int n = 0, wins = 0, losses = 0;
  for (JsonObject ev : doc.as<JsonArray>()) {
    if (n >= MAX_SCHED) break;
    const char* hs = ev["homeTeam"] | "";
    const char* as = ev["awayTeam"] | "";
    bool meHome = !strcmp(hs, teamName);
    const char* opp = meHome ? as : hs;
    if (!*opp) continue;

    SchedGame g = {};
    g.date = parseIso(ev["startDate"] | "");
    g.home = meHome;
    int wk = ev["week"] | 0;
    const char* ty = ev["seasonType"] | "regular";
    g.week = (uint8_t)constrain(wk, 0, 255);
    g.reg  = !strcmp(ty, "regular");
    if (!g.reg)          strlcpy(g.weekLabel, "Bowl", sizeof(g.weekLabel));
    else if (wk > 0)     snprintf(g.weekLabel, sizeof(g.weekLabel), "Wk %d", wk);

    bool done = ev["completed"] | false;
    bool hasPts = !ev["homePoints"].isNull();
    g.state = done ? 2 : 0;
    int hp = ev["homePoints"] | 0, ap = ev["awayPoints"] | 0;
    int mine = meHome ? hp : ap, theirs = meHome ? ap : hp;
    g.teamScore = (uint8_t)constrain(hasPts ? mine : 0, 0, 255);
    g.oppScore  = (uint8_t)constrain(hasPts ? theirs : 0, 0, 255);
    g.win = done && mine > theirs;
    if (done) { if (mine > theirs) wins++; else losses++; }
    cfbdAbbr(opp, g.opp, sizeof(g.opp));
    strlcpy(g.oppName, opp, sizeof(g.oppName));
    sg[n++] = g;
  }

  int firstUp = n > 0 ? n - 1 : 0;
  for (int i = 0; i < n; i++) if (sg[i].state != 2) { firstUp = i; break; }
  char rec[16]; snprintf(rec, sizeof(rec), "%d-%d", wins, losses);
  schedule_publish(sg, n, firstUp, teamName, rec);
  Serial.printf("[cfbd] sched %s: %d games\n", teamName, n);
}

// ===========================================================================
// NFL via balldontlie.io (free key, 5 req/min). Whole week in one request.
// Same data class as CFBD: score, quarter, live/final/upcoming, venue -- no
// play-by-play / down&distance on the free tier.
// ===========================================================================
static bool bdl_available() {
  const char* k = BDL_KEY;
  return k[0] && strcmp(k, "your-bdl-key") != 0;
}

static bool bdl_poll_nfl() {
  time_t now = time(nullptr);
  struct tm lt; localtime_r(&now, &lt);
  int yr = lt.tm_year + 1900;
  if (lt.tm_mon == 0) yr--;                          // January = tail of last season
  time_t seasonStart = utc_to_epoch(yr, 9, 2, 0, 0, 0);
  int wk = (int)((now - seasonStart) / (7 * 86400)) + 1;
  wk = wk < 1 ? 1 : (wk > 22 ? 22 : wk);

  char url[224];
  snprintf(url, sizeof(url),
           "https://api.balldontlie.io/nfl/v1/games?seasons%%5B%%5D=%d"
           "&weeks%%5B%%5D=%d&weeks%%5B%%5D=%d"
           "&season_types%%5B%%5D=2&season_types%%5B%%5D=3&per_page=64",
           yr, wk, wk > 1 ? wk - 1 : wk);

  int code = 0;
  size_t blen = 0;
  char* body = httpGetBody(url, blen, &code, BDL_KEY);
  g_httpStatus[0] = code;
  if (!body) return false;

  SpiRamAllocator alloc;
  JsonDocument filter;
  JsonObject f = filter["data"][0].to<JsonObject>();
  f["id"] = true;              f["date"] = true;      f["week"] = true;
  f["status"] = true;          f["status_state"] = true;
  f["home_team_score"] = true; f["visitor_team_score"] = true;
  f["home_team_q1"] = true; f["home_team_q2"] = true;
  f["home_team_q3"] = true; f["home_team_q4"] = true;
  f["visitor_team_q1"] = true; f["visitor_team_q2"] = true;
  f["visitor_team_q3"] = true; f["visitor_team_q4"] = true;
  f["venue"] = true;
  for (const char* side : {"home_team", "visitor_team"}) {
    JsonObject t = f[side].to<JsonObject>();
    t["id"] = true; t["abbreviation"] = true; t["name"] = true;
    t["full_name"] = true; t["location"] = true;
  }
  JsonDocument doc(&alloc);
  DeserializationError err = deserializeJson(doc, body, blen,
      DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(12));
  heap_caps_free(body);
  if (err) { Serial.printf("[bdl] JSON error: %s\n", err.c_str()); return false; }

  nflRefreshScoreboard(false);                       // daily: TV map + id map source

  Game* tmp = s_gameBuf;
  int n = 0;
  for (JsonObject ev : doc["data"].as<JsonArray>()) {
    if (n >= MAX_NFL) break;
    JsonObject ht = ev["home_team"], vt = ev["visitor_team"];
    const char* ha = ht["abbreviation"] | "";
    const char* va = vt["abbreviation"] | "";
    if (!*ha || !*va) continue;

    Game g = {};
    g.league = 0;
    snprintf(g.id, sizeof(g.id), "%ld", (long)(ev["id"] | 0));
    g.kickoff = parseIso(ev["date"] | "");
    if (g.kickoff && (g.kickoff < now - 6L * 86400 || g.kickoff > now + 11L * 86400)) continue;

    const char* ss = ev["status_state"] | "";
    if (!strcmp(ss, "final"))            g.state = 2;
    else if (!strcmp(ss, "in_progress")) g.state = 1;
    else                                 g.state = 0;    // scheduled / postponed

    bool hasS = !ev["home_team_score"].isNull();
    g.homeScore = (uint8_t)constrain(hasS ? (int)(ev["home_team_score"] | 0) : 0, 0, 255);
    g.awayScore = (uint8_t)constrain(hasS ? (int)(ev["visitor_team_score"] | 0) : 0, 0, 255);

    if (g.state == 1) {
      const char* st = ev["status"] | "";
      if (strstr(st, "Half") || strstr(st, "HALF") || !strcmp(st, "HT")) {
        g.period = 2; strlcpy(g.detail, "Halftime", sizeof(g.detail));
      } else {
        int q = 0;
        for (int i = 4; i >= 1; i--) {
          char k[16]; snprintf(k, sizeof(k), "home_team_q%d", i);
          char k2[16]; snprintf(k2, sizeof(k2), "visitor_team_q%d", i);
          if (!ev[k].isNull() || !ev[k2].isNull()) { q = i; break; }
        }
        g.period = q ? q : 1;
        if (strchr(st, ':')) {                       // status carries a clock
          strlcpy(g.clock, st, sizeof(g.clock));
          char* sp = strchr(g.clock, ' '); if (sp) *sp = 0;
        }
      }
    }

    strlcpy(g.home, ha, sizeof(g.home));
    strlcpy(g.away, va, sizeof(g.away));
    strlcpy(g.homeName, ht["name"] | ha, sizeof(g.homeName));
    strlcpy(g.awayName, vt["name"] | va, sizeof(g.awayName));
    char fn[40];
    snprintf(fn, sizeof(fn), "%s", ht["full_name"] | "");
    if (!*fn) snprintf(fn, sizeof(fn), "%s %s", ht["location"] | "", ht["name"] | "");
    g.homeColor = nflColor(fn);
    snprintf(fn, sizeof(fn), "%s", vt["full_name"] | "");
    if (!*fn) snprintf(fn, sizeof(fn), "%s %s", vt["location"] | "", vt["name"] | "");
    g.awayColor = nflColor(fn);
    snprintf(g.homeId, sizeof(g.homeId), "%ld", (long)(ht["id"] | 0));
    snprintf(g.awayId, sizeof(g.awayId), "%ld", (long)(vt["id"] | 0));
    strlcpy(g.venue, ev["venue"] | "", sizeof(g.venue));
    { const char* _t = tv_get(g.away, g.home); if (*_t) strlcpy(g.tv, _t, sizeof(g.tv)); }
    g.toHome = g.toAway = 3;

    tmp[n++] = g;
  }

  games_publish(0, tmp, n);
  Serial.printf("[bdl] NFL: %d games (%u KB)  heap=%u\n",
                n, (unsigned)(blen / 1024), (unsigned)ESP.getFreeHeap());
  return true;
}

static void bdlFetchSchedule(const char* teamId, const char* teamName) {
  if (!teamId || !*teamId) { schedule_fail(); return; }
  time_t now = time(nullptr);
  struct tm lt; localtime_r(&now, &lt);
  int yr = lt.tm_year + 1900;
  if (lt.tm_mon == 0) yr--;

  char url[192];
  snprintf(url, sizeof(url),
           "https://api.balldontlie.io/nfl/v1/games?seasons%%5B%%5D=%d&team_ids%%5B%%5D=%s"
           "&season_types%%5B%%5D=2&season_types%%5B%%5D=3&per_page=40", yr, teamId);
  size_t blen = 0;
  char* body = httpGetBody(url, blen, nullptr, BDL_KEY);
  if (!body) { schedule_fail(); return; }

  SpiRamAllocator alloc;
  JsonDocument filter;
  JsonObject f = filter["data"][0].to<JsonObject>();
  f["date"] = true; f["week"] = true; f["postseason"] = true; f["status_state"] = true;
  f["home_team_score"] = true; f["visitor_team_score"] = true;
  for (const char* s : {"home_team", "visitor_team"}) {
    JsonObject t = f[s].to<JsonObject>();
    t["id"] = true; t["abbreviation"] = true; t["name"] = true;
  }
  JsonDocument doc(&alloc);
  if (deserializeJson(doc, body, blen, DeserializationOption::Filter(filter),
                      DeserializationOption::NestingLimit(12))) {
    heap_caps_free(body); schedule_fail(); return;
  }
  heap_caps_free(body);

  SchedGame* sg = s_schBufA;
  int n = 0, wins = 0, losses = 0;
  long tid = atol(teamId);
  for (JsonObject ev : doc["data"].as<JsonArray>()) {
    if (n >= MAX_SCHED) break;
    JsonObject ht = ev["home_team"], vt = ev["visitor_team"];
    bool meHome = (long)(ht["id"] | 0) == tid;
    JsonObject opp = meHome ? vt : ht;

    SchedGame g = {};
    g.date = parseIso(ev["date"] | "");
    g.home = meHome;
    int wk = ev["week"] | 0;
    bool post = ev["postseason"] | false;
    g.week = (uint8_t)constrain(wk, 0, 255);
    g.reg = !post;
    if (post)        strlcpy(g.weekLabel, "Playoffs", sizeof(g.weekLabel));
    else if (wk > 0) snprintf(g.weekLabel, sizeof(g.weekLabel), "Wk %d", wk);

    bool done = !strcmp(ev["status_state"] | "", "final");
    g.state = done ? 2 : 0;
    bool hasS = !ev["home_team_score"].isNull();
    int hp = ev["home_team_score"] | 0, ap = ev["visitor_team_score"] | 0;
    int mine = meHome ? hp : ap, theirs = meHome ? ap : hp;
    g.teamScore = (uint8_t)constrain(hasS ? mine : 0, 0, 255);
    g.oppScore  = (uint8_t)constrain(hasS ? theirs : 0, 0, 255);
    g.win = done && mine > theirs;
    if (done) { if (mine > theirs) wins++; else losses++; }
    strlcpy(g.opp, opp["abbreviation"] | "", sizeof(g.opp));
    strlcpy(g.oppName, opp["name"] | "", sizeof(g.oppName));
    sg[n++] = g;
  }
  if (n == 0) { schedule_fail(); return; }

  std::sort(sg, sg + n, [](const SchedGame& a, const SchedGame& b) { return a.date < b.date; });
  SchedGame* out = s_schBufB;
  int m = 0;
  for (int i = 0; i < n && m < MAX_SCHED; i++) {
    out[m++] = sg[i];
    if (i + 1 < n && m < MAX_SCHED && sg[i].reg && sg[i + 1].reg &&
        sg[i].week >= 1 && sg[i + 1].week == sg[i].week + 2) {
      SchedGame b = {};
      b.bye = true; b.reg = true; b.week = sg[i].week + 1;
      b.date = sg[i].date ? sg[i].date + 7L * 86400 : 0;
      snprintf(b.weekLabel, sizeof(b.weekLabel), "Wk %u", b.week);
      out[m++] = b;
    }
  }
  int firstUp = m > 0 ? m - 1 : 0;
  for (int i = 0; i < m; i++) if (out[i].state != 2) { firstUp = i; break; }
  char rec[12]; snprintf(rec, sizeof(rec), "%d-%d", wins, losses);
  schedule_publish(out, m, firstUp, teamName, rec);
  Serial.printf("[bdl] sched %s: %d games\n", teamName, m);
}

// ===========================================================================
// NFL via TheSportsDB (free key "3"). ESPN's NFL scoreboard is Akamai-fronted
// and blocks our client; this feed is ~3-10 KB and far more permissive. No live
// game clock on the free tier, so a live game shows score + quarter.
// ===========================================================================
static uint8_t tsdbState(const char* st, time_t kickoff, char* detail, size_t dcap,
                         uint8_t* period) {
  *period = 0;
  if (!st) st = "";
  if (strstr(st, "Finished") || !strcmp(st, "FT") ||
      strstr(st, "After Over") || !strcmp(st, "AOT")) return 2;
  if (strstr(st, "Postp") || strstr(st, "Cancel") || strstr(st, "Abandoned")) return 0;
  bool ns = !*st || !strcmp(st, "NS") || strstr(st, "Not Started");
  time_t now = time(nullptr);
  if (ns) return (kickoff && now >= kickoff + 300) ? 1 : 0;   // status lagging kickoff

  // in progress
  if (strstr(st, "Half") || !strcmp(st, "HT")) { *period = 2; strlcpy(detail, "Halftime", dcap); }
  else if (strstr(st, "Over") || !strcmp(st, "OT")) *period = 5;
  else if (strchr(st, '4')) *period = 4;
  else if (strchr(st, '3')) *period = 3;
  else if (strchr(st, '2')) *period = 2;
  else if (strchr(st, '1')) *period = 1;
  else *period = 1;
  return 1;
}

static bool tsdbEvents(const char* path, const char* arrKey, Game* buf, int& n, int cap) {
  char url[128];
  snprintf(url, sizeof(url), TSDB_BASE "%s", path);
  size_t blen = 0;
  char* body = httpGetBody(url, blen);   // leave g_httpStatus[0] = ESPN's 403
  if (!body) return false;

  SpiRamAllocator alloc;
  JsonDocument filter;
  filter[arrKey][0]["idEvent"] = true;
  filter[arrKey][0]["strTimestamp"] = true;
  filter[arrKey][0]["strHomeTeam"] = true;
  filter[arrKey][0]["strAwayTeam"] = true;
  filter[arrKey][0]["intHomeScore"] = true;
  filter[arrKey][0]["intAwayScore"] = true;
  filter[arrKey][0]["strStatus"] = true;
  filter[arrKey][0]["strVenue"] = true;
  filter[arrKey][0]["idHomeTeam"] = true;
  filter[arrKey][0]["idAwayTeam"] = true;
  JsonDocument doc(&alloc);
  DeserializationError err = deserializeJson(doc, body, blen,
      DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(12));
  heap_caps_free(body);
  if (err) { Serial.printf("[tsdb] JSON error: %s\n", err.c_str()); return false; }

  JsonArray arr = doc[arrKey].as<JsonArray>();
  if (arr.isNull()) return true;                 // valid but empty
  time_t now = time(nullptr);
  for (JsonObject ev : arr) {
    if (n >= cap) break;
    const char* hs = ev["strHomeTeam"] | "";
    const char* as = ev["strAwayTeam"] | "";
    const char* id = ev["idEvent"] | "";
    if (!*hs || !*as || !*id) continue;
    bool dup = false;
    for (int i = 0; i < n; i++) if (!strcmp(buf[i].id, id)) { dup = true; break; }
    if (dup) continue;

    Game g = {};
    g.league = 0;
    strlcpy(g.id, id, sizeof(g.id));
    g.kickoff = parseIso(ev["strTimestamp"] | "");
    if (g.kickoff && (g.kickoff < now - 6L * 86400 || g.kickoff > now + 11L * 86400)) continue;

    g.state = tsdbState(ev["strStatus"] | "", g.kickoff, g.detail, sizeof(g.detail), &g.period);
    bool hasScore = !ev["intHomeScore"].isNull();
    g.homeScore = (uint8_t)constrain(hasScore ? ev["intHomeScore"].as<int>() : 0, 0, 255);
    g.awayScore = (uint8_t)constrain(hasScore ? ev["intAwayScore"].as<int>() : 0, 0, 255);

    nflNick(hs, g.homeName, sizeof(g.homeName));
    nflNick(as, g.awayName, sizeof(g.awayName));
    nflAbbr(hs, g.home, sizeof(g.home));
    nflAbbr(as, g.away, sizeof(g.away));
    g.homeColor = nflColor(hs);
    g.awayColor = nflColor(as);
    strlcpy(g.homeId, ev["idHomeTeam"] | "", sizeof(g.homeId));
    strlcpy(g.awayId, ev["idAwayTeam"] | "", sizeof(g.awayId));
    strlcpy(g.venue, ev["strVenue"] | "", sizeof(g.venue));
    g.toHome = g.toAway = 3;

    buf[n++] = g;
  }
  return true;
}

static bool tsdb_poll_nfl() {
  Game* buf = s_gameBuf;
  int n = 0;
  bool a = tsdbEvents("eventsnextleague.php?id=" TSDB_NFL_ID, "events", buf, n, MAX_NFL);
  vTaskDelay(pdMS_TO_TICKS(400));
  bool b = tsdbEvents("eventspastleague.php?id=" TSDB_NFL_ID, "events", buf, n, MAX_NFL);
  if (!a && !b) return false;

  games_merge(0, buf, n);            // patch scores into the saved slate, don't replace it
  Serial.printf("[tsdb] NFL fallback: %d games  heap=%u\n", n, (unsigned)ESP.getFreeHeap());
  return true;
}

// ---------------------------------------------------------------------------
// Wi-Fi + time
// ---------------------------------------------------------------------------
static void wifiConnect() {
  if (WiFi.status() == WL_CONNECTED) { g_wifiConnected = true; return; }
  if (!g_wifiSsid[0]) { g_wifiConnected = false; return; }   // wizard not done yet

  snprintf(g_bootMsg, sizeof(g_bootMsg), "Joining %s", g_wifiSsid);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(g_wifiSsid, g_wifiPass);

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) delay(250);

  g_wifiConnected = (WiFi.status() == WL_CONNECTED);
  if (g_wifiConnected)
    snprintf(g_bootMsg, sizeof(g_bootMsg), "Wi-Fi: %s", WiFi.localIP().toString().c_str());
  else
    snprintf(g_bootMsg, sizeof(g_bootMsg), "Wi-Fi failed - retrying");
}

// One network scan, then a join attempt if the wizard asked for one. Both are
// user-driven and rare, so it's fine to block the poll loop here.
static void serviceWifiSetup() {
  if (wifi_scan_take_request()) {
    WiFi.mode(WIFI_STA);
    int n = WiFi.scanNetworks(false, true);           // sync, include hidden
    WifiNet out[WIFI_SCAN_MAX];
    int m = 0;
    for (int i = 0; i < n && m < WIFI_SCAN_MAX; i++) {
      const String& s = WiFi.SSID(i);
      if (!s.length()) continue;
      bool dup = false;
      for (int k = 0; k < m; k++) if (s == out[k].ssid) { dup = true; break; }
      if (dup) continue;
      strlcpy(out[m].ssid, s.c_str(), sizeof(out[m].ssid));
      out[m].rssi = (int8_t)WiFi.RSSI(i);
      out[m].lock = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
      m++;
    }
    WiFi.scanDelete();
    wifi_scan_publish(out, m);
    Serial.printf("[wifi] scan: %d networks\n", m);
  }

  char ssid[33], pass[65];
  if (wifi_join_take_request(ssid, pass)) {
    Serial.printf("[wifi] joining '%s'\n", ssid);
    WiFi.disconnect(true);
    delay(200);
    strlcpy(g_wifiSsid, ssid, sizeof(g_wifiSsid));
    strlcpy(g_wifiPass, pass, sizeof(g_wifiPass));
    WiFi.mode(WIFI_STA);
    WiFi.begin(g_wifiSsid, g_wifiPass);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) delay(200);
    bool ok = WiFi.status() == WL_CONNECTED;
    g_wifiConnected = ok;
    g_wifiJoinState = ok ? WJOIN_OK : WJOIN_FAIL;
    g_timeSynced = false;                              // re-sync clock on the new link
    Serial.printf("[wifi] join %s\n", ok ? "OK" : "FAILED");
  }
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
// Route a league to its data source. NFL = ESPN scoreboard; college = CFBD
// (falls back to the ESPN college scoreboard if no CFBD key is set).
static bool pollLeague(int lg) {
  if (lg == 1 && cfbd_available()) return cfbd_poll_college();
  if (lg == 0 && bdl_available()) return bdl_poll_nfl();
  bool ok = fetchLeague(lg);
  // ESPN blocked us (403): best-effort side-pull of the marquee games from
  // TheSportsDB, merged into the saved slate. `ok` stays false so the 30-min
  // 403 backoff still runs and the block gets a chance to lapse.
  if (!ok && lg == 0 && g_httpStatus[0] == 403) tsdb_poll_nfl();
  return ok;
}

static void serviceOnDemand() {
  serviceSchedule();
  serviceNews();
  serviceStats();
  serviceDetailFocus();          // rate-limited internally
}

static void netTask(void*) {
  // Long CPU bursts (TLS handshake, JSON parse) on core 0; drop the idle-0
  // watchdog. The fetch loop has its own stall timeouts.
  disableCore0WDT();

  uint32_t otherAt = 0;
  uint8_t  failStreak = 0;

  for (;;) {
    if (g_sleepReq) { vTaskDelay(pdMS_TO_TICKS(200)); continue; }

    serviceWifiSetup();                 // on-screen scan / join, if requested
    if (!g_wifiSsid[0]) {               // wizard still needs credentials
      g_firstCycleDone = true;
      vTaskDelay(pdMS_TO_TICKS(400));
      continue;
    }

    wifiConnect();
    if (g_wifiConnected && !g_timeSynced) ntpSync();
    serviceOnDemand();

    if (g_wifiConnected) {
      // Only fetch scores when the user is actually looking at them (list or a
      // game detail). On Settings/News/Schedule/Stats, or when idling, just
      // service on-demand requests. The first cycle always runs so there's data.
      bool wantScores = g_scoresOnScreen || !g_firstCycleDone;
      uint32_t wait;

      if (wantScores) {
        int active = g_uiLeague ? 1 : 0;
        snprintf(g_bootMsg, sizeof(g_bootMsg), "Loading scores");
        bool ok = pollLeague(active);
        g_firstCycleDone = true;

        // keep the off-screen league warm so switching tabs isn't blank
        if (millis() - otherAt > POLL_OTHER_MS) {
          vTaskDelay(pdMS_TO_TICKS(1200));
          serviceOnDemand();
          pollLeague(active ^ 1);
          otherAt = millis();
        }

        if (ok) failStreak = 0;
        else if (failStreak < 3) failStreak++;

        bool blocked = !ok && (g_httpStatus[active] == 403 || g_httpStatus[active] == 429);
        if (blocked)         wait = 1800000UL;                        // 30 min — let the block lapse
        else if (!ok)        wait = POLL_RETRY_MS << (failStreak - 1); // 15s, 30s, 60s
        else if (g_anyLive)  wait = g_set.liveMs;
        else                 wait = g_set.idleMs;
      } else {
        failStreak = 0;
        wait = g_set.idleMs;
      }

      uint32_t t0 = millis();
      while (millis() - t0 < wait) {
        if (WiFi.status() != WL_CONNECTED) break;
        if (g_forceRefresh) { g_forceRefresh = false; break; }
        if (!wantScores && (g_scoresOnScreen || !g_firstCycleDone)) break;  // back to scores
        serviceWifiSetup();
        serviceOnDemand();
        vTaskDelay(pdMS_TO_TICKS(300));
      }
    } else {
      g_firstCycleDone = true;
      uint32_t t0 = millis();
      while (millis() - t0 < POLL_RETRY_MS) { serviceWifiSetup(); vTaskDelay(pdMS_TO_TICKS(300)); }
    }
  }
}

void net_start() {
  xTaskCreatePinnedToCore(netTask, "net", 16384, nullptr, 1, nullptr, 0);
}
