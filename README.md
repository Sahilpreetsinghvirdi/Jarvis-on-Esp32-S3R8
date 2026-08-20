# JARVIS on ESP32-S3-R8

AI-powered JARVIS terminal on **Waveshare ESP32-S3-LCD-1.47B Type B** (ESP32-S3 N16R8).

## Hardware
- **Board**: Waveshare ESP32-S3-LCD-1.47B Type B
- **Display**: ST7789 SPI 172x320 (1.47")
- **RGB LED**: WS2812 on GPIO 38
- **Boot Button**: GPIO 0 (used for scroll)

### Pin Mapping
| Signal | GPIO |
|--------|------|
| MOSI   | 45   |
| SCLK   | 40   |
| CS     | 42   |
| DC     | 41   |
| RST    | 39   |
| BL     | 46   |
| RGB    | 38   |
| BTN    | 0    |

## Features
- **Groq API** streaming with `openai/gpt-oss-20b`
- **LVGL UI** with styled message bubbles (Arduino_GFX driver)
- **RGB LED** -- rainbow boot, blue pulse during AI streaming
- **Button scroll** -- single press+hold = UP, double press+hold = DOWN
- **Serial Monitor** input for queries

## Variants
### `jarvis_s3/` -- TFT_eSPI version
- Terminal-style grid rendering
- Typewriter word-by-word streaming
- Splash image on boot

### `jarvis_lvgl/` -- LVGL version
- GUI with status bar, scrollable message list, styled bubbles
- User messages (cyan), JARVIS messages (red), system messages (yellow)
- Real-time streaming text updates

## Setup
1. Install [Arduino CLI](https://arduino.github.io/arduino-cli/)
2. Install ESP32 core: `arduino-cli core install esp32:esp32`
3. Clone libraries to `ArduinoLibraries/`:
   - [LVGL v8.3](https://github.com/lvgl/lvgl) + `lv_conf.h`
   - [Arduino_GFX](https://github.com/moononournation/Arduino_GFX)
   - [ArduinoJson](https://github.com/bblanchon/ArduinoJson)
4. Compile with custom partition scheme (16MB flash, 8MB PSRAM)

## WiFi & API
Edit `WIFI_SSID`, `WIFI_PASS`, and `GROQ_API_KEY` in the sketch. Get a free Groq API key at [console.groq.com](https://console.groq.com).
