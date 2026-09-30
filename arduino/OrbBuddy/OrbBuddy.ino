/*
  OrbBuddy Clean - M1 AMOLED bring-up
  Waveshare ESP32-S3 Touch AMOLED 1.75" / CO5300 466x466

  Arduino IDE:
    Board: ESP32S3 Dev Module
    Flash Size: 16MB
    PSRAM: OPI PSRAM
    USB CDC On Boot: Enabled

  Library:
    Arduino_GFX

  M1 goal: prove the clean sketch can initialise the AMOLED with no Orb OS.
*/

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

static constexpr int LCD_CS   = 12;
static constexpr int LCD_RST  = 39;
static constexpr int LCD_SCLK = 38;
static constexpr int LCD_D0   = 4;
static constexpr int LCD_D1   = 5;
static constexpr int LCD_D2   = 6;
static constexpr int LCD_D3   = 7;

static constexpr int SCREEN_W = 466;
static constexpr int SCREEN_H = 466;
static constexpr int COL_OFFSET = 6;
static constexpr int ROW_OFFSET = 0;
static constexpr uint32_t LCD_HZ = 80000000;

Arduino_DataBus *bus = new Arduino_ESP32QSPI(
  LCD_CS, LCD_SCLK, LCD_D0, LCD_D1, LCD_D2, LCD_D3
);

Arduino_CO5300 *display = new Arduino_CO5300(
  bus, LCD_RST, 0,
  SCREEN_W, SCREEN_H,
  COL_OFFSET, ROW_OFFSET, 0, 0
);

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("OrbBuddy Clean M1");

  if (!display->begin(LCD_HZ)) {
    Serial.println("DISPLAY INIT FAILED");
    while (true) delay(1000);
  }

  display->setBrightness(200);
  display->fillScreen(RGB565_BLACK);

  display->setTextColor(RGB565_CYAN);
  display->setTextSize(3);
  display->setCursor(135, 190);
  display->println("ORB BUDDY");

  display->setTextColor(RGB565_WHITE);
  display->setTextSize(2);
  display->setCursor(160, 235);
  display->println("CLEAN M1");

  display->setTextColor(RGB565_GREEN);
  display->setTextSize(1);
  display->setCursor(166, 275);
  display->println("AMOLED  OK");

  Serial.println("AMOLED OK");
}

void loop() {
  delay(1000);
}
