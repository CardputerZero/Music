#pragma once

#include <lvgl.h>

namespace music {

class View {
public:
    virtual ~View() = default;
    virtual void onEnter(lv_obj_t* parent) { (void)parent; }
    virtual void onExit() {}
    virtual void update(float delta_seconds) = 0;
    virtual void draw() = 0;
};

}  // namespace music
