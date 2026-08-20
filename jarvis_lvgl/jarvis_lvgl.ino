#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
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

// ===== LVGL FLUSH =====
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)color_p, w, h);
  lv_disp_flush_ready(disp);
}

// ===== COMPOSITE FONTS (Latin -> Devanagari -> Math) =====
static lv_font_t font_msg_14;
static lv_font_t font_msg_12;
static lv_font_t font_bar_12;
static lv_font_t fallback_deva_14;
static lv_font_t fallback_math_14;
static lv_font_t fallback_deva_12;

void setup_fonts() {
  memcpy(&fallback_math_14, &noto_math_14, sizeof(lv_font_t));
  memcpy(&fallback_deva_14, &noto_devanagari_14, sizeof(lv_font_t));
  fallback_deva_14.fallback = &fallback_math_14;
  memcpy(&font_msg_14, &noto_latin_14, sizeof(lv_font_t));
  font_msg_14.fallback = &fallback_deva_14;

  memcpy(&fallback_deva_12, &noto_devanagari_12, sizeof(lv_font_t));
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
  lv_label_set_text(wifi_hint, "Tap=prev | Hold=next | 2tap=connect");
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
  lv_label_set_text(hint, "Scroll: 1x hold | Select: triple tap");
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

  StaticJsonDocument<1024> doc;
  doc["model"] = GROQ_MODEL;
  doc["stream"] = true;
  JsonArray messages = doc.createNestedArray("messages");
  JsonObject sys = messages.createNestedObject();
  sys["role"] = "system";
  sys["content"] = "You are JARVIS, an advanced AI assistant created by Sahilpreet. Answer thoroughly and helpfully. You may use emojis, formulas, and detailed explanations as needed. Format math/physics expressions using plain text notation (e.g. F=ma, E=mc^2, v^2=u^2+2as). Respond in the same language the user writes in. Use ASCII art for simple diagrams when helpful.";
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

  if (chatCount < MAX_LINES) {
    chatLines[chatCount++] = streamBuffer;
  }

  Serial.println("\n[JARVIS] " + streamBuffer);
}

// ===== BUTTON HANDLER =====
// 1 tap = scroll DOWN, hold = scroll UP, 2 taps = select, hold 3s = settings
static bool lastBtnReading = HIGH;
static bool btnStable = HIGH;
static unsigned long btnDebounceTime = 0;
static unsigned long btnDownTime = 0;
static unsigned long lastTapTime = 0;
static int tapCount = 0;
static bool waitingForTaps = false;
static bool holdHandled = false;

#define BTN_DEBOUNCE     50
#define BTN_TAP_WINDOW   400
#define BTN_HOLD_TIME    300
#define BTN_LONG_HOLD    3000

void handleButton() {
  bool reading = digitalRead(BTN_PIN);
  unsigned long now = millis();

  if (reading != lastBtnReading) {
    btnDebounceTime = now;
  }
  lastBtnReading = reading;

  if ((now - btnDebounceTime) < BTN_DEBOUNCE) return;

  if (reading != btnStable) {
    btnStable = reading;

    if (reading == LOW) {
      btnDownTime = now;
      holdHandled = false;
    } else {
      unsigned long holdTime = now - btnDownTime;
      if (holdTime < BTN_HOLD_TIME) {
        tapCount++;
        lastTapTime = now;
        waitingForTaps = true;
      }
    }
  }

  if (waitingForTaps && !holdHandled && btnStable == LOW) {
    unsigned long heldFor = now - btnDownTime;

    if (heldFor >= BTN_LONG_HOLD) {
      holdHandled = true;
      waitingForTaps = false;
      tapCount = 0;
      if (currentScreen == SCR_MAIN) {
        switchToScreen(SCR_SETTINGS);
      }
    } else if (heldFor >= BTN_HOLD_TIME) {
      holdHandled = true;
      waitingForTaps = false;
      tapCount = 0;

      if (currentScreen == SCR_MAIN) {
        lv_obj_scroll_by(msg_list, 0, -50, LV_ANIM_ON);
      } else if (currentScreen == SCR_WIFI_SELECT) {
        wifiSelectedIdx++;
        if (wifiSelectedIdx >= wifiCount) wifiSelectedIdx = 0;
        updateWifiSelection();
      } else if (currentScreen == SCR_SETTINGS) {
        settingsIdx++;
        if (settingsIdx >= SETTINGS_COUNT) settingsIdx = 0;
        updateSettingsDisplay();
      }
    }
  }

  if (waitingForTaps && (now - lastTapTime > BTN_TAP_WINDOW)) {
    waitingForTaps = false;

    if (tapCount == 1) {
      if (currentScreen == SCR_MAIN) {
        lv_obj_scroll_by(msg_list, 0, 50, LV_ANIM_ON);
      } else if (currentScreen == SCR_WIFI_SELECT) {
        wifiSelectedIdx--;
        if (wifiSelectedIdx < 0) wifiSelectedIdx = wifiCount - 1;
        updateWifiSelection();
      } else if (currentScreen == SCR_SETTINGS) {
        settingsIdx--;
        if (settingsIdx < 0) settingsIdx = SETTINGS_COUNT - 1;
        updateSettingsDisplay();
      }
    } else if (tapCount >= 2) {
      if (currentScreen == SCR_WIFI_SELECT) {
        if (wifiCount > 0 && wifiSelectedIdx < wifiCount) {
          lv_label_set_text_fmt(wifi_title, ">> CONNECTING TO %s...", wifiSSIDs[wifiSelectedIdx].c_str());
          lv_refr_now(NULL);
          WiFi.disconnect();
          delay(200);
          WiFi.begin(wifiSSIDs[wifiSelectedIdx].c_str(), "");
          int tries = 0;
          while (WiFi.status() != WL_CONNECTED && tries < 30) { delay(500); tries++; }
          if (WiFi.status() == WL_CONNECTED) {
            lv_label_set_text_fmt(lbl_wifi, "WiFi: %s", WiFi.localIP().toString().c_str());
            lv_obj_set_style_text_color(lbl_wifi, lv_color_hex(0x22c55e), 0);
            rgb_green(); delay(500); rgb_off();
            switchToScreen(SCR_MAIN);
            add_sys_msg(("WiFi OK: " + WiFi.localIP().toString()).c_str());
            add_sys_msg("Type query in Serial Monitor...");
            lv_label_set_text(input_label, "Type in Serial Monitor...");
          } else {
            lv_label_set_text_fmt(wifi_title, ">> FAILED. Try another.");
          }
        }
      } else if (currentScreen == SCR_SETTINGS) {
        switch (settingsIdx) {
          case 0: switchToScreen(SCR_WIFI_SELECT); break;
          case 2:
            ledEnabled = !ledEnabled;
            if (!ledEnabled) rgb_off(); else rgb_cyan();
            updateSettingsDisplay();
            break;
          case 4: switchToScreen(SCR_MAIN); break;
        }
      } else if (currentScreen == SCR_MAIN) {
        switchToScreen(SCR_SETTINGS);
      }
    }
    tapCount = 0;
  }
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
  lv_obj_align(title, LV_ALIGN_CENTER, 0, -20);

  lv_obj_t *sub = lv_label_create(scr_splash);
  lv_label_set_text(sub, "ESP32-S3 AI Terminal");
  lv_obj_set_style_text_font(sub, &font_msg_12, 0);
  lv_obj_set_style_text_color(sub, lv_color_hex(0x9ca3af), 0);
  lv_obj_align(sub, LV_ALIGN_CENTER, 0, 8);

  lv_obj_t *ver = lv_label_create(scr_splash);
  lv_label_set_text(ver, "v1.0 | Powered by Groq");
  lv_obj_set_style_text_font(ver, &font_msg_12, 0);
  lv_obj_set_style_text_color(ver, lv_color_hex(0x6b7280), 0);
  lv_obj_align(ver, LV_ALIGN_CENTER, 0, 26);

  lv_refr_now(NULL);

  lv_refr_now(NULL);

  delay(2000);
}

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

  currentScreen = SCR_WIFI_SELECT;
  lv_scr_load(scr_wifi);
  lv_refr_now(NULL);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 20) {
    delay(500);
    tries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    lv_label_set_text_fmt(lbl_wifi, "WiFi: %s", WiFi.localIP().toString().c_str());
    lv_obj_set_style_text_color(lbl_wifi, lv_color_hex(0x22c55e), 0);
    switchToScreen(SCR_MAIN);
    add_sys_msg(("WiFi OK: " + WiFi.localIP().toString()).c_str());
    add_sys_msg((String("Model: ") + GROQ_MODEL).c_str());
    add_sys_msg("Type query in Serial Monitor...");
    add_sys_msg("Commands: !clear !wifi !settings !bright 50");
    lv_label_set_text(input_label, "Type in Serial Monitor...");
  } else {
    lv_label_set_text(lbl_wifi, "WiFi: None");
    lv_obj_set_style_text_color(lbl_wifi, lv_color_hex(0xef4444), 0);
    add_sys_msg("! WiFi failed - use !wifi or select below");
  }

  Serial.println("\n[JARVIS LVGL] Ready.");
  Serial.println("Commands: !clear !wifi !settings !bright <0-100>");
}

// ===== LOOP =====
void loop() {
  handleButton();
  handleSerialInput();
  lv_timer_handler();
  delay(2);
}
