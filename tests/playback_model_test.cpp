// PlaybackModel opens ALAC (.m4a) tracks through the production decoder path.
//
// Before ALAC support, play() failed with "Decoder failed (...)" for these
// files. The audio device may legitimately be unavailable on a build machine,
// so only a decoder failure fails the test; playback is stopped immediately to
// avoid making noise on a developer's speakers.

#include "models/playback_model.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace {

void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

void playFixture(const fs::path& path, std::int64_t expected_ms)
{
    music::Track track;
    track.id = 1;
    track.path = path;
    track.title = path.stem().string();

    music::PlaybackModel model;
    model.setQueue({track});
    model.play(track);
    const music::PlaybackSnapshot snapshot = model.snapshot();
    model.stop();

    const std::string label = path.filename().string();
    require(snapshot.error.rfind("Decoder", 0) != 0, label + ": " + snapshot.error);
    // Duration comes from the opened decoder, not from the (zero) library value.
    require(snapshot.duration_ms >= expected_ms - 1 && snapshot.duration_ms <= expected_ms + 1,
            label + ": duration " + std::to_string(snapshot.duration_ms) + " ms");
    if (snapshot.state == music::PlaybackState::Error)
        std::cout << label << ": decoder OK, no audio device here (" << snapshot.error << ")\n";
    else
        require(snapshot.state == music::PlaybackState::Playing, label + ": not playing");
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: music_playback_model_tests <tests/fixtures/alac>\n";
        return 2;
    }
    try {
        const fs::path dir = argv[1];
        playFixture(dir / "apple-stereo-16.m4a", 204);  // 9000 frames at 44.1 kHz
        playFixture(dir / "ffmpeg-stereo-24.m4a", 94);  // 9000 frames at 96 kHz
    } catch (const std::exception& error) {
        std::cerr << "music_playback_model_test: FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << "music_playback_model_test: PASS\n";
    return 0;
}
