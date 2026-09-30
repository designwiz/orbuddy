#pragma once
#include <lvgl.h>
namespace timerview { void init(); lv_obj_t *screen(); void onEnter(); void onExit(); void onTurn(int delta); void onPress(); }
