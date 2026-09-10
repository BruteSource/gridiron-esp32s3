# Hosyond ESP32-S3 2.8" Touchscreen Board — Developer Reference

Everything learned about this specific board while porting ESP32 Marauder to it.
Hand this whole file to a new Claude session to develop firmware/software for it.

---

## 1. Board identity

- Sold as **"Hosyond ESP32-S3 Touchscreen Module, 2.8" 240x320 IPS"**.
- It is a rebrand of the **LCDWIKI ES3C28P** reference design
  (docs: <https://www.lcdwiki.com/2.8inch_ESP32-S3_Display>,
  spec PDF: <https://www.lcdwiki.com/res/ES3C28P/ES3C28P_ES2N28P_Specification_V1.0.pdf>).
- **MCU:** ESP32-S3-WROOM-1 **N16R8** — 16 MB QSPI flash, 8 MB **OPI (octal) PSRAM**,
  Xtensa dual-core LX7 @ up to 240 MHz, Wi-Fi 2.4 GHz + BT 5.0, on-board antenna.
- **Display:** ILI9341(V) driver, 240x320 IPS, 4-wire SPI, RGB666 capable.
- **Touch:** FT6336G **capacitive**, I2C, address `0x38`. (NOT resistive / XPT2046.)
- **SD:** real **4-bit SDIO** controller (NOT SPI-mode SD).
- **Audio:** ES8311 mono codec + amplifier, on the **same I2C bus as touch**, address `0x18`.
- **RGB LED:** one WS2812-style addressable LED (single data wire), internal-IC RGB.
- **USB:** native USB (ESP32-S3 built-in). Shows as `/dev/ttyACM*` on Linux / `COM*` on Windows.
- On-board: microphone (into ES8311 ADC), speaker header, battery (LiPo) header,
  UART header, microSD slot, RESET + BOOT buttons, and expansion headers.

---

## 2. Full GPIO map

| GPIO | Function | Notes |
|-----:|----------|-------|
| 0  | **BOOT button** | strapping pin; has pull-up + button to GND |
| 1  | Audio codec **enable** | drive **LOW to enable** the ES8311/amp |
| 2  | free | brought out on expansion header |
| 3  | free | expansion header; also a strapping pin (JTAG sel) |
| 4  | Audio **MCLK** (I2S master clock) | |
| 5  | Audio **BCLK** (I2S bit clock) | |
| 6  | Audio **DIN** — ESP32 I2S data *in* (mic → ESP32) | |
| 7  | Audio **LRCK / WS** (I2S word select) | |
| 8  | Audio **DOUT** — ESP32 I2S data *out* (ESP32 → speaker) | |
| 9  | **Battery voltage ADC** | LCDWIKI ES3C28P spec: "Battery voltage ADC value acquisition input". ADC1_CH8. On-board divider ≈ **/2** (pin reads ~1.79 V for a 3.58 V cell). `analogReadMilliVolts(9) * 2` = battery volts. |
| 10 | **LCD CS** | active low |
| 11 | **LCD MOSI / SDI** | |
| 12 | **LCD SCLK** | |
| 13 | **LCD MISO / SDO** | |
| 14 | free | expansion header |
| 15 | **I2C SCL** | shared: FT6336 touch (0x38) + ES8311 audio (0x18) + peripheral header |
| 16 | **I2C SDA** | shared (same bus as above) |
| 17 | **Touch INT** | active low when a touch is present; usable as a GPIO interrupt |
| 18 | **Touch RST** | active low |
| 19 | **USB D−** | native USB — do not use as GPIO |
| 20 | **USB D+** | native USB — do not use as GPIO |
| 21 | free | expansion header |
| 26–32 | — | ESP32-S3-WROOM-1 internal SPI flash — **unavailable** |
| 33–37 | — | N16R8 octal PSRAM — **unavailable** |
| 38 | **SD CLK** | SDIO |
| 39 | **SD D0** | SDIO |
| 40 | **SD CMD** | SDIO |
| 41 | **SD D1** | SDIO; free only if SD runs in **1-bit** mode |
| 42 | **RGB LED** | single-wire WS2812-style |
| 43 | **UART0 TX** | on the serial header (`IO43 TXD / IO44 RXD / 5V / GND`) |
| 44 | **UART0 RX** | serial header — console is on USB-CDC so UART0 is free for e.g. GPS |
| 45 | **LCD backlight** | **HIGH = on**; also strapping pin (VDD_SPI) |
| 46 | **LCD DC** (data/command) | also strapping pin (ROM message enable) |
| 47 | **SD D3** | SDIO; free only if SD runs in **1-bit** mode |
| 48 | **SD D2** | SDIO; free only if SD runs in **1-bit** mode |

**Genuinely free GPIOs:** 2, 3, 14, 21 (expansion header), 43 + 44 (UART header,
if not used for UART). Plus 41 / 47 / 48 if the SD card is mounted 1-bit instead
of 4-bit. (GPIO 9 is NOT free — it's the battery voltage ADC.)

**Display reset:** there is NO reset GPIO. The panel's RST is tied to the ESP32
chip-enable (EN) line. Use `TFT_RST = -1` (TFT_eSPI) / `pin_rst = -1` (LovyanGFX).

---

## 3. Toolchains — two known-good setups

### 3a. PlatformIO + LovyanGFX (used for the display/audio/game apps)

```ini
[env:esp32-s3-devkitc-1]
platform = espressif32@6.6.0   ; MUST be pinned. Unpinned/"latest" -> early boot
                               ; crash (Guru Meditation StoreProhibited), a
                               ; newer-core regression on this board.
board = esp32-s3-devkitc-1     ; MUST be the plain id. A variant like
                               ; "-n16r8v" caused the same early boot crash.
framework = arduino
monitor_speed = 115200
upload_speed = 921600
build_flags =
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DARDUINO_USB_MODE=1        ; = hardware CDC/JTAG (stable serial number)
board_build.arduino.memory_type = qio_opi   ; QIO flash + OPI PSRAM
board_upload.flash_size = 16MB
lib_deps =
    lovyan03/LovyanGFX @ ^1.2.0
    bblanchon/ArduinoJson @ ^6.21.5
```

### 3b. arduino-cli (used for the Marauder port)

- `arduino-cli` 1.5.1
- Core: `esp32:esp32@2.0.11`, installed from the pinned Espressif release index
  `https://github.com/espressif/arduino-esp32/releases/download/2.0.11/package_esp32_dev_index.json`
- FQBN:
  ```
  esp32:esp32:esp32s3:PSRAM=disabled,FlashSize=16M,PartitionScheme=min_spiffs,CDCOnBoot=cdc,USBMode=hwcdc
  ```
- For Marauder specifically, its CI also patches `platform.txt` (append `-zmuldefs`
  to `compiler.c.elf.libs.esp32*`, swap `-fexceptions`→`-fno-exceptions`).

### Flash layout (arduino default, esp32s3)

| offset | file |
|--------|------|
| 0x0     | bootloader |
| 0x8000  | partition table |
| 0xe000  | `boot_app0.bin` (from the core's `tools/partitions/`) |
| 0x10000 | application |

flash_mode `dio`, flash_freq `80m`, flash_size `16MB`, chip `esp32s3`.
Merge into one image with `esptool.py --chip esp32s3 merge_bin ... -o merged.bin`
(flash `merged.bin` at `0x0`).

---

## 4. Gotchas / hard-won lessons

1. **PSRAM double-init crash.** With OPI PSRAM enabled, calling `esp_spiram_init()`
   yourself (on top of the framework's init) causes an intermittent early-boot
   `Guru Meditation StoreProhibited`. LovyanGFX apps ran fine with `qio_opi` because
   they don't re-init PSRAM. Marauder had to build **`PSRAM=disabled`**. Rule: if you
   enable PSRAM, let the framework init it once and don't touch `esp_spiram_init`.

2. **`while(!Serial)` hangs forever.** Serial is native USB-CDC. After a reset the
   host must re-enumerate and *reopen* the port before `Serial` becomes truthy — if
   nothing is monitoring, `while(!Serial){}` blocks `setup()` (and your display init)
   permanently → "black screen after every RESET press". Always bound it:
   ```cpp
   uint32_t t = millis();
   while (!Serial && millis() - t < 1200) delay(10);
   ```

3. **ILI9341 needs display inversion ON.** Without it, every "black" fill renders
   **white** and colours look off. TFT_eSPI: `#define TFT_INVERSION_ON`.
   LovyanGFX: `cfg.invert = true;` in the panel config.

4. **Display RST tied to EN.** No GPIO reset (`pin_rst=-1`). A software/watchdog
   reset restarts the ESP32 but does NOT hardware-reset the panel — you rely on the
   driver's software-reset (SWRESET, 0x01) in its init sequence. A full power-cycle
   does reset the panel. If the panel ever wedges, power-cycle.

5. **`neopixelWrite()` core bug (2.0.x).** It caches the first pin it's ever called
   with and ignores the `pin` argument on every later call. Useless for probing pins.
   Drive the RGB LED with `Adafruit_NeoPixel` (GRB, 800 kHz) or a bit-banged WS2812
   routine, or only ever call `neopixelWrite(42, ...)`.

6. **USB re-enumerates on every reset.** Open a FRESH serial monitor after each
   upload/reset. With `ARDUINO_USB_MODE=1` / `USBMode=hwcdc` the USB serial number is
   stable-ish; with TinyUSB mode the `/dev/ttyACM` number increments each time.

7. **Touch: don't trust library FT6336 drivers.** LovyanGFX's built-in
   `Touch_FT5x06` mishandles a negative `y_max` (clamps every touch to the bottom).
   Read the FT6336 registers directly over I2C instead (see §5). Marauder's bundled
   `ft6336.h` also reads raw registers — that's the reliable path.

8. **Touch orientation differs per graphics rotation.** In Marauder's rotation 0
   (portrait, USB pointing down) the raw FT6336 axes are **NOT inverted**:
   roughly `raw_x 20..230 → screen 0..240`, `raw_y 16..306 → screen 0..320`.
   In a different LovyanGFX rotation the earlier calibration had them inverted
   (`raw_x 204→11`, `raw_y 315→-10`). Re-measure by tapping the four corners for
   whatever rotation you use. The FT6336 also emits an occasional `0x0FFF` garbage
   sample between touches — reject `raw_x > ~300 || raw_y > ~360`.

9. **SD is SDIO, not SPI.** No SPI SD libraries. Use `SD_MMC` (see §5). 1-bit mode
   frees GPIO 41/47/48 for other use; 4-bit mode is faster but claims them.

10. **Strapping pins in use.** GPIO 45 (backlight) and 46 (DC) are strapping pins
    (VDD_SPI select, ROM-msg enable). They work fine as normal outputs *after* boot,
    which is what the board does — just don't hard-drive them during reset.

11. **Backlight** is GPIO 45, active HIGH. PWM via LEDC is fine (5 kHz / 8-bit works).

12. **Audio codec shares the touch I2C bus.** ES8311 at `0x18`, FT6336 at `0x38` on
    SDA=16 / SCL=15. Pull GPIO1 LOW to enable the codec/amp. Full ES8311 register
    init sequence (regs 0x00–0x37, based on Espressif's reference driver) is in the
    original working audio code — reuse it verbatim.

---

## 5. Working code snippets

### 5a. TFT_eSPI User_Setup (arduino-cli path)

```c
#define ILI9341_DRIVER
#define TFT_WIDTH  240
#define TFT_HEIGHT 320
#define TFT_INVERSION_ON          // REQUIRED on this panel

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

#define SPI_FREQUENCY       40000000   // 40 MHz is stable; 27 MHz also fine
#define SPI_READ_FREQUENCY  20000000
```

### 5b. LovyanGFX panel config (PlatformIO path)

> Reconstructed from the confirmed pinout — the authoritative version is in the
> original working display project. Key facts: ILI9341, pins as below, `pin_rst = -1`,
> `invert = true`, backlight on GPIO 45 non-inverted.

```cpp
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 _panel;
  lgfx::Bus_SPI       _bus;
  lgfx::Light_PWM     _light;
public:
  LGFX() {
    { auto c = _bus.config();
      c.spi_host   = SPI3_HOST;   // or SPI2_HOST
      c.spi_mode   = 0;
      c.freq_write = 40000000;
      c.freq_read  = 16000000;
      c.pin_sclk = 12; c.pin_mosi = 11; c.pin_miso = 13; c.pin_dc = 46;
      _bus.config(c); _panel.setBus(&_bus);
    }
    { auto c = _panel.config();
      c.pin_cs = 10; c.pin_rst = -1; c.pin_busy = -1;   // rst tied to EN
      c.panel_width = 240; c.panel_height = 320;
      c.invert = true;                                   // REQUIRED
      c.readable = true; c.bus_shared = false;
      _panel.config(c);
    }
    { auto c = _light.config(); c.pin_bl = 45; c.invert = false;
      c.freq = 12000; c.pwm_channel = 7; _light.config(c);
      _panel.setLight(&_light);
    }
    setPanel(&_panel);
  }
};
```

### 5c. FT6336 capacitive touch — raw register read (rotation 0)

```cpp
#include <Wire.h>
#define FT6336_ADDR 0x38
#define CTP_SDA 16
#define CTP_SCL 15
#define CTP_RST 18
#define CTP_INT 17

volatile bool ft_tap = false;
void IRAM_ATTR ft_isr() { ft_tap = true; }   // INT fires low on a new touch

void ft6336_begin() {
  pinMode(CTP_RST, OUTPUT);
  digitalWrite(CTP_RST, LOW);  delay(10);
  digitalWrite(CTP_RST, HIGH); delay(300);
  Wire.begin(CTP_SDA, CTP_SCL, 400000U);
  pinMode(CTP_INT, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(CTP_INT), ft_isr, FALLING);
  // optional: raise threshold to cut phantom touches
  Wire.beginTransmission(FT6336_ADDR); Wire.write(0x80); Wire.write(40);
  Wire.endTransmission();
}

// returns 1 with screen coords (240x320, rotation 0) if a finger is down
uint8_t ft6336_read(uint16_t *x, uint16_t *y) {
  uint8_t d[7];
  Wire.beginTransmission(FT6336_ADDR); Wire.write(0x02);
  if (Wire.endTransmission(false) != 0) return 0;
  Wire.requestFrom(FT6336_ADDR, 7);
  for (int i = 0; i < 7; i++) d[i] = Wire.available() ? Wire.read() : 0;
  if ((d[0] & 0x0F) == 0) return 0;
  uint16_t rx = ((uint16_t)(d[1] & 0x0F) << 8) | d[2];
  uint16_t ry = ((uint16_t)(d[3] & 0x0F) << 8) | d[4];
  if (rx > 300 || ry > 360) return 0;               // 0x0FFF garbage guard
  long sx = map(rx, 20, 230, 0, 240);
  long sy = map(ry, 16, 306, 0, 320);
  *x = constrain(sx, 0, 239);
  *y = constrain(sy, 0, 319);
  return 1;
}
```

### 5d. SD card (SDIO via SD_MMC)

```cpp
#include "FS.h"
#include "SD_MMC.h"

bool sd_begin() {
  SD_MMC.setPins(38 /*CLK*/, 40 /*CMD*/, 39 /*D0*/, 41 /*D1*/, 48 /*D2*/, 47 /*D3*/);
  // 2nd arg true = 1-bit mode (frees 41/48/47); 3rd true = format if mount fails
  return SD_MMC.begin("/sdcard", true, true);
}
// then use SD_MMC.open(...), SD_MMC.exists(...), etc. (fs::FS API)
```

### 5e. RGB LED (WS2812 on GPIO 42)

```cpp
#include <Adafruit_NeoPixel.h>
Adafruit_NeoPixel rgb(1, 42, NEO_GRB + NEO_KHZ800);

void led_begin() { rgb.begin(); rgb.setBrightness(60); rgb.show(); }
void led_set(uint8_t r, uint8_t g, uint8_t b) {
  rgb.setPixelColor(0, rgb.Color(r, g, b)); rgb.show();
}
```

### 5f. Audio (ES8311) — outline

```
GPIO 1  -> LOW to enable codec + amp
I2S: MCLK=4, BCLK=5, WS/LRCK=7, DOUT(to spk)=8, DIN(from mic)=6
ES8311 on I2C 0x18 (SDA 16 / SCL 15, shared with touch)
Full 0x00-0x37 register init sequence: in the original working audio project,
based on Espressif's esp-adf ES8311 reference driver. Reuse it as-is; it is
confirmed producing sound on this hardware.
```

---

## 6. Confirmed working on this board

- Display + calibrated capacitive touch (LovyanGFX and TFT_eSPI both).
- Wi-Fi + BLE (ESP32 Marauder full feature set runs).
- SD read/write over SDIO.
- ES8311 audio output (tones/melodies).
- RGB LED.
- ESP32 Marauder port: branch `hosyond-s3-port` at
  `github.com/BruteSource/ESP32Marauder` (board id `MARAUDER_HOSYOND_S3`).
- Prior small apps: Open-Meteo weather display, whack-a-mole, endless-zoom
  Mandelbrot.

## 7. Physical-unit specifics (may differ on another unit)

- Touch calibration endpoints in §5c were measured on one physical unit. Re-run the
  four-corner tap calibration if fitting a different panel.
- FT6336 reports chip id `0x64` at register `0xA3`.
- The RGB LED responds to standard WS2812B GRB 800 kHz timing.
