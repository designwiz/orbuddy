/*
  OrbBuddy Clean - M1.3
  Exact AMOLED hardware bring-up copied from the known-working Orb firmware.
  No LVGL, app shell, simulator, Wi-Fi, weather or radar.
*/

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

#define SCREEN_W           466
#define SCREEN_H           466
#define LCD_COL_OFFSET       6
#define LCD_ROW_OFFSET       0
#define LCD_QSPI_HZ   80000000
#define BRIGHTNESS_DEFAULT 200

#define PIN_LCD_CS   12
#define PIN_LCD_RST  39
#define PIN_LCD_SCLK 38
#define PIN_LCD_D0    4
#define PIN_LCD_D1    5
#define PIN_LCD_D2    6
#define PIN_LCD_D3    7

static Arduino_DataBus *s_bus = nullptr;
static Arduino_CO5300  *s_gfx = nullptr;

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("OrbBuddy Clean M1.3");\n  Serial.printf("Arduino core: %s\\n", ESP_ARDUINO_VERSION_STR);

  // EXACT construction and begin sequence used by the known-working Orb firmware.
  Serial.println("[display] init CO5300 QSPI...");
  s_bus = new Arduino_ESP32QSPI(PIN_LCD_CS, PIN_LCD_SCLK,
                                PIN_LCD_D0, PIN_LCD_D1, PIN_LCD_D2, PIN_LCD_D3);
  s_gfx = new Arduino_CO5300(s_bus, PIN_LCD_RST, 0,
                             SCREEN_W, SCREEN_H,
                             LCD_COL_OFFSET, LCD_ROW_OFFSET, 0, 0);

  if (!s_gfx->begin(LCD_QSPI_HZ)) {
    Serial.println("[display] gfx->begin() FAILED");
    while (true) delay(1000);
  }

  s_gfx->fillScreen(RGB565_BLACK);
  s_gfx->setBrightness(BRIGHTNESS_DEFAULT);
  Serial.println("[display] panel up");

  // Direct Arduino_GFX test only. If this is not black/white/cyan on the glass,
  // the remaining difference is the Arduino_GFX library/build environment itself.
  s_gfx->setTextColor(RGB565_CYAN);
  s_gfx->setTextSize(3);
  s_gfx->setCursor(135, 190);
  s_gfx->println("ORB BUDDY");

  s_gfx->setTextColor(RGB565_WHITE);
  s_gfx->setTextSize(2);
  s_gfx->setCursor(150, 235);
  s_gfx->println("CLEAN M1.3");

  Serial.println("[display] direct draw complete");
}

void loop() {
  delay(1000);
}
