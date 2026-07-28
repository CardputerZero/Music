#pragma once

#include <lvgl.h>

namespace music {

enum class FontFamily {
    Sans,
};

enum class FontSize {
    Px12 = 12,
    Px14 = 14,
    Px18 = 18,
};

void initFontAssets();
void shutdownFontAssets();
const lv_font_t* font(FontFamily family, FontSize size);

}  // namespace music
