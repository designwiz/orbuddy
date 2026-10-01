/*
  OrbBuddy Clean - M2
  AMOLED + rotary encoder bring-up.
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

static int32_t shownValue = 0;
static int32_t lastDetent = 0;
static bool lastButton = HIGH;
static uint32_t lastButtonMs = 0;

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

void drawScreen() {
  gfx->fillScreen(RGB565_BLACK);

  gfx->setTextColor(RGB565_CYAN);
  gfx->setTextSize(3);
  gfx->setCursor(135, 120);
  gfx->println("ORB BUDDY");

  gfx->setTextColor(RGB565_WHITE);
  gfx->setTextSize(2);
  gfx->setCursor(175, 160);
  gfx->println("CLEAN M2");

  char value[16];
  snprintf(value, sizeof(value), "%ld", (long)shownValue);

  // Approximate centering for the built-in font at size 8.
  int16_t x = 233 - ((int)strlen(value) * 24);
  gfx->setTextColor(RGB565_GREEN);
  gfx->setTextSize(8);
  gfx->setCursor(x, 220);
  gfx->print(value);

  gfx->setTextColor(RGB565_WHITE);
  gfx->setTextSize(2);
  gfx->setCursor(110, 340);
  gfx->println("TURN KNOB  |  PUSH = 0");
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("OrbBuddy Clean M2");
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
  drawScreen();
}

void loop() {
  int32_t nowDetent = detent;

  if (nowDetent != lastDetent) {
    int32_t delta = nowDetent - lastDetent;
    lastDetent = nowDetent;
    shownValue += delta;

    Serial.printf("[knob] delta=%ld value=%ld\n", (long)delta, (long)shownValue);
    drawScreen();
  }

  bool button = digitalRead(PIN_KNOB_SW);

  if (lastButton == HIGH && button == LOW && millis() - lastButtonMs > 200) {
    lastButtonMs = millis();
    shownValue = 0;
    Serial.println("[knob] PUSH -> reset");
    drawScreen();
  }

  lastButton = button;
  delay(2);
}
