/*
  OrbBuddy Clean - M6.9
  Live HOME: Irish time + Westport weather.
  Known-good display environment: ESP32 Arduino 3.1.3 + Arduino_GFX 1.6.4.
*/

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include "FreeSans24pt7b.h"

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

// M6 live aircraft radar — Westport centre, 35 nm range.
static const float RADAR_LAT = 53.8008f;
static const float RADAR_LON = -9.5223f;
static const int RADAR_RANGE_NM = 35;
static const int RADAR_MAX_TARGETS = 12;
struct RadarTarget { String flight, reg; float lat, lon, altFt, track, distNm, bearing; bool rescue118; };
static RadarTarget radarTargets[RADAR_MAX_TARGETS];
static int radarCount = 0;
static bool radarReady = false;
static uint32_t lastRadarMs = 0;
static uint32_t nextRadarAttemptMs = 0;
static int lastRadarHttp = 0;

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
  gfx->fillRect(45, 100, 376, 240, RGB565_BLACK);

  struct tm t;
  bool haveTime = getLocalTime(&t, 10);
  char dateBuf[24] = "SYNCING TIME";
  if (haveTime) {
    strftime(dateBuf, sizeof(dateBuf), "%a %d %b", &t);
    for (char *p = dateBuf; *p; ++p) *p = toupper(*p);
    timeReady = true;

    char timeBuf[6];
    strftime(timeBuf, sizeof(timeBuf), "%H:%M", &t);
    gfx->setFont(&FreeSans24pt7b);
    gfx->setTextSize(2);
    gfx->setTextColor(RGB565_WHITE);
    int16_t tx1, ty1; uint16_t tw, th;
    gfx->getTextBounds(timeBuf, 0, 0, &tx1, &ty1, &tw, &th);
    gfx->setCursor((SCREEN_W - (int)tw) / 2 - tx1, 180);
    gfx->print(timeBuf);
    gfx->setFont();
    gfx->setTextSize(1);
  }

  gfx->setFont(); gfx->setTextSize(2);
  gfx->setTextColor(RGB565_DARKGREY);
  int16_t x1,y1; uint16_t w,h;
  gfx->getTextBounds(dateBuf,0,0,&x1,&y1,&w,&h);
  gfx->setCursor((SCREEN_W-(int)w)/2,205);
  gfx->print(dateBuf);

  if (weatherReady) {
    char tempBuf[12];
    snprintf(tempBuf,sizeof(tempBuf),"%.0f C",weatherTemp);
    gfx->setTextColor(RGB565_CYAN);
    gfx->setTextSize(3);
    gfx->getTextBounds(tempBuf,0,0,&x1,&y1,&w,&h);
    gfx->setCursor((SCREEN_W-(int)w)/2,255);
    gfx->print(tempBuf);

    const char *desc=weatherText(weatherCode);
    gfx->setTextColor(RGB565_WHITE);
    gfx->setTextSize(2);
    gfx->getTextBounds(desc,0,0,&x1,&y1,&w,&h);
    gfx->setCursor((SCREEN_W-(int)w)/2,300);
    gfx->print(desc);
  } else {
    gfx->setTextColor(RGB565_DARKGREY);
    gfx->setTextSize(2);
    gfx->setCursor(145,270);
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

float radarRad(float d) { return d * 0.01745329252f; }
float radarDistanceNm(float lat1,float lon1,float lat2,float lon2) {
  float p1=radarRad(lat1), p2=radarRad(lat2), dp=radarRad(lat2-lat1), dl=radarRad(lon2-lon1);
  float a=sinf(dp/2)*sinf(dp/2)+cosf(p1)*cosf(p2)*sinf(dl/2)*sinf(dl/2);
  return 3440.065f * 2.0f * atan2f(sqrtf(a),sqrtf(1.0f-a));
}
float radarBearing(float lat1,float lon1,float lat2,float lon2) {
  float p1=radarRad(lat1), p2=radarRad(lat2), dl=radarRad(lon2-lon1);
  float y=sinf(dl)*cosf(p2), x=cosf(p1)*sinf(p2)-sinf(p1)*cosf(p2)*cosf(dl);
  float b=atan2f(y,x)*57.2957795f; return b<0?b+360.0f:b;
}
String radarJsonString(const String &obj,const char *key) {
  String needle=String("\"")+key+"\":"; int p=obj.indexOf(needle); if(p<0)return ""; p+=needle.length();
  while(p<(int)obj.length() && (obj[p]==' '))p++; if(p>=(int)obj.length()||obj[p]!='\"')return ""; p++;
  int e=obj.indexOf('\"',p); return e<0?"":obj.substring(p,e);
}
float radarJsonNumber(const String &obj,const char *key,float fallback=NAN) {
  String needle=String("\\\"")+key+"\\\":"; int p=obj.indexOf(needle); if(p<0)return fallback; p+=needle.length();
  while(p<(int)obj.length() && obj[p]==' ')p++; if(obj.startsWith("null",p))return fallback; return obj.substring(p).toFloat();
}
void fetchRadar() {
  if(WiFi.status()!=WL_CONNECTED)return;
  HTTPClient http; String url=String("https://api.adsb.lol/v2/point/")+String(RADAR_LAT,4)+"/"+String(RADAR_LON,4)+"/"+RADAR_RANGE_NM;
  http.begin(url); http.setTimeout(2500); http.setUserAgent("OrbBuddy/1.0"); http.addHeader("Accept","application/json"); int code=http.GET();
  if(code==HTTP_CODE_OK){
    String body=http.getString(); radarCount=0; int ap=body.indexOf("\"ac\":[");
    if(ap>=0){ int p=body.indexOf('{',ap); while(p>=0 && radarCount<RADAR_MAX_TARGETS){ int e=body.indexOf('}',p); if(e<0)break; String o=body.substring(p,e+1);
      float la=radarJsonNumber(o,"lat"), lo=radarJsonNumber(o,"lon");
      if(!isnan(la)&&!isnan(lo)){ RadarTarget &t=radarTargets[radarCount]; t.lat=la;t.lon=lo;t.altFt=radarJsonNumber(o,"alt_baro",0);t.track=radarJsonNumber(o,"track",0);t.flight=radarJsonString(o,"flight");t.flight.trim();t.reg=radarJsonString(o,"r");t.reg.trim();t.distNm=radarDistanceNm(RADAR_LAT,RADAR_LON,la,lo);t.bearing=radarBearing(RADAR_LAT,RADAR_LON,la,lo); String id=t.flight+" "+t.reg; id.toUpperCase(); t.rescue118=(id.indexOf("EI-IRT")>=0||id.indexOf("RESCUE118")>=0||id.indexOf("R118")>=0); radarCount++; }
      p=body.indexOf('{',e+1); int end=body.indexOf(']',e+1); if(end>=0 && (p<0||end<p))break;
    }} radarReady=true;lastRadarMs=millis();Serial.printf("[radar] %d aircraft within %d nm\\n",radarCount,RADAR_RANGE_NM);
  } else { lastRadarHttp=code; nextRadarAttemptMs=millis()+60000UL; Serial.printf("[radar] HTTP %d\\n",code); } http.end();
}
void drawRadar(){
  gfx->fillScreen(RGB565_BLACK);
  const int cx=233, cy=235;
  const int R1=142, R2=94, R3=47;

  gfx->setFont();
  gfx->setTextSize(2);
  gfx->setTextColor(RGB565_GREEN);
  gfx->setCursor(196,34);
  gfx->print("RADAR");

  gfx->setTextSize(1);
  gfx->setTextColor(RGB565_CYAN);
  gfx->setCursor(218,57);
  gfx->print("35 NM");

  // M6.9: clean rings without drawCircle(). Plot small filled dots around
  // each circumference; this avoids the CO5300 filled-circle erase banding.
  for(int deg=0; deg<360; deg+=2){
    float a=radarRad((float)deg);
    int ca=(int)(cosf(a)*R1), sa=(int)(sinf(a)*R1);
    gfx->fillCircle(cx+ca,cy+sa,2,RGB565_CYAN);
    ca=(int)(cosf(a)*R2); sa=(int)(sinf(a)*R2);
    gfx->fillCircle(cx+ca,cy+sa,2,RGB565_CYAN);
    ca=(int)(cosf(a)*R3); sa=(int)(sinf(a)*R3);
    gfx->fillCircle(cx+ca,cy+sa,2,RGB565_CYAN);
  }

  // Thin crosshair using filled rectangles only.
  gfx->fillRect(cx-R1,cy-1,R1*2,3,RGB565_CYAN);
  gfx->fillRect(cx-1,cy-R1,3,R1*2,RGB565_CYAN);

  // Cardinal marks positioned clear of the rings.
  gfx->setTextColor(RGB565_WHITE);
  gfx->setTextSize(2);
  gfx->setCursor(cx-6,75);  gfx->print("N");
  gfx->setCursor(cx-6,385); gfx->print("S");
  gfx->setCursor(389,cy-7); gfx->print("E");
  gfx->setCursor(63, cy-7); gfx->print("W");

  // Range labels.
  gfx->setTextColor(RGB565_CYAN);
  gfx->setTextSize(1);
  gfx->setCursor(cx+51,cy+7); gfx->print("12");
  gfx->setCursor(cx+99,cy+7); gfx->print("23");

  // OrbBuddy / Westport origin.
  gfx->fillCircle(cx,cy,7,RGB565_WHITE);
  gfx->fillCircle(cx,cy,3,RGB565_BLACK);

  bool rescue=false;
  for(int i=0;i<radarCount;i++){
    RadarTarget&t=radarTargets[i];
    if(t.distNm>RADAR_RANGE_NM) continue;
    float rr=(t.distNm/(float)RADAR_RANGE_NM)*R1;
    float a=radarRad(t.bearing-90.0f);
    int x=cx+(int)(cosf(a)*rr), y=cy+(int)(sinf(a)*rr);
    uint16_t col=t.rescue118?RGB565_RED:RGB565_GREEN;
    gfx->fillCircle(x,y,t.rescue118?9:6,col);
    String label=t.flight.length()?t.flight:t.reg;
    if(label.length()){
      if(label.length()>8) label=label.substring(0,8);
      gfx->setTextColor(col); gfx->setTextSize(1);
      gfx->setCursor(x>340?x-55:x+9,y-4); gfx->print(label);
    }
    if(t.rescue118) rescue=true;
  }

  gfx->setTextSize(1);
  gfx->setTextColor(radarReady?RGB565_GREEN:RGB565_WHITE);
  char status[42];
  if(radarReady) snprintf(status,sizeof(status),"%d AIRCRAFT  /  35 NM",radarCount);
  else if(lastRadarHttp) snprintf(status,sizeof(status),"RADAR HTTP %d",lastRadarHttp);
  else snprintf(status,sizeof(status),"RADAR SYNCING");
  int16_t x1,y1; uint16_t w,h;
  gfx->getTextBounds(status,0,0,&x1,&y1,&w,&h);
  gfx->setCursor((SCREEN_W-(int)w)/2,425);
  gfx->print(status);

  if(rescue){
    gfx->fillRoundRect(102,92,262,36,10,RGB565_RED);
    gfx->setTextColor(RGB565_WHITE); gfx->setTextSize(2);
    gfx->setCursor(126,104); gfx->print("RESCUE 118 NEARBY");
  }
}

void drawScreen() {
  if (currentScreen == HOME) {
    drawHome();
    return;
  }
  if (currentScreen == RADAR) {
    drawRadar();
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
  Serial.println("OrbBuddy Clean M6.9");
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

  // Never perform a blocking HTTP request while the user is on RADAR.
  // The old M6 path stalled knob handling while HTTPClient waited.
  if (currentScreen != RADAR && WiFi.status() == WL_CONNECTED && millis() >= nextRadarAttemptMs && (!radarReady || millis() - lastRadarMs > 15000UL)) {
    nextRadarAttemptMs = millis() + 15000UL;
    fetchRadar();
  }

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
