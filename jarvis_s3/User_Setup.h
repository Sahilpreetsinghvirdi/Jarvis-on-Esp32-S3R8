// User_Setup.h for Waveshare ESP32-S3-LCD-1.47B Type B
// ST7789 SPI display configuration for TFT_eSPI

#ifndef USER_SETUP_H
#define USER_SETUP_H

// ST7789 driver
#define ST7789_DRIVER

// Display dimensions (1.47" = 172x320 native, rotation 3 gives 320x172)
#define TFT_WIDTH  172
#define TFT_HEIGHT 320

// SPI pins for Waveshare ESP32-S3-LCD-1.47B Type B
#define TFT_MOSI 45
#define TFT_SCLK 40
#define TFT_CS   42
#define TFT_DC   41
#define TFT_RST  39
// Backlight not controlled here (code handles it)

// Use hardware SPI (VSPI on ESP32-S3)
#define USE_HSPI_PORT

// SPI frequency
#define SPI_FREQUENCY  40000000
#define SPI_READ_FREQUENCY  20000000

// Fonts
#define LOAD_GLCD   // Font 1. Original Adafruit 8 pixel font needs ~1820 bytes in FLASH
#define LOAD_FONT2  // Font 2. Small 16 pixel high font, needs ~3534 bytes in FLASH, 96 bytes RAM
#define LOAD_FONT4  // Font 4. Medium 26 pixel high font, needs ~5848 bytes in FLASH, 96 bytes RAM
#define LOAD_FONT6  // Font 6. Large 48 pixel font, needs ~2666 bytes in FLASH, 96 bytes RAM
#define LOAD_FONT7  // Font 7. 7 segment 48 pixel, needs ~2438 bytes in FLASH, 96 bytes RAM
#define LOAD_FONT8  // Font 8. Large 75 pixel font needs ~3256 bytes in FLASH, 96 bytes RAM
#define LOAD_GFXFF  // FreeFonts. Include access to the 48 Adafruit_GFX free fonts FF1-FF48 and custom fonts

#define SMOOTH_FONT

#endif