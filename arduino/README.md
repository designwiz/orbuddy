# OrbBuddy Clean — Arduino

Fresh Arduino implementation for the Waveshare ESP32-S3 Touch AMOLED 1.75.

This branch deliberately does **not** use the old Orb OS app shell, simulator, themes, or UI architecture.

## Milestones

1. **M1 — AMOLED:** initialise the CO5300 and display ORB BUDDY.
2. M2 — rotary knob.
3. M3 — five-screen navigation.
4. M4 — Wi-Fi + setup page.
5. M5 — time/weather.
6. M6 — ADS-B radar.
7. M7 — timer + MORE/admin.

## Arduino IDE settings

- Board: ESP32S3 Dev Module
- Flash Size: 16MB
- PSRAM: OPI PSRAM
- USB CDC On Boot: Enabled
- Library: Arduino_GFX

## M1

Open `arduino/OrbBuddy/OrbBuddy.ino`, compile and upload from Arduino IDE.

The first test intentionally has no Wi-Fi, LVGL, weather, ADS-B, touch, or knob code. If the display comes up, the clean foundation is proven before anything else is added.
