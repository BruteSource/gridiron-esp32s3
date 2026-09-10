#pragma once
#include <Arduino.h>
#include <string.h>
#include <ctype.h>

// School -> { abbreviation, RGB565 primary color }. Covers the programs that
// realistically show up in the AP Top 25. Anything not listed gets an auto
// abbreviation and a neutral gray. CollegeFootballData's /games and /rankings
// both use these exact school strings, so a plain strcmp is enough.
struct CfbdTeam { const char* school; const char* abbr; uint16_t color; };

static const CfbdTeam CFBD_TEAMS[] = {
  { "Alabama", "BAMA", 0x98C6 },            { "Georgia", "UGA", 0xB865 },
  { "Ohio State", "OSU", 0xB800 },          { "Michigan", "MICH", 0x0129 },
  { "Texas", "TEX", 0xBAA0 },               { "Oklahoma", "OU", 0x80A2 },
  { "Oregon", "ORE", 0x1226 },              { "Penn State", "PSU", 0x00E8 },
  { "Notre Dame", "ND", 0x0908 },           { "LSU", "LSU", 0x40EF },
  { "Tennessee", "TENN", 0xFC00 },          { "Florida State", "FSU", 0x7968 },
  { "Clemson", "CLEM", 0xF320 },            { "Florida", "FLA", 0x0114 },
  { "Auburn", "AUB", 0x0908 },              { "Texas A&M", "TAMU", 0x5000 },
  { "Ole Miss", "MISS", 0x1107 },           { "Missouri", "MIZ", 0xF5C5 },
  { "Mississippi State", "MSST", 0x58A4 },  { "Arkansas", "ARK", 0x9906 },
  { "Kentucky", "UK", 0x0194 },             { "South Carolina", "SCAR", 0x7001 },
  { "Vanderbilt", "VAN", 0x8369 },          { "USC", "USC", 0x9800 },
  { "UCLA", "UCLA", 0x2B58 },               { "Washington", "WASH", 0x4970 },
  { "Utah", "UTAH", 0xC800 },               { "Wisconsin", "WISC", 0xC021 },
  { "Iowa", "IOWA", 0xFE60 },               { "Nebraska", "NEB", 0xE0E7 },
  { "Michigan State", "MSU", 0x1A27 },      { "Minnesota", "MINN", 0x7803 },
  { "Illinois", "ILL", 0x1149 },            { "Indiana", "IND", 0x9800 },
  { "Maryland", "MD", 0xE1C7 },             { "Rutgers", "RUTG", 0xC806 },
  { "Purdue", "PUR", 0xCDD1 },              { "Northwestern", "NW", 0x4950 },
  { "Kansas State", "KSU", 0x5151 },        { "Kansas", "KU", 0x0297 },
  { "Oklahoma State", "OKST", 0xFB80 },     { "Baylor", "BAY", 0x1226 },
  { "TCU", "TCU", 0x48CF },                 { "Texas Tech", "TTU", 0xC800 },
  { "Iowa State", "ISU", 0xC885 },          { "West Virginia", "WVU", 0xED40 },
  { "Cincinnati", "CIN", 0xE004 },          { "UCF", "UCF", 0x8410 },
  { "Houston", "HOU", 0xC885 },             { "BYU", "BYU", 0x016B },
  { "Arizona", "ARIZ", 0x018C },            { "Arizona State", "ASU", 0x88E8 },
  { "Colorado", "COLO", 0xCDCF },           { "Miami", "MIA", 0xF384 },
  { "North Carolina", "UNC", 0x7D7A },      { "NC State", "NCST", 0xC800 },
  { "Duke", "DUKE", 0x0190 },               { "Virginia Tech", "VT", 0x6006 },
  { "Louisville", "LOU", 0xA800 },          { "Pittsburgh", "PITT", 0x01B2 },
  { "Georgia Tech", "GT", 0xB50D },         { "Boston College", "BC", 0x9805 },
  { "Wake Forest", "WAKE", 0x9BE7 },        { "Virginia", "UVA", 0x2169 },
  { "SMU", "SMU", 0x3274 },                 { "Boise State", "BSU", 0x0194 },
  { "Fresno State", "FRES", 0xD806 },       { "San Diego State", "SDSU", 0xA0C5 },
  { "Air Force", "AF", 0x01B2 },            { "Army", "ARMY", 0x8410 },
  { "Navy", "NAVY", 0x010B },               { "Memphis", "MEM", 0x0190 },
  { "Tulane", "TULN", 0x0328 },             { "James Madison", "JMU", 0x4010 },
  { "Liberty", "LIB", 0x016C },             { "Appalachian State", "APP", 0x8410 },
  { "Coastal Carolina", "CCU", 0x038E },    { "Washington State", "WSU", 0x98E6 },
  { "Oregon State", "ORST", 0xDA20 },
};

// Fill `out` (>= 6 bytes) with a team abbreviation for `school`.
static inline void cfbdAbbr(const char* school, char* out, size_t cap) {
  for (auto& t : CFBD_TEAMS)
    if (!strcmp(t.school, school)) { strlcpy(out, t.abbr, cap); return; }
  // auto: initials of multi-word names, else first letters, upper-cased
  size_t w = 0;
  bool boundary = true;
  for (const char* p = school; *p && w < cap - 1; p++) {
    if (*p == ' ' || *p == '-' || *p == '&') { boundary = true; continue; }
    if (boundary) { out[w++] = toupper((unsigned char)*p); boundary = false; }
  }
  if (w < 2) {                                   // single word -> first 4 chars
    w = 0;
    for (const char* p = school; *p && w < 4 && w < cap - 1; p++)
      out[w++] = toupper((unsigned char)*p);
  }
  out[w] = 0;
}

static inline uint16_t cfbdColor(const char* school) {
  for (auto& t : CFBD_TEAMS)
    if (!strcmp(t.school, school)) return t.color;
  return 0x8410;                                 // neutral gray
}
