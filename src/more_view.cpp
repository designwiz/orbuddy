#include "more_view.h"
#include "config.h"
#include <cstdio>
#ifdef ARDUINO
#include <WiFi.h>
#include <Arduino.h>
#endif
namespace{lv_obj_t*s_screen=nullptr,*s_info=nullptr;}
void moreview::init(){if(s_screen)return;s_screen=lv_obj_create(nullptr);lv_obj_remove_style_all(s_screen);lv_obj_set_style_bg_color(s_screen,lv_color_black(),0);lv_obj_set_style_bg_opa(s_screen,LV_OPA_COVER,0);lv_obj_t*t=lv_label_create(s_screen);lv_label_set_text(t,"MORE");lv_obj_set_style_text_color(t,lv_color_hex(0x19D3C5),0);lv_obj_set_style_text_font(t,&lv_font_montserrat_18,0);lv_obj_align(t,LV_ALIGN_TOP_MID,0,70);s_info=lv_label_create(s_screen);lv_obj_set_width(s_info,360);lv_obj_set_style_text_align(s_info,LV_TEXT_ALIGN_CENTER,0);lv_obj_set_style_text_color(s_info,lv_color_white(),0);lv_obj_set_style_text_font(s_info,&lv_font_montserrat_18,0);lv_obj_center(s_info);onEnter();}
lv_obj_t*moreview::screen(){return s_screen;}
void moreview::onEnter(){if(!s_info)return;char b[256];
#ifdef ARDUINO
bool up=WiFi.status()==WL_CONNECTED;String ip=up?WiFi.localIP().toString():String("--");snprintf(b,sizeof(b),"ORB BUDDY v0.2\n\nWiFi  %s\nIP  %s\n\nUptime  %lu min\n\ntheorb.local",up?"Connected":"Offline",ip.c_str(),(unsigned long)(millis()/60000UL));
#else
snprintf(b,sizeof(b),"ORB BUDDY v0.2\n\nSIMULATOR");
#endif
lv_label_set_text(s_info,b);}
