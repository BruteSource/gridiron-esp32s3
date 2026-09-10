// Host-side check of the ESPN filter + parse logic used in src/net.cpp.
// g++ -std=c++17 -I <ArduinoJson>/src parsetest.cpp -o parsetest
#include <ArduinoJson.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

static void buildFilter(JsonDocument& f) {
  f["events"][0]["id"] = true;
  f["events"][0]["date"] = true;
  f["events"][0]["status"]["period"] = true;
  f["events"][0]["status"]["displayClock"] = true;
  f["events"][0]["status"]["type"]["state"] = true;
  f["events"][0]["status"]["type"]["shortDetail"] = true;
  f["events"][0]["competitions"][0]["competitors"][0]["homeAway"] = true;
  f["events"][0]["competitions"][0]["competitors"][0]["score"] = true;
  f["events"][0]["competitions"][0]["competitors"][0]["curatedRank"]["current"] = true;
  f["events"][0]["competitions"][0]["competitors"][0]["team"]["abbreviation"] = true;
  f["events"][0]["competitions"][0]["competitors"][0]["team"]["shortDisplayName"] = true;
  f["events"][0]["competitions"][0]["competitors"][0]["team"]["displayName"] = true;
  f["events"][0]["competitions"][0]["competitors"][0]["team"]["color"] = true;
  f["events"][0]["competitions"][0]["broadcasts"][0]["names"] = true;
  f["events"][0]["competitions"][0]["geoBroadcasts"][0]["media"]["shortName"] = true;
  f["events"][0]["competitions"][0]["venue"]["fullName"] = true;
}

// Synthetic response shaped like the real ESPN scoreboard (extra junk fields
// included on purpose so the filter has something to strip).
const char* SAMPLE = R"JSON(
{
  "leagues": [{"id":"28","name":"National Football League","season":{"year":2026}}],
  "season": {"type": 2, "year": 2026},
  "week": {"number": 1},
  "events": [
    {
      "id": "401772510",
      "uid": "s:20~l:28~e:401772510",
      "date": "2026-09-10T00:20Z",
      "name": "Dallas Cowboys at Philadelphia Eagles",
      "shortName": "DAL @ PHI",
      "season": {"year": 2026, "type": 2, "slug": "regular-season"},
      "status": {
        "clock": 514.0,
        "displayClock": "8:34",
        "period": 2,
        "type": {"id":"2","name":"STATUS_IN_PROGRESS","state":"in","completed":false,
                 "description":"In Progress","detail":"8:34 - 2nd Quarter","shortDetail":"8:34 - 2nd"}
      },
      "competitions": [
        {
          "id": "401772510",
          "date": "2026-09-10T00:20Z",
          "attendance": 69879,
          "type": {"id":"1","abbreviation":"STD"},
          "timeValid": true,
          "neutralSite": false,
          "competitors": [
            {
              "id": "21", "uid":"s:20~l:28~t:21", "type":"team", "order":0, "homeAway":"home",
              "team": {"id":"21","abbreviation":"PHI","displayName":"Philadelphia Eagles",
                       "shortDisplayName":"Eagles","color":"004c54","alternateColor":"a5acaf",
                       "logo":"https://a.espncdn.com/i/teamlogos/nfl/500/phi.png"},
              "score": "14",
              "curatedRank": {"current": 99},
              "linescores":[{"value":7.0},{"value":7.0}],
              "records":[{"name":"overall","type":"total","summary":"1-0"}]
            },
            {
              "id": "6", "uid":"s:20~l:28~t:6", "type":"team", "order":1, "homeAway":"away",
              "team": {"id":"6","abbreviation":"DAL","displayName":"Dallas Cowboys",
                       "shortDisplayName":"Cowboys","color":"002244","alternateColor":"b0b7bc",
                       "logo":"https://a.espncdn.com/i/teamlogos/nfl/500/dal.png"},
              "score": "10",
              "curatedRank": {"current": 99},
              "linescores":[{"value":3.0},{"value":7.0}],
              "records":[{"name":"overall","type":"total","summary":"0-1"}]
            }
          ],
          "situation": {
            "lastPlay": {"id":"x","text":"Some play"},
            "down": 2, "yardLine": 45, "distance": 7,
            "downDistanceText": "2nd & 7 at DAL 45",
            "possession": "6", "isRedZone": false
          },
          "status": {"displayClock":"8:34","period":2,"type":{"state":"in","shortDetail":"8:34 - 2nd"}},
          "broadcasts": [{"market":"national","names":["NBC"]}],
          "geoBroadcasts": [{"type":{"id":"1","shortName":"TV"},"market":{"id":"1"},
                             "media":{"shortName":"NBC"},"lang":"en","region":"us"}],
          "venue": {"id":"3806","fullName":"Lincoln Financial Field",
                    "address":{"city":"Philadelphia","state":"PA"}}
        }
      ]
    },
    {
      "id": "401772511",
      "date": "2026-09-14T17:00Z",
      "shortName": "KC @ BUF",
      "status": {"displayClock":"0:00","period":0,
                 "type":{"state":"pre","shortDetail":"Sun, September 14th at 1:00 PM EDT"}},
      "competitions": [
        {
          "competitors": [
            {"homeAway":"home","score":"0","curatedRank":{"current":99},
             "team":{"abbreviation":"BUF","displayName":"Buffalo Bills","shortDisplayName":"Bills","color":"00338d"}},
            {"homeAway":"away","score":"0","curatedRank":{"current":99},
             "team":{"abbreviation":"KC","displayName":"Kansas City Chiefs","shortDisplayName":"Chiefs","color":"e31837"}}
          ],
          "broadcasts": [{"market":"national","names":["CBS"]}],
          "venue": {"fullName":"Highmark Stadium"}
        }
      ]
    },
    {
      "id": "401772512",
      "date": "2026-09-07T17:00Z",
      "shortName": "GB @ CHI",
      "status": {"displayClock":"0:00","period":4,
                 "type":{"state":"post","shortDetail":"Final"}},
      "competitions": [
        {
          "competitors": [
            {"homeAway":"home","score":"17","curatedRank":{"current":99},
             "team":{"abbreviation":"CHI","displayName":"Chicago Bears","shortDisplayName":"Bears","color":"0b162a"}},
            {"homeAway":"away","score":"27","curatedRank":{"current":99},
             "team":{"abbreviation":"GB","displayName":"Green Bay Packers","shortDisplayName":"Packers","color":"204e32"}}
          ],
          "venue": {"fullName":"Soldier Field"}
        }
      ]
    }
  ]
}
)JSON";

// CFB sample: one ranked matchup, one unranked-vs-unranked that must be dropped.
const char* CFB_SAMPLE = R"JSON(
{
  "events": [
    {
      "id": "cfb1", "date": "2026-09-13T23:30Z",
      "status": {"displayClock":"0:00","period":0,"type":{"state":"pre","shortDetail":"7:30 PM"}},
      "competitions": [{
        "competitors": [
          {"homeAway":"home","score":"0","curatedRank":{"current":4},
           "team":{"abbreviation":"UGA","shortDisplayName":"Bulldogs","displayName":"Georgia Bulldogs","color":"BA0C2F"}},
          {"homeAway":"away","score":"0","curatedRank":{"current":12},
           "team":{"abbreviation":"TENN","shortDisplayName":"Volunteers","displayName":"Tennessee Volunteers","color":"FF8200"}}
        ],
        "broadcasts": [{"names":["ABC"]}],
        "venue": {"fullName":"Sanford Stadium"}
      }]
    },
    {
      "id": "cfb2", "date": "2026-09-13T20:00Z",
      "status": {"displayClock":"0:00","period":0,"type":{"state":"pre","shortDetail":"3 PM"}},
      "competitions": [{
        "competitors": [
          {"homeAway":"home","score":"0","curatedRank":{"current":99},
           "team":{"abbreviation":"VAN","shortDisplayName":"Commodores","displayName":"Vanderbilt","color":"000000"}},
          {"homeAway":"away","score":"0","curatedRank":{"current":99},
           "team":{"abbreviation":"UNLV","shortDisplayName":"Rebels","displayName":"UNLV","color":"CF0A2C"}}
        ],
        "venue": {"fullName":"FirstBank Stadium"}
      }]
    }
  ]
}
)JSON";

struct Game {
  char id[12], away[6], home[6], awayName[22], homeName[22];
  uint8_t awayScore, homeScore, awayRank, homeRank;
  uint16_t awayColor, homeColor;
  uint8_t state, period;
  char clock[8], detail[36], tv[14], venue[42];
  long kickoff; uint8_t league;
};

static uint16_t colFromHex(const char* hex, uint16_t fb) {
  if (!hex || strlen(hex) < 6) return fb;
  uint32_t v = strtoul(hex, nullptr, 16);
  uint8_t r=(v>>16)&0xFF,g=(v>>8)&0xFF,b=v&0xFF;
  return (uint16_t)(((r&0xF8)<<8)|((g&0xFC)<<3)|(b>>3));
}

int parse(const char* json, int league) {
  JsonDocument filter; buildFilter(filter);
  JsonDocument doc;
  auto err = deserializeJson(doc, json, DeserializationOption::Filter(filter),
                             DeserializationOption::NestingLimit(24));
  if (err) { printf("  ERR %s\n", err.c_str()); return -1; }
  int n = 0;
  for (JsonObject ev : doc["events"].as<JsonArray>()) {
    JsonObject comp = ev["competitions"][0];
    JsonArray cs = comp["competitors"].as<JsonArray>();
    if (cs.size() < 2) continue;
    Game g{}; g.league = league;
    strncpy(g.id, ev["id"] | "", sizeof(g.id)-1);
    const char* st = ev["status"]["type"]["state"] | "pre";
    g.state = !strcmp(st,"in")?1:(!strcmp(st,"post")?2:0);
    g.period = ev["status"]["period"] | 0;
    strncpy(g.clock, ev["status"]["displayClock"] | "", sizeof(g.clock)-1);
    strncpy(g.detail, ev["status"]["type"]["shortDetail"] | "", sizeof(g.detail)-1);
    for (JsonObject c : cs) {
      bool home = !strcmp(c["homeAway"] | "", "home");
      const char* ab = c["team"]["abbreviation"] | "";
      const char* nm = c["team"]["shortDisplayName"] | "";
      if (!*nm) nm = c["team"]["displayName"] | "";
      int sc = c["score"].as<int>();
      int cr = c["curatedRank"]["current"] | 99;
      uint8_t rank = (cr>=1&&cr<=25)?(uint8_t)cr:0;
      uint16_t col = colFromHex(c["team"]["color"] | "", 0x8410);
      if (home){ strncpy(g.home,ab,5); strncpy(g.homeName,nm,21); g.homeScore=sc; g.homeRank=rank; g.homeColor=col; }
      else     { strncpy(g.away,ab,5); strncpy(g.awayName,nm,21); g.awayScore=sc; g.awayRank=rank; g.awayColor=col; }
    }
    const char* tv = comp["broadcasts"][0]["names"][0] | "";
    if (!*tv) tv = comp["geoBroadcasts"][0]["media"]["shortName"] | "";
    strncpy(g.tv, tv, sizeof(g.tv)-1);
    strncpy(g.venue, comp["venue"]["fullName"] | "", sizeof(g.venue)-1);
    if (league == 1 && g.homeRank == 0 && g.awayRank == 0) { printf("  drop unranked %s\n", g.id); continue; }
    const char* ss = g.state==0?"PRE":g.state==1?"LIVE":"FINAL";
    printf("  [%s] %s%s %s(%u) @ %s%s %s(%u)  %s Q%u %s  tv=%s venue=%s colors=%04x/%04x\n",
      ss, g.awayRank?"#":"", g.awayRank?std::to_string(g.awayRank).c_str():"",
      g.away, g.awayScore,
      g.homeRank?"#":"", g.homeRank?std::to_string(g.homeRank).c_str():"",
      g.home, g.homeScore, g.detail, g.period, g.clock, g.tv, g.venue,
      g.awayColor, g.homeColor);
    n++;
  }
  printf("  -> %d games, doc mem = %u bytes\n", n, (unsigned)doc.memoryUsage());
  return n;
}

int main() {
  printf("NFL:\n");  parse(SAMPLE, 0);
  printf("CFB (Top 25 filter):\n"); parse(CFB_SAMPLE, 1);
  return 0;
}
