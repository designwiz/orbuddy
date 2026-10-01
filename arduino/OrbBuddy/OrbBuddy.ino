/*
  OrbBuddy Clean - M3
  Five-screen navigation bring-up.
  Known-good display environment: ESP32 Arduino 3.1.3 + Arduino_GFX 1.6.4.
*/

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

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

void drawScreen() {
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

  // Placeholder content for M3.
  gfx->setTextColor(RGB565_WHITE);
  gfx->setTextSize(2);
  gfx->setCursor(145, 240);
  gfx->print("SCREEN ");
  gfx->print((int)currentScreen + 1);
  gfx->print(" / 5");

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
  Serial.println("OrbBuddy Clean M3");
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

  Serial.println("[knob] GPIO18(A) / GPIO17(B) / GPIO16(SW)");\n  Serial.println("[ui] HOME / WEATHER / RADAR / TIMER / MORE");
  drawScreen();
}

void loop() {
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
