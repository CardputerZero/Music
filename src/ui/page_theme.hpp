#pragma once

#include <cstdint>

namespace music::ui {

struct Color {
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
};

struct PageTheme {
    Color background;
    Color primary_text;
    Color secondary_text;
    Color accent;
};

constexpr PageTheme defaultPageTheme()
{
    return {
        {24, 27, 31},
        {246, 246, 247},
        {196, 199, 204},
        {63, 204, 117},
    };
}

}  // namespace music::ui
