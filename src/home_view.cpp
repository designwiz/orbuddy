#include "home_view.h"
#include "weather.h"
#include "config.h"
#include <cstdio>
#include <ctime>

#ifdef ARDUINO
#include <WiFi.h>
#include <Arduino.h>
#endif

namespace {
lv_obj_t *s_screen = nullptr;
lv_obj_t *s_place = nullptr;
lv_obj_t *s_time = nullptr;
lv_obj_t *s_date = nullptr;
lv_obj_t *s_weather = nullptr;
lv_obj_t *s_status = nullptr;
lv_timer_t *s_timer = nullptr;

void style_label(lv_obj_t *o, const lv_font_t *font, uint32_t colour) {
    lv_obj_set_style_text_font(o, font, 0);
    lv_obj_set_style_text_color(o, lv_color_hex(colour), 0);
    lv_obj_set_style_text_align(o, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(o, 420);
}

void tick(lv_timer_t *) { homeview::refresh(); }
}

void homeview::init() {
    if (s_screen) return;

    s_screen = lv_obj_create(nullptr);
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_style_bg_color(s_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    // Thin circular accent: deliberately subtle on AMOLED.
    lv_obj_t *ring = lv_obj_create(s_screen);
    lv_obj_remove_style_all(ring);
    lv_obj_set_size(ring, 438, 438);
    lv_obj_center(ring);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ring, 2, 0);
    lv_obj_set_style_border_color(ring, lv_color_hex(0x19D3C5), 0);
    lv_obj_set_style_border_opa(ring, LV_OPA_40, 0);

    s_place = lv_label_create(s_screen);
    lv_label_set_text(s_place, "ORBBUDDY");
    style_label(s_place, &lv_font_montserrat_18, 0x19D3C5);
    lv_obj_align(s_place, LV_ALIGN_TOP_MID, 0, 72);

    s_time = lv_label_create(s_screen);
    lv_label_set_text(s_time, "--:--");
    style_label(s_time, &lv_font_montserrat_48, 0xFFFFFF);
    lv_obj_align(s_time, LV_ALIGN_CENTER, 0, -50);

    s_date = lv_label_create(s_screen);
    lv_label_set_text(s_date, "--- -- ---");
    style_label(s_date, &lv_font_montserrat_18, 0xA9B5B4);
    lv_obj_align(s_date, LV_ALIGN_CENTER, 0, 4);

    s_weather = lv_label_create(s_screen);
    lv_label_set_text(s_weather, "WEATHER --");
    style_label(s_weather, &lv_font_montserrat_22, 0xFFFFFF);
    lv_obj_align(s_weather, LV_ALIGN_CENTER, 0, 62);

    s_status = lv_label_create(s_screen);
    lv_label_set_text(s_status, "ORBBUDDY v0.1");
    style_label(s_status, &lv_font_montserrat_14, 0x687674);
    lv_obj_align(s_status, LV_ALIGN_BOTTOM_MID, 0, -72);

    s_timer = lv_timer_create(tick, 1000, nullptr);
    lv_timer_pause(s_timer);
    refresh();
}

lv_obj_t *homeview::screen() { return s_screen; }

void homeview::onEnter() {
    refresh();
    if (s_timer) lv_timer_resume(s_timer);
}

void homeview::onExit() {
    if (s_timer) lv_timer_pause(s_timer);
}

void homeview::refresh() {
    if (!s_screen) return;

    time_t now = time(nullptr);
    struct tm tmv = {};
#if defined(_WIN32)
    localtime_s(&tmv, &now);
#else
    localtime_r(&now, &tmv);
#endif
    if (now > 100000) {
        char t[8], d[24];
        strftime(t, sizeof(t), "%H:%M", &tmv);
        strftime(d, sizeof(d), "%a %d %b", &tmv);
        for (char *p=d; *p; ++p) if (*p >= 'a' && *p <= 'z') *p -= 32;
        lv_label_set_text(s_time, t);
        lv_label_set_text(s_date, d);
    }

    WeatherSnapshot wx = {};
    if (weather_get(wx) && wx.valid) {
        char line[96];
        snprintf(line, sizeof(line), "%.0f\xC2\xB0""C  %s", wx.tempC, weather_condition(wx.code));
        lv_label_set_text(s_weather, line);
    } else {
        lv_label_set_text(s_weather, "WEATHER --");
    }

#ifdef ARDUINO
    char status[96];
    if (WiFi.status() == WL_CONNECTED)
        snprintf(status, sizeof(status), "WIFI  %s", WiFi.localIP().toString().c_str());
    else
        snprintf(status, sizeof(status), "WIFI  OFFLINE");
    lv_label_set_text(s_status, status);
#else
    lv_label_set_text(s_status, "ORBBUDDY v0.1  •  SIM");
#endif
}
