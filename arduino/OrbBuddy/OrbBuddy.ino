/*
  OrbBuddy Clean - M5.7
  Live HOME: Irish time + Westport weather.
  Known-good display environment: ESP32 Arduino 3.1.3 + Arduino_GFX 1.6.4.
*/

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <time.h>

#define SCREEN_W 466
#define SCREEN_H 466
#define LCD_COL_OFFSET 6
#define LCD_ROW_OFFSET 0
#define LCD_QSPI_HZ 80000000
#define BRIGHTNESS_DEFAULT 200

#define PIN_LCD_CS   12
#define PIN_LCD_RST  39
#define PIN_LCD_SCLK 38
#define PIN_LCD_D0    4
#define PIN_LCD_D1    5
#define PIN_LCD_D2    6
#define PIN_LCD_D3    7

// Exact knob wiring from the known-working Orb firmware.
#define PIN_KNOB_A  18
#define PIN_KNOB_B  17
#define PIN_KNOB_SW 16
#define KNOB_STEPS_PER_DETENT 4

static Arduino_DataBus *bus = nullptr;
static Arduino_CO5300 *gfx = nullptr;
static WebServer server(80);
static bool setupAP = false;
static String wifiStatus = "STARTING";
static String wifiIP = "--";
static bool timeReady = false;
static bool weatherReady = false;
static float weatherTemp = 0.0f;
static int weatherCode = -1;
static uint32_t lastWeatherMs = 0;
static uint32_t lastClockDrawMs = 0;
static int lastDrawnMinute = -1;
static bool homeDynamicDirty = true;

static volatile int32_t rawPos = 0;
static volatile int32_t anchor = 0;
static volatile int32_t detent = 0;
static volatile uint8_t prevAB = 0;

enum ScreenId { HOME, WEATHER, RADAR, TIMER, MORE, SCREEN_COUNT };

static ScreenId currentScreen = HOME;
static int32_t lastDetent = 0;
static bool lastButton = HIGH;
static uint32_t lastButtonMs = 0;
static uint32_t selectFlashUntil = 0;

static const int8_t quadTable[16] = {
   0, -1,  1,  0,
   1,  0,  0, -1,
  -1,  0,  0,  1,
   0,  1, -1,  0
};

void IRAM_ATTR knobISR() {
  uint8_t a = digitalRead(PIN_KNOB_A);
  uint8_t b = digitalRead(PIN_KNOB_B);
  uint8_t ab = (a << 1) | b;
  uint8_t idx = ((prevAB << 2) | ab) & 0x0F;

  rawPos += quadTable[idx];
  prevAB = ab;

  while (rawPos - anchor >= KNOB_STEPS_PER_DETENT) {
    anchor += KNOB_STEPS_PER_DETENT;
    detent++;
  }
  while (anchor - rawPos >= KNOB_STEPS_PER_DETENT) {
    anchor -= KNOB_STEPS_PER_DETENT;
    detent--;
  }
}

const char *screenName(ScreenId id) {
  switch (id) {
    case HOME:    return "HOME";
    case WEATHER: return "WEATHER";
    case RADAR:   return "RADAR";
    case TIMER:   return "TIMER";
    case MORE:    return "MORE";
    default:      return "?";
  }
}

uint16_t screenAccent(ScreenId id) {
  switch (id) {
    case HOME:    return RGB565_CYAN;
    case WEATHER: return RGB565_BLUE;
    case RADAR:   return RGB565_GREEN;
    case TIMER:   return RGB565_YELLOW;
    case MORE:    return RGB565_MAGENTA;
    default:      return RGB565_WHITE;
  }
}

String htmlPage() {
  String h = "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>";
  h += "<title>OrbBuddy Setup</title><style>body{font-family:Arial;background:#090b10;color:#fff;max-width:520px;margin:40px auto;padding:20px}input,button{width:100%;box-sizing:border-box;padding:14px;margin:8px 0;border-radius:10px;border:1px solid #333;background:#151923;color:#fff}button{background:#00a6a6;font-weight:bold}</style></head><body>";
  h += "<h1>OrbBuddy</h1><p>Wi-Fi setup</p><form method='POST' action='/save'>";
  h += "<input name='ssid' placeholder='Wi-Fi name' required>";
  h += "<input name='password' type='password' placeholder='Wi-Fi password'>";
  h += "<button type='submit'>Save & Connect</button></form>";
  h += "<p>Status: " + wifiStatus + "</p><p>IP: " + wifiIP + "</p></body></html>";
  return h;
}

void startSetupAP() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("OrbBuddy-Setup");
  setupAP = true;
  wifiStatus = "SETUP AP";
  wifiIP = WiFi.softAPIP().toString();
  Serial.print("[wifi] setup AP: OrbBuddy-Setup  IP: ");
  Serial.println(wifiIP);
}

void startWebServer() {
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", htmlPage());
  });

  server.on("/save", HTTP_POST, []() {
    String ssid = server.arg("ssid");
    String password = server.arg("password");
    if (ssid.length() == 0) {
      server.send(400, "text/plain", "SSID required");
      return;
    }

    server.send(200, "text/html", "<html><body style='font-family:Arial'><h2>Saved</h2><p>OrbBuddy is connecting. You can close this page.</p></body></html>");
    WiFi.begin(ssid.c_str(), password.c_str());
    wifiStatus = "CONNECTING";
    Serial.print("[wifi] connecting to ");
    Serial.println(ssid);
  });

  server.begin();
  Serial.println("[web] setup/admin server started on port 80");
}

void beginWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin();  // Reuse credentials stored by the ESP32 Wi-Fi stack.

  wifiStatus = "CONNECTING";
  Serial.println("[wifi] trying saved credentials");

  const uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < 8000) {
    delay(100);
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiStatus = "CONNECTED";
    wifiIP = WiFi.localIP().toString();
    Serial.print("[wifi] connected  IP: ");
    Serial.println(wifiIP);
  } else {
    startSetupAP();
  }

  startWebServer();
}

const char *weatherText(int code) {
  if (code == 0) return "Clear";
  if (code <= 3) return "Partly cloudy";
  if (code == 45 || code == 48) return "Fog";
  if (code >= 51 && code <= 57) return "Drizzle";
  if (code >= 61 && code <= 67) return "Rain";
  if (code >= 71 && code <= 77) return "Snow";
  if (code >= 80 && code <= 82) return "Showers";
  if (code >= 85 && code <= 86) return "Snow showers";
  if (code >= 95) return "Thunderstorm";
  return "Weather";
}

void beginClock() {
  // Let SNTP apply the Irish POSIX timezone directly. configTime() was
  // overwriting the TZ environment when called after setenv()/tzset().
  configTzTime("GMT0IST,M3.5.0/1,M10.5.0/2", "pool.ntp.org", "time.cloudflare.com");

  struct tm t;
  timeReady = getLocalTime(&t, 5000);
  Serial.println(timeReady ? "[time] synced" : "[time] sync pending");
}

void fetchWeather() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  // Westport, Co. Mayo. Open-Meteo needs no API key.
  const char *url = "https://api.open-meteo.com/v1/forecast?latitude=53.8008&longitude=-9.5223&current=temperature_2m,weather_code&timezone=Europe%2FDublin";
  http.begin(url);
  int code = http.GET();

  if (code == HTTP_CODE_OK) {
    String body = http.getString();
    // The same keys also exist in current_units. Search only inside
    // the actual "current" object or the unit strings parse as 0.
    int currentPos = body.indexOf("\"current\":");
    int tPos = currentPos >= 0 ? body.indexOf("\"temperature_2m\":", currentPos) : -1;
    int wPos = currentPos >= 0 ? body.indexOf("\"weather_code\":", currentPos) : -1;

    if (tPos >= 0 && wPos >= 0) {
      tPos += 17;
      wPos += 15;
      weatherTemp = body.substring(tPos).toFloat();
      weatherCode = body.substring(wPos).toInt();
      weatherReady = true;
      lastWeatherMs = millis();
      Serial.print("[weather] ");
      Serial.print(weatherTemp, 1);
      Serial.print(" C  code=");
      Serial.println(weatherCode);
    }
  } else {
    Serial.print("[weather] HTTP ");
    Serial.println(code);
  }

  http.end();
}

void drawHomeDynamic() {
  // HOME middle is redrawn only when its displayed values actually change.
  // Clearing this region every second was the visible blink.
  gfx->fillRect(45, 100, 376, 240, RGB565_BLACK);

  struct tm t;
  bool haveTime = getLocalTime(&t, 10);
  char timeBuf[6] = "--:--";
  char dateBuf[24] = "SYNCING TIME";
  if (haveTime) {
    strftime(timeBuf, sizeof(timeBuf), "%H:%M", &t);
    strftime(dateBuf, sizeof(dateBuf), "%a %d %b", &t);
    for (char *p = dateBuf; *p; ++p) *p = toupper(*p);
    timeReady = true;
  }

  // Proper GFX fonts instead of magnifying the 5x7 bitmap font.
  // Built-in font: keep it clean and proportioned; no huge blocky scaling.
  gfx->setFont(); gfx->setTextSize(4);
  gfx->setTextColor(RGB565_WHITE);
  int16_t x1, y1; uint16_t w, h;
  gfx->getTextBounds(timeBuf, 0, 0, &x1, &y1, &w, &h);
  gfx->setCursor((SCREEN_W - (int)w) / 2, 160);
  gfx->print(timeBuf);

  gfx->setFont(); gfx->setTextSize(2);
  gfx->setTextColor(RGB565_DARKGREY);
  gfx->getTextBounds(dateBuf, 0, 0, &x1, &y1, &w, &h);
  gfx->setCursor((SCREEN_W - (int)w) / 2, 205);
  gfx->print(dateBuf);

  if (weatherReady) {
    char tempBuf[12];
    snprintf(tempBuf, sizeof(tempBuf), "%.0f C", weatherTemp);
    gfx->setFont(); gfx->setTextSize(4);
    gfx->setTextColor(RGB565_CYAN);
    gfx->getTextBounds(tempBuf, 0, 0, &x1, &y1, &w, &h);
    gfx->setCursor((SCREEN_W - (int)w) / 2, 270);
    gfx->print(tempBuf);

    const char *desc = weatherText(weatherCode);
    gfx->setFont(); gfx->setTextSize(2);
    gfx->setTextColor(RGB565_WHITE);
    gfx->getTextBounds(desc, 0, 0, &x1, &y1, &w, &h);
    gfx->setCursor((SCREEN_W - (int)w) / 2, 310);
    gfx->print(desc);
  } else {
    gfx->setFont(); gfx->setTextSize(2);
    gfx->setTextColor(RGB565_DARKGREY);
    gfx->setCursor(145, 270);
    gfx->print("WEATHER SYNC");
  }
  gfx->setFont();
}

void drawHome() {
  gfx->fillScreen(RGB565_BLACK);

  gfx->setFont(); gfx->setTextSize(2);
  gfx->setTextColor(RGB565_CYAN);
  int16_t x1, y1; uint16_t w, h;
  const char *place = "WESTPORT";
  gfx->getTextBounds(place, 0, 0, &x1, &y1, &w, &h);
  gfx->setCursor((SCREEN_W - (int)w) / 2, 75);
  gfx->print(place);
  gfx->setFont();

  drawHomeDynamic();
  struct tm homeTm;
  if (getLocalTime(&homeTm, 10)) lastDrawnMinute = homeTm.tm_min;
  homeDynamicDirty = false;

  gfx->setFont(); gfx->setTextSize(2);
  gfx->setTextColor(WiFi.status() == WL_CONNECTED ? RGB565_GREEN : RGB565_DARKGREY);
  const char *net = WiFi.status() == WL_CONNECTED ? "WiFi  LIVE" : "WiFi  OFFLINE";
  gfx->getTextBounds(net, 0, 0, &x1, &y1, &w, &h);
  gfx->setCursor((SCREEN_W - (int)w) / 2, 375);
  gfx->print(net);
  gfx->setFont();

  gfx->fillCircle(233, 405, 4, RGB565_CYAN);
}

void drawScreen() {
  if (currentScreen == HOME) {
    drawHome();
    return;
  }

  gfx->fillScreen(RGB565_BLACK);

  const uint16_t accent = screenAccent(currentScreen);

  // Small OrbBuddy identity.
  gfx->setTextColor(RGB565_WHITE);
  gfx->setTextSize(2);
  gfx->setCursor(172, 75);
  gfx->print("ORB BUDDY");

  // Main screen title.
  const char *name = screenName(currentScreen);
  int titleWidth = strlen(name) * 24;  // built-in font at size 4 ~= 24 px/char
  gfx->setTextColor(accent);
  gfx->setTextSize(4);
  gfx->setCursor((SCREEN_W - titleWidth) / 2, 175);
  gfx->print(name);

  gfx->setTextColor(RGB565_WHITE);
  gfx->setTextSize(2);

  if (currentScreen == MORE) {
    gfx->setCursor(130, 235);
    gfx->print("WiFi: ");
    gfx->print(wifiStatus);
    gfx->setCursor(130, 265);
    gfx->print("IP: ");
    gfx->print(wifiIP);
  } else {
    gfx->setCursor(145, 240);
    gfx->print("SCREEN ");
    gfx->print((int)currentScreen + 1);
    gfx->print(" / 5");
  }

  // Five simple position markers.
  const int dotY = 300;
  const int firstX = 153;
  for (int i = 0; i < SCREEN_COUNT; i++) {
    int x = firstX + (i * 40);
    if (i == (int)currentScreen) {
      gfx->fillCircle(x, dotY, 8, accent);
    } else {
      gfx->drawCircle(x, dotY, 6, RGB565_DARKGREY);
    }
  }

  gfx->setTextColor(RGB565_DARKGREY);
  gfx->setTextSize(2);
  gfx->setCursor(103, 350);
  gfx->print("TURN = MOVE   PUSH = SELECT");

  if (millis() < selectFlashUntil) {
    gfx->setTextColor(accent);
    gfx->setTextSize(2);
    gfx->setCursor(163, 390);
    gfx->print("SELECTED");
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("OrbBuddy Clean M5.7");
  Serial.printf("Arduino core: %s\n", ESP_ARDUINO_VERSION_STR);

  bus = new Arduino_ESP32QSPI(
    PIN_LCD_CS, PIN_LCD_SCLK,
    PIN_LCD_D0, PIN_LCD_D1, PIN_LCD_D2, PIN_LCD_D3
  );

  gfx = new Arduino_CO5300(
    bus, PIN_LCD_RST, 0,
    SCREEN_W, SCREEN_H,
    LCD_COL_OFFSET, LCD_ROW_OFFSET, 0, 0
  );

  if (!gfx->begin(LCD_QSPI_HZ)) {
    Serial.println("[display] gfx->begin() FAILED");
    while (true) delay(1000);
  }

  gfx->setBrightness(BRIGHTNESS_DEFAULT);
  Serial.println("[display] panel up");

  pinMode(PIN_KNOB_A, INPUT_PULLUP);
  pinMode(PIN_KNOB_B, INPUT_PULLUP);
  pinMode(PIN_KNOB_SW, INPUT_PULLUP);

  uint8_t a = digitalRead(PIN_KNOB_A);
  uint8_t b = digitalRead(PIN_KNOB_B);
  prevAB = (a << 1) | b;

  attachInterrupt(digitalPinToInterrupt(PIN_KNOB_A), knobISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_KNOB_B), knobISR, CHANGE);

  lastButton = digitalRead(PIN_KNOB_SW);

  Serial.println("[knob] GPIO18(A) / GPIO17(B) / GPIO16(SW)");
  Serial.println("[ui] HOME / WEATHER / RADAR / TIMER / MORE");
  drawScreen();
  beginWiFi();
  if (WiFi.status() == WL_CONNECTED) {
    beginClock();
    fetchWeather();
  }
  drawScreen();
}

void loop() {
  server.handleClient();

  static wl_status_t lastWiFiState = WL_IDLE_STATUS;
  wl_status_t nowWiFiState = WiFi.status();
  if (nowWiFiState != lastWiFiState) {
    lastWiFiState = nowWiFiState;
    if (nowWiFiState == WL_CONNECTED) {
      wifiStatus = "CONNECTED";
      wifiIP = WiFi.localIP().toString();
      Serial.print("[wifi] connected  IP: ");
      Serial.println(wifiIP);
      if (currentScreen == MORE) drawScreen();
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (!timeReady) {
      struct tm t;
      timeReady = getLocalTime(&t, 10);
    }

    if (!weatherReady || millis() - lastWeatherMs > 900000UL) {
      fetchWeather();
      homeDynamicDirty = true;
      if (currentScreen == HOME) drawHomeDynamic();
    }

    if (currentScreen == HOME && millis() - lastClockDrawMs > 1000UL) {
      lastClockDrawMs = millis();
      struct tm t;
      if (getLocalTime(&t, 10)) {
        if (t.tm_min != lastDrawnMinute || homeDynamicDirty) {
          lastDrawnMinute = t.tm_min;
          homeDynamicDirty = false;
          drawHomeDynamic();
        }
      }
    }
  }

  int32_t nowDetent = detent;

  if (nowDetent != lastDetent) {
    int32_t delta = nowDetent - lastDetent;
    lastDetent = nowDetent;

    int next = (int)currentScreen + (int)delta;
    while (next < 0) next += SCREEN_COUNT;
    while (next >= SCREEN_COUNT) next -= SCREEN_COUNT;
    currentScreen = (ScreenId)next;

    selectFlashUntil = 0;
    Serial.printf("[ui] -> %s\n", screenName(currentScreen));
    drawScreen();
  }

  bool button = digitalRead(PIN_KNOB_SW);

  if (lastButton == HIGH && button == LOW && millis() - lastButtonMs > 200) {
    lastButtonMs = millis();
    selectFlashUntil = millis() + 700;
    Serial.printf("[ui] SELECT %s\n", screenName(currentScreen));
    drawScreen();
  }

  lastButton = button;

  static bool flashWasVisible = false;
  bool flashVisible = millis() < selectFlashUntil;
  if (flashWasVisible && !flashVisible) {
    drawScreen();
  }
  flashWasVisible = flashVisible;

  delay(2);
}
