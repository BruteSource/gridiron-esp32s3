#pragma once
#include <Arduino.h>
#include <string.h>
#include <ctype.h>

// TheSportsDB full team name -> { abbreviation, RGB565 primary color }.
struct NflTeam { const char* name; const char* abbr; uint16_t color; };

static const NflTeam NFL_TEAMS[] = {
  { "Arizona Cardinals", "ARI", 0x9107 },   { "Atlanta Falcons", "ATL", 0xA0C6 },
  { "Baltimore Ravens", "BAL", 0x20AE },     { "Buffalo Bills", "BUF", 0x0191 },
  { "Carolina Panthers", "CAR", 0x0439 },    { "Chicago Bears", "CHI", 0x08A5 },
  { "Cincinnati Bengals", "CIN", 0xFA62 },   { "Cleveland Browns", "CLE", 0x30E0 },
  { "Dallas Cowboys", "DAL", 0x01B2 },       { "Denver Broncos", "DEN", 0xFA62 },
  { "Detroit Lions", "DET", 0x03B6 },        { "Green Bay Packers", "GB", 0x21A6 },
  { "Houston Texans", "HOU", 0x0105 },       { "Indianapolis Colts", "IND", 0x016B },
  { "Jacksonville Jaguars", "JAX", 0x032F }, { "Kansas City Chiefs", "KC", 0xE0C6 },
  { "Las Vegas Raiders", "LV", 0xA575 },     { "Los Angeles Chargers", "LAC", 0x0418 },
  { "Los Angeles Rams", "LAR", 0x01B2 },     { "Miami Dolphins", "MIA", 0x0472 },
  { "Minnesota Vikings", "MIN", 0x4930 },    { "New England Patriots", "NE", 0x0108 },
  { "New Orleans Saints", "NO", 0xD5F1 },    { "New York Giants", "NYG", 0x090C },
  { "New York Jets", "NYJ", 0x12A8 },        { "Philadelphia Eagles", "PHI", 0x026A },
  { "Pittsburgh Steelers", "PIT", 0xFDA2 },  { "San Francisco 49ers", "SF", 0xA800 },
  { "Seattle Seahawks", "SEA", 0x0108 },     { "Tampa Bay Buccaneers", "TB", 0xD041 },
  { "Tennessee Titans", "TEN", 0x0908 },     { "Washington Commanders", "WAS", 0x58A2 },
};

static inline void nflAbbr(const char* name, char* out, size_t cap) {
  for (auto& t : NFL_TEAMS)
    if (!strcmp(t.name, name)) { strlcpy(out, t.abbr, cap); return; }
  const char* sp = strrchr(name, ' ');           // last word, first 3 upper
  const char* w = sp ? sp + 1 : name;
  size_t i = 0;
  for (; w[i] && i < 3 && i < cap - 1; i++) out[i] = toupper((unsigned char)w[i]);
  out[i] = 0;
}

static inline uint16_t nflColor(const char* name) {
  for (auto& t : NFL_TEAMS)
    if (!strcmp(t.name, name)) return t.color;
  return 0x8410;
}

// "shortDisplayName" for the detail screen: drop the city, keep the nickname.
static inline void nflNick(const char* name, char* out, size_t cap) {
  const char* sp = strrchr(name, ' ');
  strlcpy(out, sp ? sp + 1 : name, cap);
}
