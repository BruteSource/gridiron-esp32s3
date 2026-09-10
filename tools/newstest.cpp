#include <ArduinoJson.h>
#include <cstdio>
#include <cstring>

static void buildNewsFilter(JsonDocument& f) {
  f["articles"][0]["headline"] = true;
  f["articles"][0]["description"] = true;
  f["articles"][0]["published"] = true;
  f["articles"][0]["byline"] = true;
  f["articles"][0]["type"] = true;
}

static void sanitizeText(char* s) {
  static const struct { const char* ent; char ch; } E[] = {
    {"&amp;", '&'},  {"&#38;", '&'},
    {"&#39;", '\''}, {"&apos;", '\''}, {"&rsquo;", '\''}, {"&lsquo;", '\''},
    {"&quot;", '"'}, {"&ldquo;", '"'}, {"&rdquo;", '"'},
    {"&nbsp;", ' '}, {"&mdash;", '-'}, {"&ndash;", '-'}, {"&#8217;", '\''},
  };
  char* r = s; char* w = s;
  while (*r) {
    if (*r == '<') { while (*r && *r != '>') r++; if (*r) r++; continue; }
    if (*r == '&') {
      bool hit = false;
      for (auto& e : E) { size_t l = strlen(e.ent);
        if (!strncmp(r, e.ent, l)) { *w++ = e.ch; r += l; hit = true; break; } }
      if (hit) continue;
      char* semi = strchr(r, ';');
      if (semi && semi - r <= 8) { *w++ = '?'; r = semi + 1; continue; }
    }
    *w++ = *r++;
  }
  *w = 0;
}

const char* S = R"JSON(
{
  "header": "NFL News",
  "articles": [
    {
      "type": "HeadlineNews",
      "headline": "Cowboys &amp; Eagles set for <b>Week 1</b> clash",
      "description": "Dallas and Philadelphia will kick off the season Sunday. &ldquo;We&#39;re ready,&rdquo; said the coach &mdash; and it shows.",
      "published": "2026-09-09T13:05:00Z",
      "byline": "Adam Schefter",
      "links": {"web": {"href": "https://espn.com/x"}}
    },
    {
      "type": "Media",
      "headline": "Highlights: preseason finale",
      "description": "",
      "published": "2026-09-08T20:00:00Z"
    },
    {
      "type": "Story",
      "headline": "Injury report: 3 starters questionable",
      "description": "Coach says decisions come Friday. Weather &amp; field conditions a factor.",
      "published": "2026-09-09T11:30:00Z",
      "byline": ""
    }
  ]
}
)JSON";

int main() {
  JsonDocument filter; buildNewsFilter(filter);
  JsonDocument doc;
  auto err = deserializeJson(doc, S, DeserializationOption::Filter(filter),
                             DeserializationOption::NestingLimit(16));
  if (err) { printf("ERR %s\n", err.c_str()); return 1; }
  int n = 0;
  for (JsonObject a : doc["articles"].as<JsonArray>()) {
    const char* hl = a["headline"] | "";
    if (!*hl) continue;
    char headline[112], desc[300], byline[28];
    strncpy(headline, hl, 111); headline[111]=0;
    strncpy(desc, a["description"] | "", 299); desc[299]=0;
    strncpy(byline, a["byline"] | "", 27); byline[27]=0;
    sanitizeText(headline); sanitizeText(desc); sanitizeText(byline);
    printf("[%d] %s\n", n, headline);
    printf("    by=%s pub=%s\n", byline[0]?byline:"(none)", (const char*)(a["published"]|""));
    printf("    %s\n", desc[0]?desc:"(no summary)");
    n++;
  }
  printf("-> %d items\n", n);
}
