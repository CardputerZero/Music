#pragma once

#include <cstddef>

namespace music::playback_detail {

constexpr std::size_t adjacentQueueIndex(std::size_t current_index, std::size_t queue_size, int direction) noexcept
{
    if (queue_size == 0 || current_index >= queue_size || direction == 0) {
        return current_index;
    }
    if (direction < 0) {
        return current_index == 0 ? queue_size - 1 : current_index - 1;
    }
    return current_index + 1 == queue_size ? 0 : current_index + 1;
}

}  // namespace music::playback_detail
