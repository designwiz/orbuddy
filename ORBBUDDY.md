# OrbBuddy

OrbBuddy is a clean, modern firmware fork for the Waveshare ESP32-S3 Touch AMOLED 1.75, based on The Orb OS.

## v0.1 direction

Initial app set:

- HOME — large digital clock, date, weather summary and connection status
- WEATHER — current conditions and short forecast
- RADAR — live ADS-B traffic, with room for favourite-aircraft alerts
- TIMER — knob-driven quick timer
- MORE — Wi-Fi, IP address, brightness, volume, location, updates and About

## UI principles

- Designed specifically for the 466x466 round AMOLED
- True black backgrounds wherever practical
- Large, glanceable typography
- Minimal visual clutter
- Colour used as an app/status accent rather than decoration
- Rotary knob remains the primary interaction
- Preserve the proven Orb OS hardware, networking, display and OTA layers

## First milestone

OrbBuddy v0.1 should boot to a recognisably different HOME screen while retaining reliable Wi-Fi, NTP, display, knob and OTA behaviour.

Development happens on the `orbbuddy-v0.1` branch until the first usable build is ready.
