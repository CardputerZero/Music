#include "core/music_config.hpp"

#include <cstdlib>
#include <sstream>
#include <string>

namespace music {
namespace {

std::string environment(const char* name)
{
    const char* value = std::getenv(name);
    return value && value[0] != '\0' ? value : "";
}

std::filesystem::path homeDirectory()
{
    const std::string home = environment("HOME");
    return home.empty() ? std::filesystem::current_path() : std::filesystem::path(home);
}

std::filesystem::path xdgDirectory(const char* variable, const std::filesystem::path& fallback)
{
    const std::string value = environment(variable);
    return value.empty() ? fallback : std::filesystem::path(value);
}

std::vector<std::filesystem::path> musicRoots()
{
    const std::string configured = environment("MUSIC_LIBRARY_DIRS");
    if (configured.empty()) {
        const std::string xdg_music = environment("XDG_MUSIC_DIR");
        if (!xdg_music.empty()) {
            return {xdg_music};
        }
        return {homeDirectory() / "Music"};
    }

    std::vector<std::filesystem::path> roots;
    std::stringstream stream(configured);
    std::string root;
    while (std::getline(stream, root, ':')) {
        if (!root.empty()) {
            roots.emplace_back(root);
        }
    }
    if (roots.empty()) {
        roots.emplace_back(homeDirectory() / "Music");
    }
    return roots;
}

}  // namespace

MusicConfig defaultMusicConfig()
{
    const auto home = homeDirectory();
    const auto data_root = xdgDirectory("XDG_DATA_HOME", home / ".local" / "share") / "Music";
    const auto cache_root = xdgDirectory("XDG_CACHE_HOME", home / ".cache") / "Music";

    MusicConfig config;
    config.library.roots = musicRoots();
    config.library.database_path = data_root / "library.db";
    config.library.cache_dir = cache_root;
    return config;
}

}  // namespace music
