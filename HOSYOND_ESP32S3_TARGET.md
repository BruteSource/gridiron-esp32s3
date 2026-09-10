# Hosyond ESP32-S3 2.8" Touchscreen — Complete Hardware Target Reference

Everything learned about this specific board across three projects on it (ESP32
Marauder port, an audio/weather/games display app, and "The Game Box" NFL/CFB
score tracker). **Hand this whole file to a new Claude session** before writing
any firmware for this board.

Last verified: 2026‑09 on PlatformIO `espressif32@6.6.0` + LovyanGFX 1.2.x.

---

## 0. TL;DR — the things that will bite you

1. **Pin `platform = espressif32@6.6.0` and `board = esp32-s3-devkitc-1`** (plain
   id, no `-n16r8v`). Anything newer / any variant id → early-boot Guru
   Meditation `StoreProhibited`.
2. **Serial is native USB‑CDC.** `while(!Serial){}` hangs `setup()` forever if
   nothing has the port open → "black screen after RESET". Always bound the wait.
3. **ILI9341 needs inversion ON** or every black fill renders white.
4. **No display reset GPIO** — panel RST is tied to chip EN. `pin_rst = -1`.
   A software reset does *not* hardware-reset the panel; only a power cycle does.
5. **Touch is FT6336 capacitive over I2C — do not use library touch drivers.**
   Read registers directly (code below). Touch axes/inversion depend on the
   graphics rotation you pick; re-run 4‑corner calibration per rotation.
6. **Battery ADC is GPIO9** (not free for anything else). On-board ≈ ÷2 divider.
7. **SD is 4‑bit SDIO**, not SPI. Use `SD_MMC`, not `SD`.
8. **Audio codec (ES8311) shares the touch I2C bus.** Enable with GPIO1 = LOW.
9. **USB re-enumerates on every reset** — open a fresh monitor each time.
10. If you enable PSRAM, let the framework init it once — never call
    `esp_spiram_init()` yourself (double-init crash).

---

## 1. Board identity

- Sold as **"Hosyond ESP32-S3 Touchscreen Module, 2.8" 240×320 IPS"**.
- Rebrand of the **LCDWIKI ES3C28P** reference design.
  - <https://www.lcdwiki.com/2.8inch_ESP32-S3_Display>
  - spec PDF: <https://www.lcdwiki.com/res/ES3C28P/ES3C28P_ES2N28P_Specification_V1.0.pdf>
- **MCU:** ESP32‑S3‑WROOM‑1 **N16R8** — 16 MB QSPI flash, 8 MB **OPI (octal)
  PSRAM**, dual Xtensa LX7 @ up to 240 MHz, Wi‑Fi 2.4 GHz + BT5.0, PCB antenna.
- **Display:** ILI9341(V), 240×320 IPS, 4‑wire SPI, RGB666 capable.
- **Touch:** FT6336G **capacitive**, I2C addr `0x38`, chip id `0x64` at reg `0xA3`.
  (NOT resistive / XPT2046.)
- **SD:** real **4‑bit SDIO** controller (NOT SPI‑mode SD).
- **Audio:** ES8311 mono codec + amp, **same I2C bus as touch**, addr `0x18`.
  On-board microphone into the ES8311 ADC; speaker header.
- **RGB LED:** one WS2812‑style addressable LED on a single data wire (GPIO42).
- **USB:** native USB (S3 built-in). `/dev/ttyACM*` on Linux, `COM*` on Windows.
- On-board: LiPo battery header + charger, UART header (`IO43 TX / IO44 RX / 5V /
  GND`), microSD slot, RESET + BOOT buttons, expansion headers.
- 2.4 GHz Wi‑Fi only (no 5 GHz). Hotspot testing: set the AP to 2.4 GHz.

---

## 2. Full GPIO map

| GPIO | Function | Notes |
|-----:|----------|-------|
| 0  | **BOOT button** | strapping pin; pull-up + button to GND |
| 1  | Audio codec **enable** | drive **LOW to enable** ES8311/amp |
| 2  | free | expansion header |
| 3  | free | expansion header; strapping pin (JTAG sel) |
| 4  | Audio **MCLK** | I2S master clock |
| 5  | Audio **BCLK** | I2S bit clock |
| 6  | Audio **DIN** — I2S data *in* (mic → ESP32) | |
| 7  | Audio **LRCK / WS** | I2S word select |
| 8  | Audio **DOUT** — I2S data *out* (ESP32 → speaker) | |
| 9  | **Battery voltage ADC** | ADC1_CH8. On-board ≈ ÷2 divider. `analogReadMilliVolts(9) * ~1.955` = cell volts. **Not free.** |
| 10 | **LCD CS** | active low |
| 11 | **LCD MOSI / SDI** | |
| 12 | **LCD SCLK** | |
| 13 | **LCD MISO / SDO** | |
| 14 | free | expansion header |
| 15 | **I2C SCL** | shared: FT6336 (0x38) + ES8311 (0x18) + header |
| 16 | **I2C SDA** | shared (same bus) |
| 17 | **Touch INT** | active low while touched |
| 18 | **Touch RST** | active low |
| 19 | **USB D−** | native USB — not a GPIO |
| 20 | **USB D+** | native USB — not a GPIO |
| 21 | free | expansion header |
| 26–32 | — | internal SPI flash — **unavailable** |
| 33–37 | — | octal PSRAM — **unavailable** |
| 38 | **SD CLK** | SDIO |
| 39 | **SD D0** | SDIO |
| 40 | **SD CMD** | SDIO |
| 41 | **SD D1** | SDIO; free only in 1‑bit SD mode |
| 42 | **RGB LED** | single-wire WS2812 |
| 43 | **UART0 TX** | serial header (console is on USB‑CDC, so UART0 is free) |
| 44 | **UART0 RX** | serial header |
| 45 | **LCD backlight** | **HIGH = on**; strapping pin (VDD_SPI) |
| 46 | **LCD DC** | strapping pin (ROM msg enable) |
| 47 | **SD D3** | SDIO; free only in 1‑bit SD mode |
| 48 | **SD D2** | SDIO; free only in 1‑bit SD mode |

**Genuinely free GPIOs:** 2, 3, 14, 21 (expansion header), plus 43/44 (UART
header) if not using UART. Add 41/47/48 if SD is mounted 1‑bit instead of 4‑bit.
GPIO 9 is **not** free (battery ADC).

**Display reset:** there is NO reset GPIO — panel RST is tied to the ESP32 EN
line. `pin_rst = -1` (LovyanGFX) / `TFT_RST -1` (TFT_eSPI). Rely on the driver's
software reset (SWRESET 0x01) in its init sequence. If the panel wedges,
power-cycle.

---

## 3. Toolchain — PlatformIO + LovyanGFX (recommended)

`platformio.ini` that is confirmed-good:

```ini
[env:esp32-s3-devkitc-1]
platform = espressif32@6.6.0        ; MUST pin — newer core regresses on this board
board = esp32-s3-devkitc-1          ; MUST be plain id (no -n16r8v variant)
framework = arduino
monitor_speed = 115200
upload_speed = 921600
monitor_filters = esp32_exception_decoder, time

build_flags =
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DARDUINO_USB_MODE=1            ; hardware CDC/JTAG — stable serial number
    -DBOARD_HAS_PSRAM
    -DLGFX_USE_V1
    -DCORE_DEBUG_LEVEL=3

board_build.arduino.memory_type = qio_opi   ; QIO flash + OPI (octal) PSRAM
board_upload.flash_size = 16MB
board_build.partitions = huge_app.csv       ; single 3 MB app, no OTA

lib_deps =
    lovyan03/LovyanGFX @ ^1.2.0
    bblanchon/ArduinoJson @ ^7.2.0
    adafruit/Adafruit NeoPixel @ ^1.12.3
```

**`huge_app.csv` layout** (nvs 20 KB, app 3 MB, spiffs/LittleFS ≈ 896 KB):

```
nvs,      data, nvs,     0x9000,  0x5000
otadata,  data, ota,     0xe000,  0x2000
app0,     app,  ota_0,   0x10000, 0x300000
spiffs,   data, spiffs,  0x310000,0xE0000
coredump, data, coredump,0x3F0000,0x10000
```

- The `spiffs` region is used with **LittleFS** (`LittleFS.begin(true)` to format
  on first mount). Good for a few‑KB–hundreds‑of‑KB blob cache.
- **NVS is only ~20 KB** and is shared by every `Preferences` namespace. A ~7 KB
  `putBytes` blob returned 0 (didn't fit alongside other namespaces). Keep NVS
  for small key/value config; put anything bigger in LittleFS.

### Alternative: arduino-cli (used for the Marauder port)

- `arduino-cli` 1.5.1, core `esp32:esp32@2.0.11` from the pinned release index
  `https://github.com/espressif/arduino-esp32/releases/download/2.0.11/package_esp32_dev_index.json`
- FQBN:
  `esp32:esp32:esp32s3:PSRAM=disabled,FlashSize=16M,PartitionScheme=min_spiffs,CDCOnBoot=cdc,USBMode=hwcdc`
- Marauder specifically had `PSRAM=disabled` (see gotcha #1) and CI patches to
  `platform.txt` (`-zmuldefs`, `-fno-exceptions`).

### Flashing

```
# PlatformIO
pio run -t upload
pio device monitor          # open a FRESH monitor after every reset

# raw esptool (merged image)
esptool.py --chip esp32s3 -p /dev/ttyACM0 write_flash 0x0 firmware.bin
```

If auto-reset into the bootloader misbehaves: **hold BOOT, tap RESET, release
BOOT**, then flash. Standard S3 flash params: mode `dio`, freq `80m`,
size `16MB`. Merge with `esptool.py --chip esp32s3 merge_bin ... -o merged.bin`
(flash merged at `0x0`; components are bootloader @0x0, partitions @0x8000,
`boot_app0.bin` @0xe000, app @0x10000).

---

## 4. Gotchas / hard-won lessons

1. **PSRAM double-init crash.** With OPI PSRAM enabled, calling
   `esp_spiram_init()` yourself on top of the framework init → intermittent
   early-boot `Guru Meditation StoreProhibited`. Let the framework init PSRAM
   once; never touch `esp_spiram_init`. (Marauder worked around it with
   `PSRAM=disabled`.)

2. **`while(!Serial)` hangs forever.** Native USB‑CDC only becomes truthy after
   the host re-enumerates *and reopens* the port. Always bound it:
   ```cpp
   uint32_t t = millis();
   while (!Serial && millis() - t < 1200) delay(10);
   ```

3. **ILI9341 needs inversion ON.** Without it every "black" fill renders white.
   LovyanGFX: `cfg.invert = true;`  TFT_eSPI: `#define TFT_INVERSION_ON`.

4. **Display RST tied to EN.** `pin_rst = -1`. Software/watchdog reset restarts
   the MCU but not the panel; you rely on the driver's SWRESET. Full power-cycle
   does reset the panel.

5. **`neopixelWrite()` core bug (2.0.x).** Caches the first pin it's ever called
   with, ignores the `pin` arg afterwards. Use `Adafruit_NeoPixel` (GRB,
   800 kHz) or bit-bang, or only ever call `neopixelWrite(42, ...)`.

6. **USB re-enumerates on every reset.** Fresh monitor each time. With
   `ARDUINO_USB_MODE=1` the serial number is stable-ish; TinyUSB mode increments
   the `/dev/ttyACM` number each reset. When flashing via `pio`, monitor with
   DTR/RTS deasserted.

7. **Don't trust library FT6336 drivers.** LovyanGFX's `Touch_FT5x06` mishandles
   a negative `y_max` (clamps every touch to the bottom edge). Read the FT6336
   registers directly (§5c).

8. **Touch orientation is rotation-dependent.** Raw axes and whether they invert
   change per graphics rotation. Rotation 0 (USB down for LovyanGFX `setRotation`
   varies — measure it): roughly `raw_x 20..230 → 0..240`, `raw_y 16..306 →
   0..320`, no inversion. Other rotations had axes inverted. The FT6336 also
   emits an occasional `0x0FFF` garbage sample between touches — reject
   `raw_x > ~300/800 || raw_y > ~360/800`.

9. **SD is SDIO, not SPI.** No SPI SD libraries. Use `SD_MMC` (§5d). 1‑bit mode
   frees 41/47/48.

10. **Strapping pins in use.** GPIO 45 (backlight) and 46 (DC) are strapping
    pins. Fine as normal outputs *after* boot — just don't hard-drive them
    during reset.

11. **Backlight** GPIO 45, active HIGH, PWM via LEDC fine (5–12 kHz / 8‑bit).

12. **Audio codec shares the touch I2C bus.** ES8311 @ `0x18`, FT6336 @ `0x38`
    on SDA=16 / SCL=15. Pull GPIO1 LOW to enable codec/amp. Full ES8311 register
    init (0x00–0x37, from Espressif's esp-adf reference driver) is required —
    reuse verbatim from working audio code.

13. **Deep sleep hangs on this S3 + octal-PSRAM board.** `esp_light_sleep_start()`
    also hangs. `lcd.sleep()` can wedge the panel. For low-power standby: set
    backlight to 0, `WiFi.mode(WIFI_OFF)`, and poll the touch controller in a
    loop until a tap — draws ~45–55 mA (~2–3 days on a 3000 mAh cell). The
    FT6336 must be in polling INT mode (reg `0xA4 = 0x00`) so INT keeps pulsing
    while touched.

14. **After `WiFi.mode(WIFI_OFF)` then back on**, re-init the I2C bus for touch
    (`Wire.begin(SDA, SCL, 400000)`) — call it `touch_bus_resume()`.

15. **`timegm()` is not in newlib on ESP32.** Use a days-from-civil helper for
    UTC→epoch.

16. **HTTPS: reading the TLS stream one byte at a time pegs the core for seconds
    on big responses → idle‑task watchdog reset.** Buffer the body into PSRAM in
    chunks with `vTaskDelay` yields between reads (§6b). And run networking on a
    core‑0 FreeRTOS task with `disableCore0WDT()`.

---

## 5. Working code snippets

### 5a. LovyanGFX panel config — CONFIRMED on hardware

```cpp
#pragma once
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 _panel;
  lgfx::Bus_SPI       _bus;
  lgfx::Light_PWM     _light;
public:
  LGFX() {
    { auto c = _bus.config();
      c.spi_host   = SPI3_HOST;      // SPI2_HOST also fine
      c.spi_mode   = 0;
      c.freq_write = 40000000;       // 40 MHz stable
      c.freq_read  = 16000000;
      c.pin_sclk = 12; c.pin_mosi = 11; c.pin_miso = 13; c.pin_dc = 46;
      _bus.config(c); _panel.setBus(&_bus);
    }
    { auto c = _panel.config();
      c.pin_cs = 10; c.pin_rst = -1; c.pin_busy = -1;   // rst tied to EN
      c.panel_width = 240; c.panel_height = 320;
      c.offset_x = 0; c.offset_y = 0;
      c.invert = true;               // REQUIRED
      c.readable = true; c.bus_shared = false;
      _panel.config(c);
    }
    { auto c = _light.config();
      c.pin_bl = 45; c.invert = false; c.freq = 12000; c.pwm_channel = 7;
      _light.config(c); _panel.setLight(&_light);
    }
    setPanel(&_panel);
  }
};
```

Rotation: `lcd.setRotation(0)` = USB port up; `2` = USB port down (180° flip).
Changing rotation invalidates a stored touch calibration — force recal.

Full-screen sprite in PSRAM (leaves ~7.5 MB PSRAM free; a second 240×320×16bpp
sprite for overlays is also fine — each is 150 KB):

```cpp
LGFX_Sprite canvas(&lcd);
canvas.setColorDepth(16);
canvas.setPsram(true);
canvas.createSprite(240, 320);
```

### 5b. TFT_eSPI User_Setup (arduino-cli path)

```c
#define ILI9341_DRIVER
#define TFT_WIDTH  240
#define TFT_HEIGHT 320
#define TFT_INVERSION_ON          // REQUIRED
#define TFT_MISO 13
#define TFT_MOSI 11
#define TFT_SCLK 12
#define TFT_CS   10
#define TFT_DC   46
#define TFT_RST  -1               // tied to EN
#define TFT_BL   45
#define TFT_BACKLIGHT_ON HIGH
#define TOUCH_CS -1               // FT6336 is I2C, not SPI
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_GFXFF
#define SMOOTH_FONT
#define SPI_FREQUENCY       40000000
#define SPI_READ_FREQUENCY  20000000
```

### 5c. FT6336 capacitive touch — raw register reads

```cpp
#include <Wire.h>
#define FT_ADDR 0x38
#define PIN_SDA 16
#define PIN_SCL 15
#define PIN_RST 18
#define PIN_INT 17

void touch_init() {
  pinMode(PIN_RST, OUTPUT);
  digitalWrite(PIN_RST, LOW);  delay(10);
  digitalWrite(PIN_RST, HIGH); delay(300);
  Wire.begin(PIN_SDA, PIN_SCL, 400000U);
  pinMode(PIN_INT, INPUT_PULLUP);
  // raise touch threshold a little to cut phantom taps
  Wire.beginTransmission(FT_ADDR); Wire.write(0x80); Wire.write(40);
  Wire.endTransmission();
  // G_MODE = polling: INT pulses low repeatedly while touched (needed to wake
  // reliably from the light-standby loop)
  Wire.beginTransmission(FT_ADDR); Wire.write(0xA4); Wire.write(0x00);
  Wire.endTransmission();
}

// raw touch point; returns false if no finger / garbage sample
bool ftRaw(int* rx, int* ry) {
  uint8_t d[7];
  Wire.beginTransmission(FT_ADDR); Wire.write(0x02);
  if (Wire.endTransmission(false) != 0) return false;
  if ((int)Wire.requestFrom(FT_ADDR, 7) != 7) return false;
  for (int i = 0; i < 7; i++) d[i] = Wire.read();
  if ((d[0] & 0x0F) == 0) return false;                  // no finger
  int x = ((uint16_t)(d[1] & 0x0F) << 8) | d[2];
  int y = ((uint16_t)(d[3] & 0x0F) << 8) | d[4];
  if (x > 800 || y > 800) return false;                  // 0x0FFF garbage guard
  *rx = x; *ry = y; return true;
}

// map raw -> screen with a stored 4-corner calibration (xMin/xMax/yMin/yMax)
long sx = map(rx, cal.xMin, cal.xMax, 0, 240);
long sy = map(ry, cal.yMin, cal.yMax, 0, 320);
// near an edge the contact patch pulls toward centre — nudge edge taps outward:
if (sy < 48)       sy -= (48 - sy) / 4;
else if (sy > 271) sy += (sy - 271) / 4;
```

Default endpoints before calibration (rotation 0): `{20, 230, 16, 306}`.
4‑corner calibration: show a crosshair at each of `(24,28) (216,28) (216,292)
(24,292)`, average the raw samples per corner, linear-extrapolate to the 0/240
and 0/320 edges, sanity-check `|xMax-xMin| > 40 && |yMax-yMin| > 40`. Persist to
NVS **with the rotation value** and ignore a stored cal whose rotation differs.

### 5d. SD card (SDIO via SD_MMC)

```cpp
#include "FS.h"
#include "SD_MMC.h"
bool sd_begin() {
  SD_MMC.setPins(38 /*CLK*/, 40 /*CMD*/, 39 /*D0*/, 41 /*D1*/, 48 /*D2*/, 47 /*D3*/);
  return SD_MMC.begin("/sdcard", true /*1-bit: frees 41/48/47*/, true /*format if needed*/);
}
```

### 5e. RGB LED (WS2812 on GPIO 42)

```cpp
#include <Adafruit_NeoPixel.h>
Adafruit_NeoPixel rgb(1, 42, NEO_GRB + NEO_KHZ800);
rgb.begin(); rgb.setBrightness(60);
rgb.setPixelColor(0, rgb.Color(r, g, b)); rgb.show();
```

The LED can power up lit/animating — drive a few clean "off" frames at boot, then
`pinMode(42, OUTPUT); digitalWrite(42, LOW);` if you don't want it.

### 5f. Battery sense (GPIO9)

```cpp
#define BAT_ADC_PIN 9
#define BAT_DIVIDER 1.955f     // calibrate: this unit reads ~1.79 V for a 3.50 V cell
analogSetPinAttenuation(BAT_ADC_PIN, ADC_11db);   // ~0..3.1 V range
float v = 0;
for (int i = 0; i < 24; i++) v += analogReadMilliVolts(BAT_ADC_PIN);
v = (v / 24.0f / 1000.0f) * BAT_DIVIDER;
```

Smooth with an EMA (`s_v += (r - s_v) * 0.25f`, ~2 s cadence). LiPo % LUT
(voltage → %): `4.20→100, 4.10→92, 4.00→82, 3.90→68, 3.83→55, 3.78→45, 3.73→35,
3.68→24, 3.62→15, 3.52→7, 3.40→2, 3.20→0` (interpolate between). Charge detection:
`v ≥ 4.05` → on charger, or a steady climb (> ~12 mV over ~10 min).

### 5g. Audio (ES8311) — outline

```
GPIO 1  -> LOW to enable codec + amp
I2S: MCLK=4, BCLK=5, WS/LRCK=7, DOUT(to spk)=8, DIN(from mic)=6
ES8311 on I2C 0x18 (SDA 16 / SCL 15, shared with touch)
Full 0x00-0x37 register init: from esp-adf ES8311 reference driver; reuse as-is.
```

---

## 6. Networking patterns (learned on The Game Box)

### 6a. Core-0 network task

```cpp
void net_start() {
  xTaskCreatePinnedToCore(netTask, "net", 16384, nullptr, 1, nullptr, 0);
}
static void netTask(void*) {
  disableCore0WDT();               // long TLS handshakes / JSON parses on core 0
  for (;;) { /* wifi, fetch, parse, publish to mutex-guarded stores */ }
}
```

UI (LovyanGFX + touch) runs on the Arduino `loop()` (core 1) and never stalls
during a fetch. Cross-task state via `volatile` flags + small mutex-guarded
snapshot buffers (request/response pattern).

### 6b. Buffered HTTPS GET into PSRAM

```cpp
WiFiClientSecure client; client.setInsecure(); client.setTimeout(12);
HTTPClient http;
http.setConnectTimeout(8000); http.setTimeout(12000);
http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
http.begin(client, url);
http.useHTTP10(true);
http.setUserAgent("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
                  "(KHTML, like Gecko) Chrome/122.0 Safari/537.36");
// then loop st->available() / st->readBytes into a heap_caps_malloc(MALLOC_CAP_SPIRAM)
// buffer, vTaskDelay(pdMS_TO_TICKS(3)) when nothing's available, 15 s stall timeout.
```

A default `ESP32HTTPClient` User-Agent gets rate-limited / blocked harder by
Akamai-fronted APIs — send a browser UA.

### 6c. ArduinoJson v7 in PSRAM

```cpp
struct SpiRamAllocator : ArduinoJson::Allocator {
  void* allocate(size_t n) override { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM); }
  void  deallocate(void* p) override { heap_caps_free(p); }
  void* reallocate(void* p, size_t n) override { return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM); }
};
SpiRamAllocator alloc;
JsonDocument doc(&alloc);
deserializeJson(doc, body, DeserializationOption::Filter(filter));
```

**Filter gotcha:** `JsonObject x = filter["a"]["b"][0];` captures a *null* node and
every later write through `x` is silently dropped. Use
`filter["a"]["b"][0].to<JsonObject>()` or write full chained paths
(`filter["a"][0]["b"] = true`).

### 6d. Time

`configTzTime(TZ_STRING, "pool.ntp.org", "time.nist.gov", "time.google.com")`,
then poll `getLocalTime(&tm, 500)` up to ~15 s. `timegm()` is missing — use a
days-from-civil helper for UTC→epoch.

---

## 7. NVS namespaces already used (avoid collisions if reusing config code)

| namespace | contents |
|-----------|----------|
| `grid_cfg`   | app settings struct |
| `grid_wifi`  | `ssid` / `pass` (on-screen wizard) |
| `grid_touch` | `xmn/xmx/ymn/ymx` calibration + `rot` |

Rename these for a fresh project so a re-flash doesn't inherit stale values, or
`nvs_flash_erase` once.

---

## 8. Confirmed working on this board

- Display + calibrated capacitive touch (LovyanGFX and TFT_eSPI both).
- Full-screen + overlay PSRAM sprites, 16bpp.
- Wi‑Fi (STA) + HTTPS + NTP; BLE (full ESP32 Marauder feature set runs).
- SD read/write over SDIO.
- ES8311 audio output (tones / melodies).
- RGB LED.
- Battery voltage read + charge detection on GPIO9.
- Light "standby" (backlight+Wi‑Fi off, touch-poll wake). Deep sleep does NOT work.
- Projects: ESP32 Marauder port (`github.com/BruteSource/ESP32Marauder`, branch
  `hosyond-s3-port`, board id `MARAUDER_HOSYOND_S3`); Open‑Meteo weather display;
  whack‑a‑mole; endless-zoom Mandelbrot; "The Game Box" NFL/CFB score tracker
  (`github.com/BruteSource/gridiron-esp32s3`).

## 9. Per-unit specifics (may differ on another physical unit)

- Touch calibration endpoints and `BAT_DIVIDER 1.955` were measured on one unit —
  re-calibrate both on a different board/panel.
- FT6336 reports chip id `0x64` at register `0xA3`.
- RGB LED responds to standard WS2812B GRB 800 kHz timing.
