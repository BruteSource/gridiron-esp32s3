# Gridiron — NFL & College football on an ESP32-S3 touchscreen

A standalone, battery-powered desk board that shows live football scores, play-by-play,
schedules, and news. Runs on the **Hosyond ESP32-S3 2.8" 240×320 capacitive
touchscreen** module (LCDWIKI ES3C28P design). Data comes from ESPN's public
JSON API over Wi-Fi; everything is driven by the touchscreen.

Full hardware notes for this board are in [`HOSYOND_ESP32S3_BOARD.md`](HOSYOND_ESP32S3_BOARD.md).

## Features

**Scores**
- **NFL** and **NCAAF** (college games with at least one AP Top-25 team) — tap
  `NFL` / `NCAAF` in the header to switch.
- Scrollable game list: live score, quarter + clock, or kickoff time + TV
  network for upcoming, or the final.
- Adaptive refresh — 20 s while a game is live, 5 min otherwise (both adjustable).

**Live game detail** (tap a live game)
- Big matchup with a **▸ possession** arrow.
- Quarter + clock, **RED ZONE** badge.
- Down & distance, **timeout dots** per team, and **stadium weather** (e.g. `71F Sunny`).
- **Play-by-play** — the latest play in full, plus a rolling history of recent
  plays, updated every poll.
- Full-screen **score-change celebration** animation (three styles, toggleable).

**Team schedule** — tap either team's name on the detail screen for its full
season: week, date, vs/@ opponent, result or kickoff/TV. NFL bye weeks are
inferred and shown. Auto-scrolls to the next game.

**News** — newspaper icon in the header → latest ESPN headlines for the active
league; tap one for the summary (word-wrapped, scrollable).

**Battery** — reads the cell voltage on GPIO9; level glyph in the header
(green / amber / red, ⚡ when charging), voltage + % readout in Settings, and a
blinking low-battery banner.

**Standby** — Settings → `SLEEP`, or an idle auto-sleep timer. Screen + Wi-Fi
off, polls the touch chip; tap anywhere to wake. (Not deep sleep — that hangs on
this S3 + octal-PSRAM board — so ~2 days of standby rather than weeks.)

**Themes** — 7 palettes in Settings: Midnight, Turf, Daylight, Amber, Neon,
Volt, 49ers. Applies everywhere instantly, saved to flash.

**Touch calibration** — a 4-point routine, stored in NVS. Auto-runs on first
boot; re-run from Settings or by holding the screen during the splash.

## Flash a pre-built binary (no toolchain)

A merged image is on the [Releases](../../releases) page. **It has placeholder
Wi-Fi credentials baked in — you must build from source with your own
`credentials.h` to actually connect** (there is no on-screen Wi-Fi entry).

```
esptool.py --chip esp32s3 --port /dev/ttyACM0 write_flash 0x0 gridiron-esp32s3.bin
```

If the auto-reset fails: hold **BOOT**, tap **RESET**, release **BOOT**, then flash.

## Build from source

Requires [PlatformIO](https://platformio.org/) (`espressif32@6.6.0`, pulled
automatically).

```
git clone https://github.com/BruteSource/gridiron-esp32s3
cd gridiron-esp32s3
cp include/credentials.h.example include/credentials.h     # add your Wi-Fi
$EDITOR include/config.h                                   # set TZ_STRING
pio run -t upload
pio device monitor          # open a FRESH monitor after each reset
```

Produce a single flashable image:

```
tools/make_release.sh                     # -> dist/gridiron-esp32s3.bin (flash at 0x0)
```

### Config (`include/config.h`)

| macro | meaning |
|-------|---------|
| `TZ_STRING` | POSIX TZ for kickoff / schedule times |
| `POLL_LIVE_MS` / `POLL_IDLE_MS` | default refresh cadence (Settings overrides) |
| `BAT_DIVIDER` / `BAT_CAL_OFFSET` | battery voltage calibration (÷2 divider on GPIO9) |
| `DATES_OVERRIDE` | force a slate, e.g. `"20250906"`, to test on an off day |
| `RGB_LED_ENABLE` | 0 = hold the back RGB LED off (power saving) |
| `MAX_NFL` / `MAX_CFB` | game array caps |

## How it works

- **Networking** runs in a FreeRTOS task pinned to core 0, so touch and redraw
  on core 1 never stall during a fetch. The idle-0 watchdog is disabled for that
  task (TLS + JSON parse are long CPU bursts).
- Each response is pulled into a **PSRAM buffer in chunks** (yielding between
  reads), then parsed with an **ArduinoJson `Filter`** so only the handful of
  fields we use are kept — a ~300 KB scoreboard becomes a few KB.
- On-demand fetches (schedule, news) are serviced via request flags the net task
  checks between score polls.
- The UI renders to a full-screen `LGFX_Sprite` in PSRAM; a second sprite backs
  the score-change overlay.
- Settings, calibration, and battery trend live in NVS.

### Source layout

```
src/
  lgfx_conf.h    LovyanGFX panel config (ILI9341)
  display.*      canvas sprite, backlight, boot screen, touch calibration
  touch.*        FT6336 raw register reads + tap/drag gestures
  net.*          Wi-Fi, NTP, all ESPN fetches + filtered JSON parse (core 0)
  games.*        Game struct, mutex-guarded score store, schedule + PBP
  news.*         news store
  battery.*      GPIO9 ADC, smoothing, LiPo curve, charge detection
  settings.*     NVS-backed Settings struct
  theme.*        7 RGB565 palettes
  anim.*         score-change celebration overlays
  ui_list.cpp / ui_detail.cpp / ui_schedule.cpp / ui_settings.cpp / ui_news.cpp
  main.cpp       setup/loop, screen state machine, standby
tools/           offline JSON parse checks, make_release.sh
```

## Data source

ESPN's public (keyless) endpoints, all under
`https://site.api.espn.com/apis/site/v2/sports/football/{nfl|college-football}/`:
`scoreboard`, `teams/{id}/schedule`, `news`. TLS via `setInsecure()`.
These are undocumented and unofficial — fine for one personal device, don't
build a service on them.

## License

MIT — see [LICENSE](LICENSE).
