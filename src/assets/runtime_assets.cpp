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

std::filesystem::path defaultCoverPath() { return assetPath("covers/all-music.jpg"); }

std::filesystem::path displayCoverPath(const std::filesystem::path& preferred_path)
{
    std::error_code error;
    if (!preferred_path.empty() && std::filesystem::is_regular_file(preferred_path, error) && !error) {
        return preferred_path;
    }
    return defaultCoverPath();
}

}  // namespace music
