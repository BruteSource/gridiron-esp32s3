#include <ArduinoJson.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>

static void buildSchedFilter(JsonDocument& f) {
  f["team"]["recordSummary"] = true;
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
  f["events"][0]["competitions"][0]["status"]["type"]["state"] = true;
  f["events"][0]["competitions"][0]["broadcasts"][0]["media"]["shortName"] = true;
}
static int scoreOf(JsonObject c){int v=c["score"]["value"].as<int>(); if(!v)v=c["score"].as<int>(); return v;}
static void normalizeTv(char* tv){ if(strstr(tv,"Prime")||strstr(tv,"Amazon"))strcpy(tv,"Prime"); }

struct SG { char opp[6]; bool home; uint8_t state; bool win; uint8_t ts,os;
            char wl[12]; uint8_t week; bool reg; bool bye; long date; char tv[12]; };

const char* S = R"JSON(
{ "team": { "id":"12","recordSummary":"2-0" },
  "events": [
    { "date":"2026-09-06T00:20Z","week":{"number":1},"seasonType":{"type":2,"name":"Regular Season"},
      "competitions":[{"competitors":[
        {"homeAway":"away","winner":true,"team":{"id":"12","abbreviation":"KC"},"score":{"value":27}},
        {"homeAway":"home","winner":false,"team":{"id":"24","abbreviation":"LAC"},"score":{"value":21}}],
        "status":{"type":{"state":"post"}},"broadcasts":[{"media":{"shortName":"NBC"}}]}]},
    { "date":"2026-09-13T17:00Z","week":{"number":2},"seasonType":{"type":2,"name":"Regular Season"},
      "competitions":[{"competitors":[
        {"homeAway":"away","team":{"id":"21","abbreviation":"PHI"},"score":{"value":0}},
        {"homeAway":"home","team":{"id":"12","abbreviation":"KC"},"score":{"value":0}}],
        "status":{"type":{"state":"pre"}},"broadcasts":[{"media":{"shortName":"Amazon Prime Video"}}]}]},
    { "date":"2026-09-27T17:00Z","week":{"number":4},"seasonType":{"type":2,"name":"Regular Season"},
      "competitions":[{"competitors":[
        {"homeAway":"away","team":{"id":"12","abbreviation":"KC"},"score":{"value":0}},
        {"homeAway":"home","team":{"id":"7","abbreviation":"DEN"},"score":{"value":0}}],
        "status":{"type":{"state":"pre"}},"broadcasts":[{"media":{"shortName":"CBS"}}]}]},
    { "date":"2027-01-18T21:00Z","week":{"number":1},"seasonType":{"type":3,"name":"Postseason"},
      "competitions":[{"competitors":[
        {"homeAway":"home","team":{"id":"12","abbreviation":"KC"},"score":{"value":0}},
        {"homeAway":"away","team":{"id":"33","abbreviation":"BAL"},"score":{"value":0}}],
        "status":{"type":{"state":"pre"}},"broadcasts":[{"media":{"shortName":"CBS"}}]}]}
  ] }
)JSON";

int main(){
  JsonDocument filter; buildSchedFilter(filter);
  JsonDocument doc;
  auto err=deserializeJson(doc,S,DeserializationOption::Filter(filter),DeserializationOption::NestingLimit(24));
  if(err){printf("ERR %s\n",err.c_str());return 1;}
  const char* teamId="12"; int league=0;
  SG sg[32]; int n=0;
  for(JsonObject ev: doc["events"].as<JsonArray>()){
    JsonObject comp=ev["competitions"][0];
    JsonArray cs=comp["competitors"].as<JsonArray>();
    if(cs.size()<2) continue;
    SG g={}; g.date=0;
    int wn=ev["week"]["number"]|0; int sty=ev["seasonType"]["type"]|0;
    const char* stn=ev["seasonType"]["name"]|"";
    g.week=wn; g.reg=(sty==2)||strstr(stn,"Regular");
    if(strstr(stn,"Post")||strstr(stn,"Bowl")) strcpy(g.wl,"Bowl");
    else if(wn>0) snprintf(g.wl,sizeof g.wl,"Wk %d",wn);
    const char* stt=comp["status"]["type"]["state"]|"pre";
    g.state=!strcmp(stt,"in")?1:(!strcmp(stt,"post")?2:0);
    JsonObject me,opp; bool f=false;
    for(JsonObject c:cs){ if(!strcmp(c["team"]["id"]|"",teamId)){me=c;f=true;} else opp=c; }
    if(!f) continue;
    g.home=!strcmp(me["homeAway"]|"","home"); g.win=me["winner"]|false;
    g.ts=scoreOf(me); g.os=scoreOf(opp);
    strncpy(g.opp,opp["team"]["abbreviation"]|"",5);
    strncpy(g.tv,comp["broadcasts"][0]["media"]["shortName"]|"",11); normalizeTv(g.tv);
    sg[n++]=g;
  }
  SG out[32]; int m=0;
  for(int i=0;i<n&&m<32;i++){
    out[m++]=sg[i];
    if(league==0&&i+1<n&&m<32&&sg[i].reg&&sg[i+1].reg&&sg[i].week>=1&&sg[i+1].week<=19&&sg[i+1].week==sg[i].week+2){
      SG b={}; b.bye=true; b.week=sg[i].week+1; b.reg=true;
      snprintf(b.wl,sizeof b.wl,"Wk %u",b.week); out[m++]=b;
    }
  }
  for(int i=0;i<m;i++){
    if(out[i].bye) printf("  %-6s  BYE WEEK\n", out[i].wl);
    else if(out[i].state==2) printf("  %-6s  %s %-4s  %c %d-%d  tv=%s\n",out[i].wl,out[i].home?"vs":"@",out[i].opp,out[i].win?'W':'L',out[i].ts,out[i].os,out[i].tv);
    else printf("  %-6s  %s %-4s  upcoming  tv=%s\n",out[i].wl,out[i].home?"vs":"@",out[i].opp,out[i].tv);
  }
  printf("-> %d rows\n",m);
}
