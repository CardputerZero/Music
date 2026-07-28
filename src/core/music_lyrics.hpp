#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace music {

struct LyricLine {
    std::int64_t time_ms = -1;
    std::string text;
};

struct LyricsDocument {
    std::vector<LyricLine> lines;
    bool synchronized = false;

    bool empty() const noexcept { return lines.empty(); }
};

LyricsDocument parseLyrics(const std::string& contents);

}  // namespace music
