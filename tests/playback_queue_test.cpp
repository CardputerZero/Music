#include "models/playback_queue.hpp"

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

}  // namespace

int main()
{
    using music::playback_detail::adjacentQueueIndex;

    require(adjacentQueueIndex(0, 3, -1) == 2, "previous from the first track did not wrap to the last track");
    require(adjacentQueueIndex(2, 3, 1) == 0, "next from the last track did not wrap to the first track");
    require(adjacentQueueIndex(1, 3, -1) == 0, "previous from a middle track skipped its neighbor");
    require(adjacentQueueIndex(1, 3, 1) == 2, "next from a middle track skipped its neighbor");
    require(adjacentQueueIndex(0, 1, -1) == 0 && adjacentQueueIndex(0, 1, 1) == 0,
            "single-track queue did not remain on its only track");
    return 0;
}
