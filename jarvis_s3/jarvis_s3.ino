#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include "jarvis_splash.h"

// ===== CONFIG - EDIT THESE =====
#define WIFI_SSID       "Net+"
#define WIFI_PASS       "123456789"
#define GROQ_API_KEY    "YOUR_GROQ_API_KEY_HERE"  // Get free at https://console.groq.com
#define GROQ_MODEL      "openai/gpt-oss-20b"                // or llama3-70b-8192, mixtral-8x7b-32768
// =================================

// TFT_eSPI pins for Waveshare ESP32-S3-LCD-1.47B Type B
// MOSI: 45, SCLK: 40, CS: 42, DC: 41, RST: 39, BL: 46
#define TFT_BL 46
#define BTN_PIN 0

// Built-in RGB LED (WS2812 on GPIO 38) — driven via RMT (no external library)
#define RGB_LED_PIN 38

// Display config (rotation 3 = 320x172 landscape)
#define SCR_W 320
#define SCR_H 172
#define CW 6
#define CH 8
#define COLS (SCR_W / CW)   // 53
#define ROWS (SCR_H / CH)   // 21

// Colors (16-bit RGB565)
#define C_BLACK   0x0000
#define C_WHITE   0xFFFF
#define C_GREEN   0x07E0
#define C_CYAN    0x07FF
#define C_RED     0xF800
#define C_YELLOW  0xFFE0
#define C_MAGENTA 0xF81F
#define C_ORANGE  0xFD20
#define C_PINK    0xF81F
#define C_BLUE    0x001F

// UI colors
uint16_t USER_COLOR   = C_CYAN;
uint16_t JARVIS_COLOR = C_RED;
uint16_t SYS_COLOR    = C_YELLOW;
uint16_t ERR_COLOR    = C_MAGENTA;

// Grid buffer (same as Barista)
static char grid[ROWS][COLS + 1];
static int grid_row = 0, grid_col = 0;
static bool display_ready = false;
static TFT_eSPI tft = TFT_eSPI();

// History
#define MAX_LINES 200
String history[MAX_LINES];
uint16_t historyColor[MAX_LINES];
int historyCount = 0;
int scrollOffset = 0;

// Streaming
#define MAX_RESPONSE_WORDS 500
String pendingWords[MAX_RESPONSE_WORDS];
int pendingCount = 0, pendingIndex = 0;
bool isStreaming = false;
int streamingLineIdx = -1;
int pendingWordX = 0, pendingWordY = 0;
unsigned long lastWordTime = 0;
#define WORD_DELAY_MS 80

// Button
int btnState = HIGH, lastBtnState = HIGH;
unsigned long btnPressTime = 0, lastPressTime = 0;
int pressCount = 0;
bool btnHeld = false, scrollMode = false;

// ===== DISPLAY CORE (Barista-style) =====

void display_clear() {
  for (int r = 0; r < ROWS; r++) grid[r][0] = '\0';
  grid_row = 0; grid_col = 0;
}

bool display_begin() {
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  
  tft.init();
  tft.setRotation(3);  // 320x172 landscape
  tft.fillScreen(C_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(C_GREEN, C_BLACK);
  tft.setTextWrap(false);
  tft.setTextDatum(TL_DATUM);
  display_ready = true;
  display_clear();
  return true;
}

void display_flush() {
  if (!display_ready) return;
  tft.fillScreen(C_BLACK);
  for (int r = 0; r < ROWS; r++) {
    if (!grid[r][0]) continue;
    tft.setCursor(0, r * CH);
    tft.setTextColor(C_GREEN, C_BLACK);  // default, will be overridden by colored text
    tft.print(grid[r]);
  }
}

void display_scroll() {
  for (int r = 0; r + 1 < ROWS; r++) memcpy(grid[r], grid[r + 1], COLS + 1);
  grid[ROWS - 1][0] = '\0';
  grid_row = ROWS - 1;
  grid_col = 0;
}

void display_newline() {
  if (grid_row + 1 < ROWS) { grid_row++; grid_col = 0; }
  else display_scroll();
}

void display_putc(char c) {
  if (grid_col >= COLS) display_newline();
  grid[grid_row][grid_col++] = c;
  grid[grid_row][grid_col] = '\0';
}

// Print UTF-8 string to grid with color tag support: [c]cyan[/c], [r]red[/r], [y]yellow[/y], [g]green[/g], [m]magenta[/m], [w]white[/w], [o]orange[/o], [p]pink[/p]
// Handles UTF-8 multi-byte sequences properly
void display_puts_colored(const String &s) {
  int len = s.length();
  for (int i = 0; i < len; i++) {
    uint8_t c = (uint8_t)s[i];
    
    // Handle color tags: [c], [/c], etc.
    if (c == '[' && i + 3 < len && s[i+2] == ']') {
      char tag = s[i+1];
      if (i + 3 < len && s[i+3] == '[') { // closing tag [/c]
        i += 3;
        continue;
      }
      // Opening tag - skip
      i += 2;
      continue;
    }
    
    // Handle UTF-8
    if (c == '\n') {
      display_newline();
    } else if (c == '\t') {
      for (int t = 0; t < 4; t++) display_putc(' ');
    } else if (c >= 32 && c <= 126) {
      display_putc((char)c);
    } else if (c >= 0xC0) {
      // UTF-8 start byte
      int seq_len = 0;
      if ((c & 0xE0) == 0xC0) seq_len = 2;
      else if ((c & 0xF0) == 0xE0) seq_len = 3;
      else if ((c & 0xF8) == 0xF0) seq_len = 4;
      else seq_len = 1;
      
      if (i + seq_len <= len) {
        display_putc((char)c);
        for (int j = 1; j < seq_len; j++) {
          if (i + j < len) display_putc((char)s[i + j]);
        }
        i += seq_len - 1;
      } else {
        display_putc((char)c);
      }
    } else if (c >= 128) {
      // UTF-8 continuation byte
      display_putc((char)c);
    }
  }
  display_flush();
}

void display_home() {
  display_clear();
  display_flush();
}

// ===== RGB LED CONTROL (ESP32 Arduino core 3.x built-in neopixelWrite) =====
#define RGB_LED_PIN 38

void rgb_init() {
  neopixelWrite(RGB_LED_PIN, 0, 0, 0);
}

void rgb_set(uint8_t r, uint8_t g, uint8_t b) {
  neopixelWrite(RGB_LED_PIN, r, g, b);
}

void rgb_red()    { neopixelWrite(RGB_LED_PIN, 255, 0, 0); }
void rgb_green()  { neopixelWrite(RGB_LED_PIN, 0, 255, 0); }
void rgb_blue()   { neopixelWrite(RGB_LED_PIN, 0, 0, 255); }
void rgb_cyan()   { neopixelWrite(RGB_LED_PIN, 0, 255, 255); }
void rgb_yellow() { neopixelWrite(RGB_LED_PIN, 255, 255, 0); }
void rgb_magenta(){ neopixelWrite(RGB_LED_PIN, 255, 0, 255); }
void rgb_white()  { neopixelWrite(RGB_LED_PIN, 255, 255, 255); }
void rgb_off()    { neopixelWrite(RGB_LED_PIN, 0, 0, 0); }

void rgb_rainbow_cycle(int cycles) {
  for (int c = 0; c < cycles; c++) {
    for (int i = 0; i < 255; i++) {
      uint8_t r = 0, g = 0, b = 0;
      if (i < 85) { r = i * 3; g = 255 - i * 3; }
      else if (i < 170) { r = 255 - (i-85)*3; b = (i-85)*3; }
      else { g = (i-170)*3; b = 255 - (i-170)*3; }
      neopixelWrite(RGB_LED_PIN, r, g, b);
      delay(2);
    }
  }
  neopixelWrite(RGB_LED_PIN, 0, 0, 0);
}

// ===== SPLASH / LOGO =====

void display_splash() {
  if (!display_ready) return;
  tft.fillScreen(C_BLACK);
  
  // Display Jarvis HD image centered
  int img_x = (SCR_W - jarvis_splash_width) / 2;
  int img_y = (SCR_H - jarvis_splash_height) / 2;
  tft.pushImage(img_x, img_y, jarvis_splash_width, jarvis_splash_height, jarvis_splash);
  
  delay(2000);
  display_home();
}

void display_stats() {
  if (!display_ready) return;
  tft.fillScreen(C_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(C_CYAN, C_BLACK);
  tft.setCursor(10, 20); tft.print("JARVIS READY");
  tft.setTextSize(1);
  tft.setTextColor(C_GREEN, C_BLACK);
  tft.setCursor(10, 55); tft.print("Groq: " GROQ_MODEL);
  tft.setCursor(10, 70); tft.print("WiFi: Connected");
  tft.setCursor(10, 85); tft.print("Type in Serial Monitor");
  tft.setCursor(10, 100); tft.print("Btn: Hold=scroll, Dbl=up");
}

// ===== HISTORY / RENDERING =====

void addToHistory(String text, uint16_t color) {
  if (historyCount < MAX_LINES) {
    history[historyCount] = text; 
    historyColor[historyCount] = color; 
    historyCount++;
  } else {
    for (int i = 1; i < MAX_LINES; i++) { 
      history[i-1] = history[i]; 
      historyColor[i-1] = historyColor[i]; 
    }
    history[MAX_LINES-1] = text; 
    historyColor[MAX_LINES-1] = color;
  }
  scrollOffset = 0;
}

void addToHistoryPrefix(String prefix, uint16_t color) {
  if (historyCount < MAX_LINES) {
    history[historyCount] = prefix; 
    historyColor[historyCount] = color; 
    historyCount++;
  } else {
    for (int i = 1; i < MAX_LINES; i++) { 
      history[i-1] = history[i]; 
      historyColor[i-1] = historyColor[i]; 
    }
    history[MAX_LINES-1] = prefix; 
    historyColor[MAX_LINES-1] = color;
  }
}

void renderHistory() {
  display_clear();
  int maxScroll = max(0, historyCount - ROWS);
  if (scrollOffset > maxScroll) scrollOffset = maxScroll;
  int start = max(0, historyCount - ROWS - scrollOffset);
  int end = min(historyCount, start + ROWS);
  
  for (int i = start; i < end; i++) {
    display_puts_colored(history[i]);
  }
  if (scrollOffset > 0) {
    char buf[20];
    snprintf(buf, sizeof(buf), "^UP %d", scrollOffset);
    display_puts_colored("[y]" + String(buf) + "[/y]");
  }
  display_flush();
}

// ===== BUTTON =====

void handleButton() {
  int reading = digitalRead(BTN_PIN);
  
  if (reading != lastBtnState) {
    lastPressTime = millis();
  }
  
  if ((millis() - lastPressTime) > 50) {
    if (reading != btnState) {
      btnState = reading;
      if (btnState == LOW) {
        btnPressTime = millis();
        pressCount++;
        Serial.println("[BTN] Press #" + String(pressCount));
      } else {
        unsigned long holdTime = millis() - btnPressTime;
        Serial.println("[BTN] Release, hold=" + String(holdTime) + "ms, count=" + String(pressCount));
        if (holdTime >= 500) {
          scrollMode = true;
          int maxScroll = max(0, historyCount - ROWS);
          if (pressCount == 1) {
            scrollOffset = max(0, scrollOffset - 3);  // Single press = UP
            Serial.println("[BTN] Scroll UP");
          } else if (pressCount >= 2) {
            scrollOffset = min(maxScroll, scrollOffset + 3); // Double press = DOWN
            Serial.println("[BTN] Scroll DOWN");
          }
          renderHistory();
        }
        btnHeld = false;
      }
    }
  }
  
  // Continuous scroll while holding (after 500ms)
  if (btnState == LOW && (millis() - btnPressTime > 500)) {
    if (!btnHeld) { btnHeld = true; scrollMode = true; }
    int maxScroll = max(0, historyCount - ROWS);
    if (pressCount == 1) scrollOffset = max(0, scrollOffset - 1);      // Single = UP
    else if (pressCount >= 2) scrollOffset = min(maxScroll, scrollOffset + 1); // Double = DOWN
    renderHistory();
    delay(100);
  }
  
  // Reset press count after 1 second of inactivity (longer window for double press)
  if (millis() - lastPressTime > 1000 && pressCount > 0) pressCount = 0;
  
  // Exit scroll mode after 3s
  if (scrollMode && (millis() - btnPressTime > 3000)) {
    scrollMode = false; scrollOffset = 0; renderHistory();
  }
  lastBtnState = reading;
}

// ===== STREAMING TYPEWRITER =====

void handleTypewriter() {
  if (!isStreaming || pendingIndex >= pendingCount) {
    if (isStreaming) { 
      isStreaming = false; 
      streamingLineIdx = -1;
      rgb_off();  // AI finished → LED off
      renderHistory();
    }
    return;
  }
  if (millis() - lastWordTime < WORD_DELAY_MS) return;

  String word = pendingWords[pendingIndex];
  pendingIndex++;
  lastWordTime = millis();

  if (word == "\n") {
    display_newline();
    if (streamingLineIdx >= 0 && streamingLineIdx < historyCount) {
      history[streamingLineIdx] += "\n";
    }
    return;
  }

  // Keep all printable Unicode (don't strip emojis/special chars)
  // Only remove control chars except newline/tab
  String clean = "";
  for (int i = 0; i < word.length(); i++) {
    char c = word[i];
    uint8_t uc = (uint8_t)c;
    if (uc >= 32 || c == '\n' || c == '\t') clean += c;
    // Allow UTF-8 continuation bytes (128-255) to pass through for multi-byte sequences
    else if (uc >= 128) clean += c;
  }
  if (clean.length() == 0) return;

  // Add to grid buffer
  display_puts_colored("[r]" + clean + "[/r] ");
  
  if (streamingLineIdx >= 0 && streamingLineIdx < historyCount) {
    history[streamingLineIdx] += clean + " ";
  }
}

// ===== GROQ QUERY =====

void queryGroq(String prompt) {
  HTTPClient http;
  http.begin("https://api.groq.com/openai/v1/chat/completions");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + String(GROQ_API_KEY));

  StaticJsonDocument<768> doc;
  doc["model"] = GROQ_MODEL;
  JsonArray messages = doc.createNestedArray("messages");
  JsonObject msg = messages.createNestedObject();
  msg["role"] = "user"; msg["content"] = prompt;
  doc["temperature"] = 0.7; doc["max_tokens"] = 512; doc["stream"] = true;

  String body; serializeJson(doc, body);
  int code = http.POST(body);
  if (code != 200) { 
    showError("HTTP " + String(code) + ": " + http.getString()); 
    http.end(); return; 
  }

  WiFiClient *s = http.getStreamPtr();
  StaticJsonDocument<384> resp; String buf = "";

  streamingLineIdx = historyCount;
  addToHistoryPrefix("[r]JARVIS:[/r] ", JARVIS_COLOR);
  pendingCount = 0;
  rgb_blue();  // AI is thinking → LED blue

  while (http.connected() || s->available()) {
    if (s->available()) {
      char c = s->read(); buf += c;
      if (c == '\n') {
        buf.trim();
        if (buf.startsWith("data: ")) {
          String json = buf.substring(6);
          if (json == "[DONE]") break;
          DeserializationError err = deserializeJson(resp, json);
          if (!err && resp["choices"][0]["delta"].containsKey("content")) {
            String token = resp["choices"][0]["delta"]["content"].as<String>();
            Serial.print(token);
            
            int tokenLen = token.length();
            const char* tokenStr = token.c_str();
            int wordStart = 0;
            for (int i = 0; i <= tokenLen && pendingCount < MAX_RESPONSE_WORDS; i++) {
              if (i == tokenLen || tokenStr[i] == ' ' || tokenStr[i] == '\n') {
                int wordLen = i - wordStart;
                if (wordLen > 0) {
                  String w = token.substring(wordStart, i);
                  // Keep symbols/emojis by not sanitizing here
                  if (w.length() > 0) pendingWords[pendingCount++] = w;
                }
                if (i < tokenLen && tokenStr[i] == '\n') {
                  pendingWords[pendingCount++] = "\n";
                }
                wordStart = i + 1;
              }
            }
          }
        }
        buf = "";
      }
    }
  }
  http.end(); Serial.println();

  pendingIndex = 0; isStreaming = true; lastWordTime = millis();
}

void showError(String msg) {
  display_clear();
  display_puts_colored("[m]ERROR:[/m]");
  display_puts_colored("[m]" + msg + "[/m]");
  display_flush();
  Serial.println("[ERROR] " + msg);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(BTN_PIN, INPUT_PULLUP);

  display_begin();
  display_splash();

  // WiFi
  tft.setTextSize(1);
  tft.setTextColor(C_YELLOW, C_BLACK);
  tft.setCursor(10, 100); tft.print("Connecting WiFi...");
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }

  Serial.println("\nWiFi: " + WiFi.localIP().toString());
  Serial.println("Commands: !color red|green|cyan|yellow|magenta|white|orange|pink");
  Serial.println("          !rgb red|green|blue|cyan|yellow|magenta|white|rainbow|off");
  Serial.println("          !clear  !up  !down  !top  !btn");
  Serial.println("BTN (BOOT/GPIO0): Single press+hold = UP, Double press+hold = DOWN");
  Serial.println("Type query and press Enter:");
  delay(500);

  // Initialize RGB LED
  rgb_init();
  // Startup rainbow effect
  rgb_rainbow_cycle(2);
  rgb_cyan();  // Start with cyan

  display_stats();
  delay(1000);

  addToHistory("[w]Ready. Type in Serial Monitor.[/w]", C_WHITE);
  addToHistory("[y]BTN: Single+hold=UP, Double+hold=DOWN[/y]", C_YELLOW);
  renderHistory();
}

void loop() {
  handleButton();
  handleTypewriter();
  
  // RGB pulse effect during streaming
  static unsigned long lastPulseTime = 0;
  static int pulseStep = 0;
  if (isStreaming && millis() - lastPulseTime > 30) {
    lastPulseTime = millis();
    pulseStep = (pulseStep + 1) % 60;
    // Smooth blue pulse: fade brightness between 40–255
    uint8_t bri = 40 + (int)(215.0 * (1.0 - fabs((float)pulseStep - 30.0) / 30.0));
    neopixelWrite(RGB_LED_PIN, 0, 0, bri);
  }
  
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.length() == 0) return;

    String lower = input; lower.toLowerCase();
    if (lower == "!up") { scrollOffset = min(max(0, historyCount - ROWS), scrollOffset + 3); scrollMode = true; renderHistory(); return; }
    if (lower == "!down") { scrollOffset = max(0, scrollOffset - 3); scrollMode = true; renderHistory(); return; }
    if (lower == "!top") { scrollMode = false; scrollOffset = 0; renderHistory(); return; }
    if (lower == "!clear") { historyCount = 0; scrollOffset = 0; addToHistory("[y]Cleared.[/y]", C_YELLOW); renderHistory(); return; }
    if (lower == "!btn") { Serial.println("[BTN TEST] Pin=" + String(BTN_PIN) + " State=" + String(digitalRead(BTN_PIN))); return; }
    if (lower == "!rgb off") { rgb_off(); addToHistory("[y]RGB OFF[/y]", C_YELLOW); renderHistory(); return; }
    if (lower == "!rgb red") { rgb_red(); addToHistory("[r]RGB RED[/r]", C_RED); renderHistory(); return; }
    if (lower == "!rgb green") { rgb_green(); addToHistory("[g]RGB GREEN[/g]", C_GREEN); renderHistory(); return; }
    if (lower == "!rgb blue") { rgb_blue(); addToHistory("[b]RGB BLUE[/b]", C_BLUE); renderHistory(); return; }
    if (lower == "!rgb cyan") { rgb_cyan(); addToHistory("[c]RGB CYAN[/c]", C_CYAN); renderHistory(); return; }
    if (lower == "!rgb yellow") { rgb_yellow(); addToHistory("[y]RGB YELLOW[/y]", C_YELLOW); renderHistory(); return; }
    if (lower == "!rgb magenta") { rgb_magenta(); addToHistory("[m]RGB MAGENTA[/m]", C_MAGENTA); renderHistory(); return; }
    if (lower == "!rgb white") { rgb_white(); addToHistory("[w]RGB WHITE[/w]", C_WHITE); renderHistory(); return; }
    if (lower == "!rgb rainbow") { rgb_rainbow_cycle(3); addToHistory("[y]RGB RAINBOW[/y]", C_YELLOW); renderHistory(); return; }
    if (input.startsWith("!color ")) {
      String c = input.substring(7); c.toLowerCase();
      if (c == "red")       USER_COLOR = C_RED;
      else if (c == "green")    USER_COLOR = C_GREEN;
      else if (c == "cyan")     USER_COLOR = C_CYAN;
      else if (c == "yellow")   USER_COLOR = C_YELLOW;
      else if (c == "magenta")  USER_COLOR = C_MAGENTA;
      else if (c == "white")    USER_COLOR = C_WHITE;
      else if (c == "orange")   USER_COLOR = C_ORANGE;
      else if (c == "pink")     USER_COLOR = C_PINK;
      addToHistory("[y]Color set: " + c + "[/y]", C_YELLOW);
      renderHistory();
      Serial.println("[Color] " + c);
      return;
    }

    if (!scrollMode) {
      addToHistory("[c]You:[/c] " + input, USER_COLOR);
      renderHistory();
      Serial.println("\n[Query] " + input);
      queryGroq(input);
    }
  }
}
