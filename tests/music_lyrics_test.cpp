#include "core/music_lyrics.hpp"

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
    const music::LyricsDocument synchronized =
        music::parseLyrics("[ar:Artist]\n[00:01.25]First\n[00:04.500][00:06.00]Second\r\n");
    require(synchronized.synchronized, "timestamped LRC was not recognized");
    require(synchronized.lines.size() == 3, "multiple timestamps did not create multiple lyric lines");
    require(synchronized.lines[0].time_ms == 1250 && synchronized.lines[0].text == "First",
            "centisecond timestamp was parsed incorrectly");
    require(synchronized.lines[1].time_ms == 4500 && synchronized.lines[2].time_ms == 6000,
            "millisecond timestamps were parsed incorrectly");

    const music::LyricsDocument plain = music::parseLyrics("\xef\xbb\xbfLine one\n\nLine two\n");
    require(!plain.synchronized, "plain lyrics were incorrectly marked synchronized");
    require(plain.lines.size() == 2 && plain.lines[0].text == "Line one" && plain.lines[1].text == "Line two",
            "plain lyrics content was not preserved");

    const music::LyricsDocument metadata_only = music::parseLyrics("[ti:Title]\n[ar:Artist]\n");
    require(metadata_only.empty(), "metadata tags were exposed as lyric lines");
    return 0;
}
