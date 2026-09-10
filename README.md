# The Game Box — NFL & College football on an ESP32-S3 touchscreen

A standalone, battery-powered desk box that shows live football scores,
play-by-play, schedules, and news. Runs on the **Hosyond ESP32-S3 2.8"
240×320 capacitive touchscreen** module (LCDWIKI ES3C28P design). Everything is
driven by the touchscreen — including first-time Wi-Fi setup.

*Vibecoded with Claude by BruteSource.* Full hardware notes are in
[`HOSYOND_ESP32S3_BOARD.md`](HOSYOND_ESP32S3_BOARD.md).

## Features

**Scores**
- **NFL** and **NCAAF** (college games with at least one AP Top-25 team) — tap
  `NFL` / `NCAAF` in the header to switch.
- Scrollable list: live score + quarter, kickoff time + **TV network** for
  upcoming, or the final. "Halftime" / "End of 1st" between periods.
- Adaptive refresh — 20 s while a game is live, 5 min otherwise (adjustable).
  Scores are only polled while the list or a game screen is open.

**Live game detail** (tap a game)
- Big matchup with a **▸ possession** arrow, quarter + clock, **RED ZONE** badge.
- Down & distance, **timeout dots** per team, stadium weather.
- **Play-by-play** — latest play in full plus a rolling history.
- **STATS** button → team box score + per-player passing / rushing / receiving
  (also reachable from a finished game).
- Score-change celebration animation (toggleable).

**Team schedule** — tap either team's name for its season: week, date, vs/@
opponent, result or kickoff/TV. Bye weeks inferred. Auto-scrolls to the next game.

**News** — newspaper icon → latest ESPN headlines for the active league; tap for
the summary.

**Wi-Fi setup wizard** — no config file needed. Scans, shows networks with signal
bars, on-screen keyboard (with symbols) for the password. Stored in flash;
re-run any time from Settings → `WI-FI`.

**Offline resilience** — the last-known slate and the TV map are saved to flash,
so games, times, and networks still show after a reboot even if a data source is
unreachable. A rate-limited source backs off automatically.

**Battery** — reads cell voltage on GPIO9; header glyph (green / amber / red, ⚡
charging), voltage + % in Settings, blinking low-battery banner.

**Standby** — Settings → `SLEEP` or an idle timer. Screen + Wi-Fi off, tap to
wake. (~2 days — deep sleep hangs on this S3 + octal-PSRAM board.)

**Themes** — 8 palettes (Midnight, Turf, Daylight, Amber, Neon, Volt, 49ers,
**Lime**). Applies instantly, saved to flash. Boot screen is always Lime.

**Settings** also has: brightness, refresh intervals, boot league, NCAAF-today
filter, score alerts, auto-sleep, **Recalibrate**, and **Restart** (soft reboot,
for when the case is closed and the buttons aren't reachable).

**Screen flip** — `SCREEN_ROTATION` in `config.h` (0 = USB up, 2 = USB down);
changing it re-runs touch calibration.

## Data sources

The old all-ESPN design got the device's IP rate-limited, so it's split across
providers. Two free API keys are needed (both instant, no card):

| feed | source | key |
|------|--------|-----|
| NFL scores + schedule | [balldontlie.io](https://balldontlie.io) `/nfl/v1` | `BDL_KEY` |
| NCAAF scores, ranks, schedule, TV | [collegefootballdata.com](https://collegefootballdata.com/key) | `CFBD_KEY` |
| News | `www.espn.com/espn/rss/...` | — |
| Live clock / PBP / down&distance, box score, NFL TV | `site.web.api.espn.com` (on demand only) | — |

Constant polling never touches ESPN's blocked hosts. Keys go in
`include/credentials.h` (git-ignored) — see below.

## Flash a pre-built binary (no toolchain)

A merged image is on the [Releases](../../releases) page.

```
esptool.py --chip esp32s3 --port /dev/ttyACM0 write_flash 0x0 the-game-box.bin
```
If the auto-reset fails: hold **BOOT**, tap **RESET**, release **BOOT**, then flash.

**Wi-Fi:** the pre-built image has no credentials — it boots straight into the
setup wizard. **API keys** are compiled in, so the public binary has none; for
live data you'll want to build from source with your own free keys (or the
device still works for whatever ESPN RSS / on-demand endpoints allow).

## Build from source

Requires [PlatformIO](https://platformio.org/) (`espressif32@6.6.0`, pulled
automatically).

```
git clone https://github.com/BruteSource/gridiron-esp32s3
cd gridiron-esp32s3
cp include/credentials.h.example include/credentials.h
$EDITOR include/credentials.h      # add CFBD_KEY + BDL_KEY (Wi-Fi is on-screen)
$EDITOR include/config.h           # set TZ_STRING, SCREEN_ROTATION
pio run -t upload
pio device monitor                 # open a FRESH monitor after each reset
```

Single flashable image:

```
tools/make_release.sh              # -> dist/the-game-box.bin  (flash at 0x0)
```

### Config (`include/config.h`)

| macro | meaning |
|-------|---------|
| `TZ_STRING` | POSIX TZ for kickoff / schedule times |
| `SCREEN_ROTATION` | 0 = USB port up, 2 = USB port down (180° flip) |
| `POLL_LIVE_MS` / `POLL_IDLE_MS` | default refresh cadence (Settings overrides) |
| `BAT_DIVIDER` / `BAT_CAL_OFFSET` | battery voltage calibration (÷2 divider on GPIO9) |
| `DATES_OVERRIDE` | force a slate, e.g. `"20250906"`, to test on an off day |
| `RGB_LED_ENABLE` | 0 = hold the back RGB LED off |
| `MAX_NFL` / `MAX_CFB` | game array caps |

## How it works

- **Networking** on a FreeRTOS task pinned to core 0; touch + redraw on core 1
  never stall during a fetch. Idle-0 watchdog disabled for that task.
- Each response is slurped into a **PSRAM buffer in chunks** (yielding between
  reads), then parsed with an **ArduinoJson `Filter`** — only the handful of
  fields used are kept.
- Scores come from balldontlie / CFBD; ESPN (`site.web.api`) is hit only
  on-demand for a live game's clock/PBP or the box score, plus a once-a-day
  scoreboard pull for NFL TV.
- On-demand fetches (schedule, news, stats, focused-game detail) are serviced via
  request flags between score polls.
- The UI renders to a full-screen `LGFX_Sprite` in PSRAM; a second sprite backs
  the score-change overlay.
- Settings / calibration / battery trend in NVS; the game-slate + TV cache in
  LittleFS.

### Source layout

```
src/
  lgfx_conf.h     LovyanGFX panel config (ILI9341)
  display.*       canvas sprite, backlight, Lime boot screen, touch calibration
  touch.*         FT6336 raw register reads + tap/drag gestures
  net.cpp         Wi-Fi, NTP, every fetch + filtered JSON parse (core 0)
  wifi_cfg.*      stored credentials + scan/join plumbing for the wizard
  games.*         Game struct, mutex-guarded store, schedule, PBP, LittleFS cache
  news.* stats.*  news + box-score stores
  cfbd_teams.h nfl_teams.h   abbreviation + colour tables
  battery.*       GPIO9 ADC, smoothing, LiPo curve, charge detection
  settings.*      NVS-backed Settings struct
  theme.*         8 RGB565 palettes
  anim.*          score-change celebration overlays
  ui_list / ui_detail / ui_schedule / ui_settings / ui_news / ui_stats / ui_wifi
  main.cpp        setup/loop, screen state machine, standby
tools/            offline JSON parse checks, make_release.sh
```

## License

MIT — see [LICENSE](LICENSE).
