#pragma once
#include <lvgl.h>

namespace homeview {
    void init();
    lv_obj_t *screen();
    void onEnter();
    void onExit();
    void refresh();
}
