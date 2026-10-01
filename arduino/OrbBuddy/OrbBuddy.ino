/*
  OrbBuddy Clean - M1 AMOLED bring-up
  Waveshare ESP32-S3 Touch AMOLED 1.75" / CO5300 466x466

  This version intentionally follows Waveshare's official HelloWorld
  display initialisation as closely as possible.
*/

#include <Arduino.h>
#include <Wire.h>
#include <Arduino_GFX_Library.h>

static constexpr int LCD_CS   = 12;
static constexpr int LCD_RST  = 39;
static constexpr int LCD_SCLK = 38;
static constexpr int LCD_D0   = 4;
static constexpr int LCD_D1   = 5;
static constexpr int LCD_D2   = 6;
static constexpr int LCD_D3   = 7;

static constexpr int I2C_SDA = 15;
static constexpr int I2C_SCL = 14;

static constexpr int SCREEN_W = 466;
static constexpr int SCREEN_H = 466;

Arduino_DataBus *bus = new Arduino_ESP32QSPI(
  LCD_CS, LCD_SCLK, LCD_D0, LCD_D1, LCD_D2, LCD_D3
);

Arduino_CO5300 *gfx = new Arduino_CO5300(
  bus, LCD_RST, 0,
  SCREEN_W, SCREEN_H,
  6, 0, 0, 0
);

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("OrbBuddy Clean M1.1");

  // Waveshare's official example initialises the shared board I2C bus first.
  Wire.begin(I2C_SDA, I2C_SCL);

  // IMPORTANT: do not force an 80 MHz QSPI clock here.
  // Use the same default begin() path as Waveshare's HelloWorld example.
  if (!gfx->begin()) {
    Serial.println("gfx->begin() failed!");
    while (true) delay(1000);
  }

  gfx->fillScreen(RGB565_BLACK);
  gfx->setBrightness(128);

  // Keep this first visual test deliberately close to the factory example.
  gfx->setCursor(10, 10);
  gfx->setTextColor(RGB565_RED);
  gfx->setTextSize(2);
  gfx->println("ORB BUDDY");

  gfx->setTextColor(RGB565_WHITE);
  gfx->println("CLEAN M1.1");

  gfx->setTextColor(RGB565_GREEN);
  gfx->println("AMOLED OK");

  Serial.println("AMOLED draw commands sent");
}

void loop() {
  delay(1000);
}
