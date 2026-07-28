#pragma once

namespace music::display {

inline constexpr int kSupersampleScale = 1;

constexpr int supersampled(int logical_pixels) noexcept { return logical_pixels * kSupersampleScale; }

constexpr float supersampled(float logical_pixels) noexcept
{
    return logical_pixels * static_cast<float>(kSupersampleScale);
}

}  // namespace music::display
