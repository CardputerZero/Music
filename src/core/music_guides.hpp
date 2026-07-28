#pragma once

#include "core/music_types.hpp"

#include <array>

namespace music {

struct MusicGuide {
    GuideTopic topic = GuideTopic::None;
    const char* album_id = "";
    const char* album_title = "";
    const char* album_subtitle = "";
    const char* cover_relative_path = "";
    const char* page_title = "";
    const char* page_body = "";
};

const std::array<MusicGuide, 3>& musicGuides();
const std::array<Track, 3>& exampleTracks();
const MusicGuide* findMusicGuide(GuideTopic topic);
Album makeAllMusicAlbum();
Album makeGuideAlbum(const MusicGuide& guide);

}  // namespace music
