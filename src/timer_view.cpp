#include "timer_view.h"
#include <cstdio>
#ifdef ARDUINO
#include <Arduino.h>
#endif
namespace {
lv_obj_t *s_screen=nullptr,*s_time=nullptr,*s_hint=nullptr; lv_timer_t *s_tick=nullptr;
int s_minutes=5; bool s_running=false; uint32_t s_end=0;
uint32_t nowms(){
#ifdef ARDUINO
 return millis();
#else
 return lv_tick_get();
#endif
}
void draw(){ if(!s_time)return; int sec=s_minutes*60; if(s_running){int32_t left=(int32_t)(s_end-nowms());sec=left>0?(left+999)/1000:0;if(left<=0){s_running=false;s_minutes=1;}} char b[16];snprintf(b,sizeof(b),"%02d:%02d",sec/60,sec%60);lv_label_set_text(s_time,b);lv_label_set_text(s_hint,s_running?"PUSH TO PAUSE":"TURN TO SET  |  PUSH TO START");}
void tick(lv_timer_t*){draw();}
}
void timerview::init(){if(s_screen)return;s_screen=lv_obj_create(nullptr);lv_obj_remove_style_all(s_screen);lv_obj_set_style_bg_color(s_screen,lv_color_black(),0);lv_obj_set_style_bg_opa(s_screen,LV_OPA_COVER,0);lv_obj_t*t=lv_label_create(s_screen);lv_label_set_text(t,"TIMER");lv_obj_set_style_text_color(t,lv_color_hex(0x19D3C5),0);lv_obj_set_style_text_font(t,&lv_font_montserrat_18,0);lv_obj_align(t,LV_ALIGN_TOP_MID,0,70);s_time=lv_label_create(s_screen);lv_obj_set_style_text_color(s_time,lv_color_white(),0);lv_obj_set_style_text_font(s_time,&lv_font_montserrat_48,0);lv_obj_center(s_time);s_hint=lv_label_create(s_screen);lv_obj_set_style_text_color(s_hint,lv_color_hex(0xA9B5B4),0);lv_obj_set_style_text_font(s_hint,&lv_font_montserrat_14,0);lv_obj_align(s_hint,LV_ALIGN_BOTTOM_MID,0,-85);s_tick=lv_timer_create(tick,250,nullptr);lv_timer_pause(s_tick);draw();}
lv_obj_t*timerview::screen(){return s_screen;} void timerview::onEnter(){if(s_tick)lv_timer_resume(s_tick);draw();} void timerview::onExit(){if(s_tick)lv_timer_pause(s_tick);}
void timerview::onTurn(int d){if(s_running)return;s_minutes+=d;if(s_minutes<1)s_minutes=1;if(s_minutes>120)s_minutes=120;draw();}
void timerview::onPress(){if(s_running){int32_t left=(int32_t)(s_end-nowms());s_minutes=left>0?(left+59999)/60000:1;s_running=false;}else{s_end=nowms()+(uint32_t)s_minutes*60000UL;s_running=true;}draw();}
