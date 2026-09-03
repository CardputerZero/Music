#pragma once

#include <algorithm>
#include <cstdint>

namespace music::playback_detail {

constexpr int adjustVolumePercent(int current_percent, int delta_percent) noexcept
{
    const std::int64_t requested = static_cast<std::int64_t>(current_percent) + delta_percent;
    return static_cast<int>(std::clamp<std::int64_t>(requested, 0, 100));
}

}  // namespace music::playback_detail
