#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include <Wire.h>
#include "fonts.h"

// ===== CONFIG =====
#define WIFI_SSID    "Net+"
#define WIFI_PASS    "123456789"
#define GROQ_API_KEY "YOUR_GROQ_API_KEY_HERE"
#define GROQ_MODEL   "openai/gpt-oss-20b"

// ===== PINS =====
#define TFT_BL    46
#define BTN_PIN    0
#define RGB_LED_PIN 38

// ===== DISPLAY (Arduino_GFX) =====
#define SCR_W 320
#define SCR_H 172

Arduino_DataBus *bus = new Arduino_ESP32SPI(41, 42, 40, 45);
Arduino_GFX *gfx = new Arduino_ST7789(
  bus, 39, 0, false,
  172, 320,
  34, 0, 34, 0);

// ===== LVGL =====
#define LV_BUF_LINES 40
static lv_disp_draw_buf_t disp_buf;
static lv_color_t buf1[SCR_W * LV_BUF_LINES];
static lv_color_t buf2[SCR_W * LV_BUF_LINES];
static lv_disp_drv_t disp_drv;

// ===== SCREENS =====
enum ScreenID { SCR_WIFI_SELECT, SCR_SPLASH, SCR_MAIN, SCR_SETTINGS };
static ScreenID currentScreen = SCR_WIFI_SELECT;

// ===== UI OBJECTS - MAIN =====
static lv_obj_t *scr_main = NULL;
static lv_obj_t *status_bar = NULL;
static lv_obj_t *lbl_wifi = NULL;
static lv_obj_t *lbl_model = NULL;
static lv_obj_t *msg_list = NULL;
static lv_obj_t *input_label = NULL;

// ===== UI OBJECTS - WIFI SELECT =====
static lv_obj_t *scr_wifi = NULL;
static lv_obj_t *wifi_list = NULL;
static lv_obj_t *wifi_title = NULL;
static lv_obj_t *wifi_hint = NULL;
static int wifiSelectedIdx = 0;
static int wifiCount = 0;
#define MAX_WIFI 20
String wifiSSIDs[MAX_WIFI];
int wifiRSSIs[MAX_WIFI];

// ===== UI OBJECTS - SETTINGS =====
static lv_obj_t *scr_settings = NULL;
static lv_obj_t *settings_list = NULL;
static lv_obj_t *settings_title = NULL;
#define SETTINGS_COUNT 5
static int settingsIdx = 0;

// ===== STYLES =====
static lv_style_t style_user;
static lv_style_t style_jarvis;
static lv_style_t style_sys;
static lv_style_t style_bar;
static lv_style_t style_wifi_item;
static lv_style_t style_wifi_selected;
static lv_style_t style_settings_item;
static lv_style_t style_settings_selected;
static lv_style_t style_title;

// ===== CHAT =====
#define MAX_LINES 100
String chatLines[MAX_LINES];
int chatCount = 0;

// ===== CONVERSATION HISTORY =====
#define MAX_HISTORY 10
String histRole[MAX_HISTORY];
String histContent[MAX_HISTORY];
int histCount = 0;

void histAdd(const char *role, const char *content) {
  if (histCount >= MAX_HISTORY) {
    for (int i = 0; i < MAX_HISTORY - 1; i++) {
      histRole[i] = histRole[i + 1];
      histContent[i] = histContent[i + 1];
    }
    histCount = MAX_HISTORY - 1;
  }
  histRole[histCount] = role;
  histContent[histCount] = content;
  histCount++;
}

// ===== STREAMING =====
bool isStreaming = false;
String streamBuffer = "";

// ===== BRIGHTNESS =====
int currentBrightness = 255;

// ===== LED MODE =====
bool ledEnabled = true;

// ===== RGB LED =====
void rgb_off() { if (ledEnabled) neopixelWrite(RGB_LED_PIN, 0, 0, 0); }
void rgb_blue() { if (ledEnabled) neopixelWrite(RGB_LED_PIN, 0, 0, 120); }
void rgb_red() { if (ledEnabled) neopixelWrite(RGB_LED_PIN, 255, 0, 0); }
void rgb_green() { if (ledEnabled) neopixelWrite(RGB_LED_PIN, 0, 255, 0); }
void rgb_cyan() { if (ledEnabled) neopixelWrite(RGB_LED_PIN, 0, 255, 255); }

void rgb_rainbow_cycle(int cycles) {
  for (int c = 0; c < cycles; c++) {
    for (int i = 0; i < 255; i++) {
      uint8_t r = 0, g = 0, b = 0;
      if (i < 85) { r = i * 3; g = 255 - i * 3; }
      else if (i < 170) { r = 255 - (i - 85) * 3; b = (i - 85) * 3; }
      else { g = (i - 170) * 3; b = 255 - (i - 170) * 3; }
      neopixelWrite(RGB_LED_PIN, r, g, b);
      delay(2);
    }
  }
  neopixelWrite(RGB_LED_PIN, 0, 0, 0);
}

// ===== QMI8658 IMU =====
#define QMI8658_ADDR       0x6B
#define QMI8658_WHO_AM_I   0x00
#define QMI8658_CTRL2      0x03
#define QMI8658_CTRL3      0x04
#define QMI8658_CTRL7      0x08
#define QMI8658_AX_L       0x35
#define QMI8658_STATUSINT  0x2D

#define IMU_SDA 48
#define IMU_SCL 47

bool imuOK = false;
uint8_t currentRotation = 1;
uint8_t pendingRotation = 1;
int rotStableCount = 0;
#define ROT_THRESHOLD 0.4
#define ROT_STABLE_N  5
#define ROT_CHECK_MS  200
unsigned long lastRotCheck = 0;

uint8_t qmi_read_reg(uint8_t reg) {
  Wire.beginTransmission(QMI8658_ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)QMI8658_ADDR, (uint8_t)1);
  return Wire.read();
}

void qmi_write_reg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(QMI8658_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

void qmi_init() {
  Wire.begin(IMU_SDA, IMU_SCL, 400000);
  delay(100);
  uint8_t who = qmi_read_reg(QMI8658_WHO_AM_I);
  Serial.printf("[IMU] WHO_AM_I = 0x%02X\n", who);
  if (who != 0x05) {
    Serial.println("[IMU] QMI8658 not found!");
    imuOK = false;
    return;
  }
  qmi_write_reg(QMI8658_CTRL7, 0x03);
  delay(50);
  uint8_t ctrl7 = qmi_read_reg(QMI8658_CTRL7);
  Serial.printf("[IMU] CTRL7 = 0x%02X (accel+gyro enabled)\n", ctrl7);
  imuOK = true;
}

void qmi_read_accel(float &ax, float &ay, float &az) {
  Wire.beginTransmission(QMI8658_ADDR);
  Wire.write(QMI8658_AX_L);
  Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)QMI8658_ADDR, (uint8_t)6);
  int16_t raw[3];
  for (int i = 0; i < 3; i++) {
    uint8_t lo = Wire.read();
    uint8_t hi = Wire.read();
    raw[i] = (int16_t)((hi << 8) | lo);
  }
  float scale = 4.0 / 32768.0;
  ax = raw[0] * scale;
  ay = raw[1] * scale;
  az = raw[2] * scale;
}

uint8_t calc_rotation(float ax, float ay, float az) {
  float absX = fabs(ax), absY = fabs(ay), absZ = fabs(az);
  if (absX > absY && absX > absZ) {
    return (ax > 0) ? 3 : 1;
  }
  return 1;
}

void check_auto_rotation() {
  if (!imuOK) return;
  unsigned long now = millis();
  if (now - lastRotCheck < ROT_CHECK_MS) return;
  lastRotCheck = now;

  float ax, ay, az;
  qmi_read_accel(ax, ay, az);
  uint8_t rot = calc_rotation(ax, ay, az);

  if (rot == pendingRotation) {
    rotStableCount++;
  } else {
    pendingRotation = rot;
    rotStableCount = 1;
  }

  if (rotStableCount >= ROT_STABLE_N && rot != currentRotation) {
    currentRotation = rot;
    rotStableCount = 0;
    Serial.printf("[IMU] Rotation -> %d\n", currentRotation);
    gfx->setRotation(currentRotation);
    lv_refr_now(NULL);
  }
}

// ===== LVGL FLUSH =====
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)color_p, w, h);
  lv_disp_flush_ready(disp);
}

// ===== COMPOSITE FONTS (Latin -> Devanagari -> Gurmukhi -> Math) =====
static lv_font_t font_msg_14;
static lv_font_t font_msg_12;
static lv_font_t font_bar_12;
static lv_font_t fallback_deva_14;
static lv_font_t fallback_gur_14;
static lv_font_t fallback_math_14;
static lv_font_t fallback_deva_12;
static lv_font_t fallback_gur_12;

void setup_fonts() {
  memcpy(&fallback_math_14, &noto_math_14, sizeof(lv_font_t));
  memcpy(&fallback_gur_14, &noto_gurmukhi_14, sizeof(lv_font_t));
  fallback_gur_14.fallback = &fallback_math_14;
  memcpy(&fallback_deva_14, &noto_devanagari_14, sizeof(lv_font_t));
  fallback_deva_14.fallback = &fallback_gur_14;
  memcpy(&font_msg_14, &noto_latin_14, sizeof(lv_font_t));
  font_msg_14.fallback = &fallback_deva_14;

  memcpy(&fallback_gur_12, &noto_gurmukhi_12, sizeof(lv_font_t));
  memcpy(&fallback_deva_12, &noto_devanagari_12, sizeof(lv_font_t));
  fallback_deva_12.fallback = &fallback_gur_12;
  memcpy(&font_msg_12, &noto_latin_12, sizeof(lv_font_t));
  font_msg_12.fallback = &fallback_deva_12;

  memcpy(&font_bar_12, &noto_latin_12, sizeof(lv_font_t));
  font_bar_12.fallback = &fallback_deva_12;
}

// ===== STYLES =====
void ui_init_styles() {
  lv_style_init(&style_bar);
  lv_style_set_bg_color(&style_bar, lv_color_hex(0x111827));
  lv_style_set_bg_opa(&style_bar, LV_OPA_COVER);
  lv_style_set_border_width(&style_bar, 0);
  lv_style_set_radius(&style_bar, 0);
  lv_style_set_text_color(&style_bar, lv_color_hex(0x60a5fa));
  lv_style_set_text_font(&style_bar, &font_bar_12);

  lv_style_init(&style_user);
  lv_style_set_bg_color(&style_user, lv_color_hex(0x0c1a2e));
  lv_style_set_bg_opa(&style_user, LV_OPA_COVER);
  lv_style_set_radius(&style_user, 6);
  lv_style_set_pad_all(&style_user, 6);
  lv_style_set_border_width(&style_user, 1);
  lv_style_set_border_color(&style_user, lv_color_hex(0x1e90ff));
  lv_style_set_border_opa(&style_user, LV_OPA_50);
  lv_style_set_text_color(&style_user, lv_color_hex(0x88ccff));
  lv_style_set_text_font(&style_user, &font_msg_14);

  lv_style_init(&style_jarvis);
  lv_style_set_bg_color(&style_jarvis, lv_color_hex(0x1a0c0c));
  lv_style_set_bg_opa(&style_jarvis, LV_OPA_COVER);
  lv_style_set_radius(&style_jarvis, 6);
  lv_style_set_pad_all(&style_jarvis, 6);
  lv_style_set_border_width(&style_jarvis, 1);
  lv_style_set_border_color(&style_jarvis, lv_color_hex(0xff4444));
  lv_style_set_border_opa(&style_jarvis, LV_OPA_50);
  lv_style_set_text_color(&style_jarvis, lv_color_hex(0xff8888));
  lv_style_set_text_font(&style_jarvis, &font_msg_14);

  lv_style_init(&style_sys);
  lv_style_set_text_color(&style_sys, lv_color_hex(0xfbbf24));
  lv_style_set_text_font(&style_sys, &font_msg_12);

  lv_style_init(&style_wifi_item);
  lv_style_set_bg_color(&style_wifi_item, lv_color_hex(0x111827));
  lv_style_set_bg_opa(&style_wifi_item, LV_OPA_COVER);
  lv_style_set_radius(&style_wifi_item, 4);
  lv_style_set_pad_all(&style_wifi_item, 4);
  lv_style_set_border_width(&style_wifi_item, 1);
  lv_style_set_border_color(&style_wifi_item, lv_color_hex(0x1e3a5f));
  lv_style_set_text_color(&style_wifi_item, lv_color_hex(0x9ca3af));
  lv_style_set_text_font(&style_wifi_item, &font_msg_14);

  lv_style_init(&style_wifi_selected);
  lv_style_set_bg_color(&style_wifi_selected, lv_color_hex(0x1e3a5f));
  lv_style_set_bg_opa(&style_wifi_selected, LV_OPA_COVER);
  lv_style_set_radius(&style_wifi_selected, 4);
  lv_style_set_pad_all(&style_wifi_selected, 4);
  lv_style_set_border_width(&style_wifi_selected, 2);
  lv_style_set_border_color(&style_wifi_selected, lv_color_hex(0x22c55e));
  lv_style_set_text_color(&style_wifi_selected, lv_color_hex(0x22c55e));
  lv_style_set_text_font(&style_wifi_selected, &font_msg_14);

  lv_style_init(&style_settings_item);
  lv_style_set_bg_color(&style_settings_item, lv_color_hex(0x111827));
  lv_style_set_bg_opa(&style_settings_item, LV_OPA_COVER);
  lv_style_set_radius(&style_settings_item, 4);
  lv_style_set_pad_all(&style_settings_item, 6);
  lv_style_set_border_width(&style_settings_item, 1);
  lv_style_set_border_color(&style_settings_item, lv_color_hex(0x1e3a5f));
  lv_style_set_text_color(&style_settings_item, lv_color_hex(0x9ca3af));
  lv_style_set_text_font(&style_settings_item, &font_msg_14);

  lv_style_init(&style_settings_selected);
  lv_style_set_bg_color(&style_settings_selected, lv_color_hex(0x1e3a5f));
  lv_style_set_bg_opa(&style_settings_selected, LV_OPA_COVER);
  lv_style_set_radius(&style_settings_selected, 4);
  lv_style_set_pad_all(&style_settings_selected, 6);
  lv_style_set_border_width(&style_settings_selected, 2);
  lv_style_set_border_color(&style_settings_selected, lv_color_hex(0xfbbf24));
  lv_style_set_text_color(&style_settings_selected, lv_color_hex(0xfbbf24));
  lv_style_set_text_font(&style_settings_selected, &font_msg_14);

  lv_style_init(&style_title);
  lv_style_set_text_color(&style_title, lv_color_hex(0x60a5fa));
  lv_style_set_text_font(&style_title, &font_msg_14);
}

// ===== WIFI SELECT SCREEN =====
static lv_obj_t *wifi_items[MAX_WIFI] = { NULL };

void ui_create_wifi_screen() {
  scr_wifi = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_wifi, lv_color_hex(0x0a0e17), 0);
  lv_obj_set_style_bg_opa(scr_wifi, LV_OPA_COVER, 0);

  wifi_title = lv_label_create(scr_wifi);
  lv_label_set_text(wifi_title, ">> SCANNING NETWORKS...");
  lv_obj_set_style_text_font(wifi_title, &font_msg_14, 0);
  lv_obj_set_style_text_color(wifi_title, lv_color_hex(0x60a5fa), 0);
  lv_obj_align(wifi_title, LV_ALIGN_TOP_MID, 0, 6);

  wifi_list = lv_obj_create(scr_wifi);
  lv_obj_set_size(wifi_list, SCR_W - 8, SCR_H - 50);
  lv_obj_align(wifi_list, LV_ALIGN_TOP_MID, 0, 28);
  lv_obj_set_style_bg_color(wifi_list, lv_color_hex(0x0a0e17), 0);
  lv_obj_set_style_bg_opa(wifi_list, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(wifi_list, 0, 0);
  lv_obj_set_style_pad_all(wifi_list, 2, 0);
  lv_obj_set_flex_flow(wifi_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(wifi_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(wifi_list, 3, 0);

  wifi_hint = lv_label_create(scr_wifi);
  lv_label_set_text(wifi_hint, "1tap=scan | Hold=next | 2hold=prev | 3hold=connect");
  lv_obj_set_style_text_font(wifi_hint, &font_msg_12, 0);
  lv_obj_set_style_text_color(wifi_hint, lv_color_hex(0x6b7280), 0);
  lv_obj_align(wifi_hint, LV_ALIGN_BOTTOM_MID, 0, -4);
}

void scanWiFiAndPopulate() {
  WiFi.scanDelete();
  wifiCount = WiFi.scanNetworks(false, true);
  if (wifiCount > MAX_WIFI) wifiCount = MAX_WIFI;

  lv_obj_clean(wifi_list);
  for (int i = 0; i < MAX_WIFI; i++) wifi_items[i] = NULL;

  if (wifiCount == 0) {
    lv_label_set_text(wifi_title, ">> NO NETWORKS FOUND");
    return;
  }

  lv_label_set_text_fmt(wifi_title, ">> %d NETWORKS FOUND", wifiCount);

  for (int i = 0; i < wifiCount; i++) {
    wifiSSIDs[i] = WiFi.SSID(i);
    wifiRSSIs[i] = WiFi.RSSI(i);

    lv_obj_t *item = lv_obj_create(wifi_list);
    lv_obj_set_size(item, SCR_W - 16, 24);
    lv_obj_set_height(item, 24);
    lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(item);
    String rssi_bar = "";
    int bars = map(wifiRSSIs[i], -100, -30, 1, 4);
    if (bars < 1) bars = 1;
    if (bars > 4) bars = 4;
    for (int b = 0; b < bars; b++) rssi_bar += "|";
    for (int b = bars; b < 4; b++) rssi_bar += ".";

    lv_label_set_text_fmt(lbl, "%s  %s  %d", rssi_bar.c_str(), wifiSSIDs[i].c_str(), wifiRSSIs[i]);
    lv_obj_set_style_text_font(lbl, &font_msg_14, 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 4, 0);

    if (i == wifiSelectedIdx) {
      lv_obj_add_style(item, &style_wifi_selected, 0);
      lv_obj_set_style_text_color(lbl, lv_color_hex(0x22c55e), 0);
    } else {
      lv_obj_add_style(item, &style_wifi_item, 0);
    }

    wifi_items[i] = item;
  }
}

void updateWifiSelection() {
  for (int i = 0; i < wifiCount; i++) {
    if (!wifi_items[i]) continue;
    lv_obj_t *lbl = lv_obj_get_child(wifi_items[i], 0);
    if (i == wifiSelectedIdx) {
      lv_obj_remove_style_all(wifi_items[i]);
      lv_obj_add_style(wifi_items[i], &style_wifi_selected, 0);
      lv_obj_set_style_text_color(lbl, lv_color_hex(0x22c55e), 0);
    } else {
      lv_obj_remove_style_all(wifi_items[i]);
      lv_obj_add_style(wifi_items[i], &style_wifi_item, 0);
      lv_obj_set_style_text_color(lbl, lv_color_hex(0x9ca3af), 0);
    }
  }
}

// ===== MAIN CHAT SCREEN =====
void ui_create() {
  scr_main = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_main, lv_color_hex(0x0a0e17), 0);
  lv_obj_set_style_bg_opa(scr_main, LV_OPA_COVER, 0);

  status_bar = lv_obj_create(scr_main);
  lv_obj_set_size(status_bar, SCR_W, 22);
  lv_obj_align(status_bar, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_add_style(status_bar, &style_bar, 0);
  lv_obj_set_flex_flow(status_bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(status_bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_hor(status_bar, 6, 0);
  lv_obj_set_style_pad_ver(status_bar, 0, 0);
  lv_obj_clear_flag(status_bar, LV_OBJ_FLAG_SCROLLABLE);

  lbl_wifi = lv_label_create(status_bar);
  lv_label_set_text(lbl_wifi, "WiFi: None");
  lv_obj_set_style_text_color(lbl_wifi, lv_color_hex(0x60a5fa), 0);

  lbl_model = lv_label_create(status_bar);
  lv_label_set_text(lbl_model, GROQ_MODEL);
  lv_obj_set_style_text_color(lbl_model, lv_color_hex(0x9ca3af), 0);
  lv_obj_set_style_text_font(lbl_model, &font_bar_12, 0);

  msg_list = lv_obj_create(scr_main);
  lv_obj_set_size(msg_list, SCR_W, SCR_H - 22 - 20);
  lv_obj_align(msg_list, LV_ALIGN_TOP_MID, 0, 22);
  lv_obj_set_style_bg_color(msg_list, lv_color_hex(0x0a0e17), 0);
  lv_obj_set_style_bg_opa(msg_list, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(msg_list, 0, 0);
  lv_obj_set_style_pad_all(msg_list, 4, 0);
  lv_obj_set_flex_flow(msg_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(msg_list, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  input_label = lv_label_create(scr_main);
  lv_obj_set_size(input_label, SCR_W - 8, 18);
  lv_obj_align(input_label, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_label_set_text(input_label, "");
  lv_obj_set_style_text_color(input_label, lv_color_hex(0x22c55e), 0);
  lv_obj_set_style_text_font(input_label, &font_msg_12, 0);
  lv_obj_set_style_bg_color(input_label, lv_color_hex(0x111827), 0);
  lv_obj_set_style_bg_opa(input_label, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_hor(input_label, 4, 0);
}

// ===== SETTINGS SCREEN =====
static lv_obj_t *settings_items[SETTINGS_COUNT] = { NULL };
static lv_obj_t *settings_values[SETTINGS_COUNT] = { NULL };
const char *settingsLabels[SETTINGS_COUNT] = {
  "WiFi Network",
  "Brightness",
  "LED Toggle",
  "About JARVIS",
  "Back to Chat"
};

void ui_create_settings() {
  scr_settings = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_settings, lv_color_hex(0x0a0e17), 0);
  lv_obj_set_style_bg_opa(scr_settings, LV_OPA_COVER, 0);

  settings_title = lv_label_create(scr_settings);
  lv_label_set_text(settings_title, ">> SETTINGS");
  lv_obj_set_style_text_font(settings_title, &font_msg_14, 0);
  lv_obj_set_style_text_color(settings_title, lv_color_hex(0x60a5fa), 0);
  lv_obj_align(settings_title, LV_ALIGN_TOP_MID, 0, 6);

  settings_list = lv_obj_create(scr_settings);
  lv_obj_set_size(settings_list, SCR_W - 12, SCR_H - 46);
  lv_obj_align(settings_list, LV_ALIGN_TOP_MID, 0, 28);
  lv_obj_set_style_bg_color(settings_list, lv_color_hex(0x0a0e17), 0);
  lv_obj_set_style_bg_opa(settings_list, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(settings_list, 0, 0);
  lv_obj_set_style_pad_all(settings_list, 2, 0);
  lv_obj_set_flex_flow(settings_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(settings_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(settings_list, 4, 0);

  for (int i = 0; i < SETTINGS_COUNT; i++) {
    lv_obj_t *item = lv_obj_create(settings_list);
    lv_obj_set_size(item, SCR_W - 20, 26);
    lv_obj_set_height(item, 26);
    lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(item);
    lv_label_set_text(lbl, settingsLabels[i]);
    lv_obj_set_style_text_font(lbl, &font_msg_14, 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 4, 0);

    lv_obj_t *val = lv_label_create(item);
    lv_obj_set_style_text_font(val, &font_msg_12, 0);
    lv_obj_align(val, LV_ALIGN_RIGHT_MID, -4, 0);

    if (i == settingsIdx) {
      lv_obj_add_style(item, &style_settings_selected, 0);
      lv_obj_set_style_text_color(lbl, lv_color_hex(0xfbbf24), 0);
    } else {
      lv_obj_add_style(item, &style_settings_item, 0);
    }
    settings_items[i] = item;
    settings_values[i] = val;
  }

  lv_obj_t *hint = lv_label_create(scr_settings);
  lv_label_set_text(hint, "1tap=next | 2tap=back | 3tap=select");
  lv_obj_set_style_text_font(hint, &font_msg_12, 0);
  lv_obj_set_style_text_color(hint, lv_color_hex(0x6b7280), 0);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -4);
}

void updateSettingsDisplay() {
  char buf[64];

  snprintf(buf, sizeof(buf), "%s", WiFi.SSID().length() > 0 ? WiFi.SSID().c_str() : "None");
  lv_label_set_text(settings_values[0], buf);

  snprintf(buf, sizeof(buf), "%d%%", map(currentBrightness, 0, 255, 0, 100));
  lv_label_set_text(settings_values[1], buf);

  lv_label_set_text(settings_values[2], ledEnabled ? "ON" : "OFF");

  snprintf(buf, sizeof(buf), "v1.0");
  lv_label_set_text(settings_values[3], buf);

  lv_label_set_text(settings_values[4], "<");

  for (int i = 0; i < SETTINGS_COUNT; i++) {
    if (!settings_items[i]) continue;
    lv_obj_t *lbl = lv_obj_get_child(settings_items[i], 0);
    lv_obj_remove_style_all(settings_items[i]);
    if (i == settingsIdx) {
      lv_obj_add_style(settings_items[i], &style_settings_selected, 0);
      lv_obj_set_style_text_color(lbl, lv_color_hex(0xfbbf24), 0);
    } else {
      lv_obj_add_style(settings_items[i], &style_settings_item, 0);
      lv_obj_set_style_text_color(lbl, lv_color_hex(0x9ca3af), 0);
    }
  }
}

// ===== CHAT DISPLAY =====
void switchToScreen(ScreenID id) {
  currentScreen = id;
  switch (id) {
    case SCR_WIFI_SELECT:
      lv_scr_load(scr_wifi);
      scanWiFiAndPopulate();
      break;
    case SCR_MAIN:
      lv_scr_load(scr_main);
      break;
    case SCR_SETTINGS:
      settingsIdx = 0;
      updateSettingsDisplay();
      lv_scr_load(scr_settings);
      break;
    case SCR_SPLASH:
      lv_scr_load(scr_main);
      break;
  }
}

void add_msg_to_ui(const char *prefix, const char *text, lv_style_t *style) {
  lv_obj_t *container = lv_obj_create(msg_list);
  lv_obj_set_width(container, SCR_W - 12);
  lv_obj_set_height(container, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(container, 0, 0);
  lv_obj_set_style_pad_all(container, 0, 0);
  lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);

  if (prefix && strlen(prefix) > 0) {
    lv_obj_t *pfx = lv_label_create(container);
    lv_label_set_text(pfx, prefix);
    lv_obj_set_style_text_font(pfx, &font_msg_12, 0);
    lv_obj_set_style_text_color(pfx, lv_color_hex(0x60a5fa), 0);
  }

  lv_obj_t *lbl = lv_label_create(container);
  lv_label_set_text(lbl, text);
  lv_obj_add_style(lbl, style, 0);
  lv_obj_set_width(lbl, SCR_W - 20);
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_pad_top(lbl, 1, 0);

  lv_obj_scroll_to_y(msg_list, LV_COORD_MAX, LV_ANIM_ON);
}

void add_user_msg(const char *text) {
  add_msg_to_ui("[YOU] ", text, &style_user);
}

void add_jarvis_msg(const char *text) {
  add_msg_to_ui("[JARVIS] ", text, &style_jarvis);
}

void add_sys_msg(const char *text) {
  lv_obj_t *lbl = lv_label_create(msg_list);
  lv_label_set_text(lbl, text);
  lv_obj_add_style(lbl, &style_sys, 0);
  lv_obj_set_width(lbl, SCR_W - 16);
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
}

// ===== GROQ API =====
void sendToGroq(String query) {
  if (WiFi.status() != WL_CONNECTED) {
    add_sys_msg("! WiFi not connected");
    return;
  }

  HTTPClient http;
  http.begin("https://api.groq.com/openai/v1/chat/completions");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + String(GROQ_API_KEY));

  StaticJsonDocument<4096> doc;
  doc["model"] = GROQ_MODEL;
  doc["stream"] = true;
  JsonArray messages = doc.createNestedArray("messages");
  JsonObject sys = messages.createNestedObject();
  sys["role"] = "system";
  sys["content"] = "You are JARVIS, an AI assistant by Sahilpreet. Be concise — answer only what's asked, no filler, no unnecessary explanations. Use short paragraphs. Only go detailed when the user explicitly asks for it. Respond in the user's language. Use plain text math notation (F=ma, E=mc^2). Use emojis sparingly.";
  for (int i = 0; i < histCount; i++) {
    JsonObject msg = messages.createNestedObject();
    msg["role"] = histRole[i];
    msg["content"] = histContent[i];
  }
  JsonObject user = messages.createNestedObject();
  user["role"] = "user";
  user["content"] = query.c_str();

  String payload;
  serializeJson(doc, payload);

  int code = http.POST(payload);
  if (code != 200) {
    add_sys_msg(("HTTP " + String(code) + ": " + http.getString()).c_str());
    http.end();
    return;
  }

  isStreaming = true;
  streamBuffer = "";
  rgb_blue();

  histAdd("user", query.c_str());

  lv_obj_t *container = lv_obj_create(msg_list);
  lv_obj_set_width(container, SCR_W - 12);
  lv_obj_set_height(container, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(container, 0, 0);
  lv_obj_set_style_pad_all(container, 0, 0);
  lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);

  lv_obj_t *pfx = lv_label_create(container);
  lv_label_set_text(pfx, ">> JARVIS");
  lv_obj_set_style_text_font(pfx, &font_msg_12, 0);
  lv_obj_set_style_text_color(pfx, lv_color_hex(0x60a5fa), 0);

  lv_obj_t *lbl = lv_label_create(container);
  lv_label_set_text(lbl, "...");
  lv_obj_set_style_text_color(lbl, lv_color_hex(0xff8888), 0);
  lv_obj_set_style_text_font(lbl, &font_msg_14, 0);
  lv_obj_set_width(lbl, SCR_W - 20);
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);

  lv_obj_scroll_to_y(msg_list, LV_COORD_MAX, LV_ANIM_ON);

  WiFiClient *s = http.getStreamPtr();
  StaticJsonDocument<384> resp;
  String buf = "";

  while (http.connected() || s->available()) {
    if (s->available()) {
      char c = s->read();
      buf += c;
      if (c == '\n') {
        buf.trim();
        if (buf.startsWith("data: ")) {
          String json = buf.substring(6);
          if (json == "[DONE]") break;

          DeserializationError err = deserializeJson(resp, json);
          if (!err && resp["choices"][0]["delta"].containsKey("content")) {
            String token = resp["choices"][0]["delta"]["content"].as<String>();
            streamBuffer += token;

            lv_label_set_text(lbl, streamBuffer.c_str());
            lv_obj_scroll_to_y(msg_list, LV_COORD_MAX, LV_ANIM_ON);
            lv_refr_now(NULL);

            if (ledEnabled) {
              static uint8_t pulse = 0;
              pulse = (pulse + 15) % 256;
              uint8_t bri = 40 + (pulse > 128 ? 255 - pulse : pulse);
              neopixelWrite(RGB_LED_PIN, 0, 0, bri);
            }
          }
        }
        buf = "";
      }
    }
    lv_timer_handler();
    yield();
  }

  http.end();
  isStreaming = false;
  rgb_off();

  histAdd("assistant", streamBuffer.c_str());

  if (chatCount < MAX_LINES) {
    chatLines[chatCount++] = streamBuffer;
  }

  Serial.println("\n[JARVIS] " + streamBuffer);
}

// ===== BUTTON HANDLER =====
static int btnState = HIGH, lastBtnState = HIGH;
static unsigned long btnPressTime = 0, lastPressTime = 0;
static unsigned long lastScrollTime = 0;
static int pressCount = 0;
static bool btnHeld = false;

void handleButton() {
  int reading = digitalRead(BTN_PIN);
  unsigned long now = millis();

  if (reading != lastBtnState) {
    if (reading == LOW) {
      btnPressTime = now;
      pressCount++;
      lastPressTime = now;
    } else {
      unsigned long holdTime = now - btnPressTime;
      if (holdTime >= 500 && pressCount == 1) {
        if (currentScreen == SCR_MAIN) lv_obj_scroll_by(msg_list, 0, 40, LV_ANIM_ON);
        else if (currentScreen == SCR_WIFI_SELECT) { wifiSelectedIdx++; if (wifiSelectedIdx >= wifiCount) wifiSelectedIdx = 0; updateWifiSelection(); }
        else if (currentScreen == SCR_SETTINGS) { settingsIdx++; if (settingsIdx >= SETTINGS_COUNT) settingsIdx = 0; updateSettingsDisplay(); }
      } else if (holdTime >= 500 && pressCount >= 2) {
        if (currentScreen == SCR_MAIN) lv_obj_scroll_by(msg_list, 0, -40, LV_ANIM_ON);
        else if (currentScreen == SCR_WIFI_SELECT) { wifiSelectedIdx--; if (wifiSelectedIdx < 0) wifiSelectedIdx = wifiCount - 1; updateWifiSelection(); }
        else if (currentScreen == SCR_SETTINGS) { settingsIdx--; if (settingsIdx < 0) settingsIdx = SETTINGS_COUNT - 1; updateSettingsDisplay(); }
      } else if (holdTime < 300 && pressCount >= 3) {
        if (currentScreen == SCR_MAIN) switchToScreen(SCR_SETTINGS);
        else if (currentScreen == SCR_SETTINGS) {
          switch (settingsIdx) {
            case 0: switchToScreen(SCR_WIFI_SELECT); break;
            case 2: ledEnabled = !ledEnabled; if (!ledEnabled) rgb_off(); else rgb_cyan(); updateSettingsDisplay(); break;
            case 4: switchToScreen(SCR_MAIN); break;
          }
        }
        pressCount = 0;
      }
      btnHeld = false;
    }
  }

  if (btnState == LOW && (now - btnPressTime > 500)) {
    if (!btnHeld) {
      btnHeld = true;
      lastScrollTime = 0;
    }
    if (now - lastScrollTime > 60) {
      lastScrollTime = now;
      if (currentScreen == SCR_MAIN) {
        if (pressCount == 1) lv_obj_scroll_by(msg_list, 0, 15, LV_ANIM_ON);
        else if (pressCount >= 2) lv_obj_scroll_by(msg_list, 0, -15, LV_ANIM_ON);
      } else if (currentScreen == SCR_WIFI_SELECT) {
        if (pressCount == 1) { wifiSelectedIdx++; if (wifiSelectedIdx >= wifiCount) wifiSelectedIdx = 0; updateWifiSelection(); }
        else if (pressCount >= 2) { wifiSelectedIdx--; if (wifiSelectedIdx < 0) wifiSelectedIdx = wifiCount - 1; updateWifiSelection(); }
      } else if (currentScreen == SCR_SETTINGS) {
        if (pressCount == 1) { settingsIdx++; if (settingsIdx >= SETTINGS_COUNT) settingsIdx = 0; updateSettingsDisplay(); }
        else if (pressCount >= 2) { settingsIdx--; if (settingsIdx < 0) settingsIdx = SETTINGS_COUNT - 1; updateSettingsDisplay(); }
      }
    }
  }

  if (now - lastPressTime > 1000 && pressCount > 0) pressCount = 0;
  lastBtnState = reading;
  btnState = reading;
}

// ===== SERIAL INPUT =====
String serialInput = "";

void handleSerialInput() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (serialInput.length() > 0) {
        serialInput.trim();
        if (serialInput == "!clear") {
          lv_obj_clean(msg_list);
          chatCount = 0;
          histCount = 0;
          add_sys_msg("Chat cleared.");
        } else if (serialInput == "!wifi") {
          switchToScreen(SCR_WIFI_SELECT);
        } else if (serialInput == "!settings") {
          switchToScreen(SCR_SETTINGS);
        } else if (serialInput.startsWith("!bright ")) {
          int val = serialInput.substring(8).toInt();
          if (val >= 0 && val <= 100) {
            currentBrightness = map(val, 0, 100, 0, 255);
            add_sys_msg(("Brightness: " + String(val) + "%").c_str());
          }
        } else {
          if (currentScreen != SCR_MAIN) switchToScreen(SCR_MAIN);
          add_user_msg(serialInput.c_str());
          lv_label_set_text(input_label, ("> " + serialInput).c_str());
          sendToGroq(serialInput);
          lv_label_set_text(input_label, "Type in Serial Monitor...");
        }
        serialInput = "";
      }
    } else {
      serialInput += c;
    }
  }
}

// ===== JARVIS LOGO SPLASH =====
static lv_obj_t *scr_splash = NULL;

void showSplash() {
  scr_splash = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_splash, lv_color_hex(0x0a0e17), 0);
  lv_obj_set_style_bg_opa(scr_splash, LV_OPA_COVER, 0);
  lv_scr_load(scr_splash);

  lv_obj_t *title = lv_label_create(scr_splash);
  lv_label_set_text(title, "J A R V I S");
  lv_obj_set_style_text_font(title, &noto_latin_14, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0x60a5fa), 0);
  lv_obj_align(title, LV_ALIGN_CENTER, 0, -30);

  lv_obj_t *sub = lv_label_create(scr_splash);
  lv_label_set_text(sub, "ESP32-S3 AI Terminal");
  lv_obj_set_style_text_font(sub, &font_msg_12, 0);
  lv_obj_set_style_text_color(sub, lv_color_hex(0x9ca3af), 0);
  lv_obj_align(sub, LV_ALIGN_CENTER, 0, -6);

  lv_obj_t *ver = lv_label_create(scr_splash);
  lv_label_set_text(ver, "v1.0 | Powered by Groq");
  lv_obj_set_style_text_font(ver, &font_msg_12, 0);
  lv_obj_set_style_text_color(ver, lv_color_hex(0x6b7280), 0);
  lv_obj_align(ver, LV_ALIGN_CENTER, 0, 10);

  lv_obj_t *loadtxt = lv_label_create(scr_splash);
  lv_label_set_text(loadtxt, "Initializing...");
  lv_obj_set_style_text_font(loadtxt, &font_msg_12, 0);
  lv_obj_set_style_text_color(loadtxt, lv_color_hex(0x22c55e), 0);
  lv_obj_align(loadtxt, LV_ALIGN_CENTER, 0, 30);

  lv_refr_now(NULL);
  delay(2500);
}

// ===== WIFI CONNECT STATE =====
static bool wifiConnecting = false;
static unsigned long wifiStartMs = 0;
#define WIFI_TIMEOUT_MS 15000

// ===== SETUP =====
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n[JARVIS] Booting...");

  pinMode(BTN_PIN, INPUT_PULLUP);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  gfx->begin();
  gfx->setRotation(1);

  neopixelWrite(RGB_LED_PIN, 0, 0, 0);
  rgb_rainbow_cycle(2);

  qmi_init();

  lv_init();
  lv_disp_draw_buf_init(&disp_buf, buf1, buf2, SCR_W * LV_BUF_LINES);

  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = SCR_W;
  disp_drv.ver_res = SCR_H;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &disp_buf;
  lv_disp_drv_register(&disp_drv);

  setup_fonts();
  ui_init_styles();
  ui_create_wifi_screen();
  ui_create();
  ui_create_settings();

  showSplash();

  currentScreen = SCR_MAIN;
  lv_scr_load(scr_main);
  lv_refr_now(NULL);

  add_sys_msg("JARVIS v1.0 online.");
  add_sys_msg("Connecting to WiFi...");
  lv_label_set_text(input_label, "Connecting...");

  Serial.println("[JARVIS] Starting WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  wifiConnecting = true;
  wifiStartMs = millis();

  Serial.println("[JARVIS] Ready. Type messages below.");
  Serial.println("Commands: !clear !wifi !settings !bright <0-100>");
}

// ===== LOOP =====
void loop() {
  handleButton();
  handleSerialInput();
  check_auto_rotation();
  lv_timer_handler();

  if (wifiConnecting) {
    if (WiFi.status() == WL_CONNECTED) {
      wifiConnecting = false;
      lv_label_set_text_fmt(lbl_wifi, "WiFi: %s", WiFi.localIP().toString().c_str());
      lv_obj_set_style_text_color(lbl_wifi, lv_color_hex(0x22c55e), 0);
      rgb_green(); delay(300); rgb_off();
      add_sys_msg(("WiFi connected: " + WiFi.localIP().toString()).c_str());
      add_sys_msg("Type query in Serial Monitor...");
      lv_label_set_text(input_label, "Type in Serial Monitor...");
      Serial.println("[JARVIS] WiFi connected: " + WiFi.localIP().toString());
    } else if (millis() - wifiStartMs > WIFI_TIMEOUT_MS) {
      wifiConnecting = false;
      lv_label_set_text(lbl_wifi, "WiFi: Failed");
      lv_obj_set_style_text_color(lbl_wifi, lv_color_hex(0xef4444), 0);
      add_sys_msg("! WiFi connect failed. Use button to scan.");
      lv_label_set_text(input_label, "Use button to scan WiFi");
      Serial.println("[JARVIS] WiFi connection timed out.");
    } else {
      static unsigned long lastBlink = 0;
      if (millis() - lastBlink > 500) {
        lastBlink = millis();
        lv_label_set_text(input_label, "Connecting...");
      }
    }
  }

  delay(2);
}
