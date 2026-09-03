#include "models/playback_volume.hpp"

#include <climits>
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
    using music::playback_detail::adjustVolumePercent;

    require(adjustVolumePercent(50, 25) == 75, "positive volume delta was not applied");
    require(adjustVolumePercent(50, -25) == 25, "negative volume delta was not applied");
    require(adjustVolumePercent(95, 10) == 100, "volume increase did not clamp at 100 percent");
    require(adjustVolumePercent(5, -10) == 0, "volume decrease did not clamp at 0 percent");
    require(adjustVolumePercent(42, 0) == 42, "zero volume delta changed the volume");
    require(adjustVolumePercent(50, INT_MAX) == 100, "large positive volume delta overflowed");
    require(adjustVolumePercent(50, INT_MIN) == 0, "large negative volume delta overflowed");
    return 0;
}
