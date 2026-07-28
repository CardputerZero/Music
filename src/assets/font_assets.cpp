#include "assets/font_assets.hpp"

#include "assets/assets.h"

namespace music {
namespace {

bool g_initialized = false;
lv_font_t g_sans_sc_12;
lv_font_t g_sans_jp_12;
lv_font_t g_sans_sc_14;
lv_font_t g_sans_jp_14;
lv_font_t g_sans_sc_18;
lv_font_t g_sans_jp_18;

}  // namespace

void initFontAssets()
{
    if (g_initialized) {
        return;
    }

    g_sans_sc_12 = font_noto_sans_sc_semibold_12;
    g_sans_jp_12 = font_noto_sans_jp_semibold_12;
    g_sans_sc_14 = font_noto_sans_sc_semibold_14;
    g_sans_jp_14 = font_noto_sans_jp_semibold_14;
    g_sans_sc_18 = font_noto_sans_sc_semibold_18;
    g_sans_jp_18 = font_noto_sans_jp_semibold_18;

    g_sans_sc_12.fallback = &g_sans_jp_12;
    g_sans_sc_14.fallback = &g_sans_jp_14;
    g_sans_sc_18.fallback = &g_sans_jp_18;
    g_initialized = true;
}

void shutdownFontAssets() { g_initialized = false; }

const lv_font_t* font(FontFamily family, FontSize size)
{
    initFontAssets();
    switch (family) {
        case FontFamily::Sans:
            switch (size) {
                case FontSize::Px12:
                    return &g_sans_sc_12;
                case FontSize::Px14:
                    return &g_sans_sc_14;
                case FontSize::Px18:
                    return &g_sans_sc_18;
            }
    }
    return &lv_font_montserrat_14;
}

}  // namespace music
