#pragma once

#include "models/music_library_model.hpp"

#include <filesystem>

namespace music {

struct MusicConfig {
    LibraryConfig library;
    int screen_width = 320;
    int screen_height = 170;
};

MusicConfig defaultMusicConfig();

}  // namespace music
