#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>

// ===== CONFIG =====
#define WIFI_SSID    "Net+"
#define WIFI_PASS    "123456789"
#define GROQ_API_KEY "YOUR_GROQ_API_KEY_HERE"
#define GROQ_MODEL   "openai/gpt-oss-20b"

// ===== PINS =====
#define TFT_BL   46
#define BTN_PIN   0
#define RGB_LED_PIN 38

// ===== DISPLAY (Arduino_GFX) =====
#define SCR_W 320
#define SCR_H 172

Arduino_DataBus *bus = new Arduino_ESP32SPI(41 /* DC */, 42 /* CS */, 40 /* SCK */, 45 /* MOSI */);
Arduino_GFX *gfx = new Arduino_ST7789(
  bus, 39 /* RST */, 0 /* rotation */, false /* IPS */,
  172 /* width */, 320 /* height */,
  34 /*col_offset1*/, 0 /*row_offset1*/,
  34 /*col_offset2*/, 0 /*row_offset2*/);

// ===== LVGL =====
#define LV_BUF_LINES 40
static lv_disp_draw_buf_t disp_buf;
static lv_color_t buf1[SCR_W * LV_BUF_LINES];
static lv_color_t buf2[SCR_W * LV_BUF_LINES];
static lv_disp_drv_t disp_drv;

// ===== UI OBJECTS =====
static lv_obj_t *scr_main       = NULL;
static lv_obj_t *status_bar     = NULL;
static lv_obj_t *lbl_wifi       = NULL;
static lv_obj_t *lbl_model      = NULL;
static lv_obj_t *msg_list       = NULL;
static lv_obj_t *typing_label   = NULL;
static lv_obj_t *input_label    = NULL;
static lv_style_t style_user;
static lv_style_t style_jarvis;
static lv_style_t style_sys;
static lv_style_t style_bar;

// ===== CHAT HISTORY =====
#define MAX_LINES 100
String chatLines[MAX_LINES];
int chatCount = 0;

// ===== STREAMING =====
bool isStreaming = false;
String streamBuffer = "";

// ===== RGB LED =====
void rgb_off() { neopixelWrite(RGB_LED_PIN, 0, 0, 0); }
void rgb_blue() { neopixelWrite(RGB_LED_PIN, 0, 0, 120); }
void rgb_red() { neopixelWrite(RGB_LED_PIN, 255, 0, 0); }
void rgb_green() { neopixelWrite(RGB_LED_PIN, 0, 255, 0); }
void rgb_cyan() { neopixelWrite(RGB_LED_PIN, 0, 255, 255); }

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

// ===== LVGL DISPLAY FLUSH =====
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)color_p, w, h);
  lv_disp_flush_ready(disp);
}

// ===== UI STYLES =====
void ui_init_styles() {
  lv_style_init(&style_bar);
  lv_style_set_bg_color(&style_bar, lv_color_hex(0x111827));
  lv_style_set_bg_opa(&style_bar, LV_OPA_COVER);
  lv_style_set_border_width(&style_bar, 0);
  lv_style_set_radius(&style_bar, 0);
  lv_style_set_text_color(&style_bar, lv_color_hex(0x60a5fa));
  lv_style_set_text_font(&style_bar, &lv_font_montserrat_12);

  lv_style_init(&style_user);
  lv_style_set_bg_color(&style_user, lv_color_hex(0x0c1a2e));
  lv_style_set_bg_opa(&style_user, LV_OPA_COVER);
  lv_style_set_radius(&style_user, 6);
  lv_style_set_pad_all(&style_user, 6);
  lv_style_set_border_width(&style_user, 1);
  lv_style_set_border_color(&style_user, lv_color_hex(0x1e90ff));
  lv_style_set_border_opa(&style_user, LV_OPA_50);
  lv_style_set_text_color(&style_user, lv_color_hex(0x88ccff));
  lv_style_set_text_font(&style_user, &lv_font_montserrat_14);

  lv_style_init(&style_jarvis);
  lv_style_set_bg_color(&style_jarvis, lv_color_hex(0x1a0c0c));
  lv_style_set_bg_opa(&style_jarvis, LV_OPA_COVER);
  lv_style_set_radius(&style_jarvis, 6);
  lv_style_set_pad_all(&style_jarvis, 6);
  lv_style_set_border_width(&style_jarvis, 1);
  lv_style_set_border_color(&style_jarvis, lv_color_hex(0xff4444));
  lv_style_set_border_opa(&style_jarvis, LV_OPA_50);
  lv_style_set_text_color(&style_jarvis, lv_color_hex(0xff8888));
  lv_style_set_text_font(&style_jarvis, &lv_font_montserrat_14);

  lv_style_init(&style_sys);
  lv_style_set_text_color(&style_sys, lv_color_hex(0xfbbf24));
  lv_style_set_text_font(&style_sys, &lv_font_montserrat_12);
}

// ===== UI CREATE =====
void ui_create() {
  scr_main = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_main, lv_color_hex(0x0a0e17), 0);
  lv_obj_set_style_bg_opa(scr_main, LV_OPA_COVER, 0);
  lv_scr_load(scr_main);

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
  lv_obj_set_style_text_font(lbl_model, &lv_font_montserrat_12, 0);

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
  lv_obj_set_style_text_font(input_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_bg_color(input_label, lv_color_hex(0x111827), 0);
  lv_obj_set_style_bg_opa(input_label, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_hor(input_label, 4, 0);

  typing_label = lv_label_create(msg_list);
  lv_label_set_text(typing_label, "");
  lv_obj_add_style(typing_label, &style_jarvis, 0);
  lv_obj_set_width(typing_label, SCR_W - 20);
  lv_label_set_long_mode(typing_label, LV_LABEL_LONG_WRAP);
}

// ===== CHAT DISPLAY =====
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
    lv_obj_set_style_text_font(pfx, &lv_font_montserrat_12, 0);
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
  sys["content"] = "You are JARVIS, an AI assistant. Be concise. Answer in 1-3 sentences max.";
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
  lv_obj_set_style_text_font(pfx, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(pfx, lv_color_hex(0x60a5fa), 0);

  lv_obj_t *lbl = lv_label_create(container);
  lv_label_set_text(lbl, "...");
  lv_obj_set_style_text_color(lbl, lv_color_hex(0xff8888), 0);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
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

            static uint8_t pulse = 0;
            pulse = (pulse + 15) % 256;
            uint8_t bri = 40 + (pulse > 128 ? 255 - pulse : pulse);
            neopixelWrite(RGB_LED_PIN, 0, 0, bri);
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

// ===== BUTTON =====
static int btnState = HIGH, lastBtnState = HIGH;
static unsigned long btnPressTime = 0, lastPressTime = 0;
static int pressCount = 0;
static bool btnHeld = false;

void handleButton() {
  int reading = digitalRead(BTN_PIN);
  if (reading != lastBtnState) {
    if (reading == LOW) {
      btnPressTime = millis();
      pressCount++;
      lastPressTime = millis();
    } else {
      unsigned long holdTime = millis() - btnPressTime;
      if (holdTime >= 500 && pressCount == 1) {
        lv_obj_scroll_by(msg_list, 0, 40, LV_ANIM_ON);
      } else if (holdTime >= 500 && pressCount >= 2) {
        lv_obj_scroll_by(msg_list, 0, -40, LV_ANIM_ON);
      }
      btnHeld = false;
    }
  }

  if (btnState == LOW && (millis() - btnPressTime > 500)) {
    if (!btnHeld) btnHeld = true;
    if (pressCount == 1) lv_obj_scroll_by(msg_list, 0, 15, LV_ANIM_ON);
    else if (pressCount >= 2) lv_obj_scroll_by(msg_list, 0, -15, LV_ANIM_ON);
    delay(50);
  }

  if (millis() - lastPressTime > 1000 && pressCount > 0) pressCount = 0;
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
        add_user_msg(serialInput.c_str());
        lv_label_set_text(input_label, ("> " + serialInput).c_str());
        sendToGroq(serialInput);
        lv_label_set_text(input_label, "Type in Serial Monitor...");
        serialInput = "";
      }
    } else {
      serialInput += c;
    }
  }
}

// ===== SETUP =====
void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(BTN_PIN, INPUT_PULLUP);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  gfx->begin();
  gfx->setRotation(1);
  gfx->fillScreen(0);

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

  ui_init_styles();
  ui_create();

  add_sys_msg("Connecting WiFi...");
  lv_refr_now(NULL);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 40) {
    delay(500);
    tries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    lv_label_set_text_fmt(lbl_wifi, "WiFi: %s", WiFi.localIP().toString().c_str());
    lv_obj_set_style_text_color(lbl_wifi, lv_color_hex(0x22c55e), 0);
    add_sys_msg(("WiFi OK: " + WiFi.localIP().toString()).c_str());
  } else {
  lv_label_set_text(lbl_wifi, "WiFi: None");
    lv_obj_set_style_text_color(lbl_wifi, lv_color_hex(0xef4444), 0);
    add_sys_msg("! WiFi failed");
  }

  add_sys_msg((String("Model: ") + GROQ_MODEL).c_str());
  add_sys_msg("Type query in Serial Monitor...");
  lv_label_set_text(input_label, "Type in Serial Monitor...");

  rgb_cyan();
  Serial.println("\n[JARVIS LVGL] Ready. Type query + Enter:");
  Serial.println("Commands: !clear");
}

// ===== LOOP =====
void loop() {
  handleButton();
  handleSerialInput();
  lv_timer_handler();
  delay(2);
}
