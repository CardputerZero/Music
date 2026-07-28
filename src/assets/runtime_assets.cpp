#include "assets/runtime_assets.hpp"

#ifndef MUSIC_ASSETS_PATH
#define MUSIC_ASSETS_PATH ""
#endif

namespace music {

std::filesystem::path assetPath(const std::filesystem::path& relative_path)
{
    if (MUSIC_ASSETS_PATH[0] == '\0') {
        return {};
    }
    return std::filesystem::path(MUSIC_ASSETS_PATH) / relative_path;
}

}  // namespace music
