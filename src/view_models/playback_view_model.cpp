#include "view_models/playback_view_model.hpp"

#include "input/music_keys.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <utility>

namespace music {
namespace {

bool isPlaybackKey(std::uint32_t key)
{
    return key == music_key::Key4 || key == music_key::Key5 || key == music_key::Key6 || key == music_key::Key7 ||
           key == music_key::Key8 || key == music_key::PlayPause || key == music_key::Previous ||
           key == music_key::Next;
}

std::string readTextFile(const std::filesystem::path& path)
{
    if (path.empty()) {
        return {};
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return {};
    }
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

}  // namespace

PlaybackViewModel::PlaybackViewModel(MusicLibraryModel& library, PlaybackModel& playback)
    : _library(library), _playback(playback)
{
}

void PlaybackViewModel::onEnter()
{
    _pressed_key = 0;
    _fullscreen = false;
    _library_snapshot.reset();
    _library_revision = 0;
    refresh();
}

void PlaybackViewModel::onKey(std::uint32_t key, bool pressed)
{
    if (!isPlaybackKey(key)) {
        return;
    }
    if (pressed) {
        _pressed_key = key;
        return;
    }
    if (_pressed_key != key) {
        return;
    }
    _pressed_key = 0;
    activate(key);
    refresh();
}

void PlaybackViewModel::update(float delta_seconds)
{
    (void)delta_seconds;
    refresh();
}

const Track* PlaybackViewModel::track() const noexcept
{
    if (!_library_snapshot || !_playback_snapshot.hasTrack()) {
        return nullptr;
    }
    const auto found = std::find_if(
        _library_snapshot->tracks.begin(), _library_snapshot->tracks.end(), [this](const Track& candidate) {
            return candidate.id == _playback_snapshot.track_id && candidate.path == _playback_snapshot.path;
        });
    return found == _library_snapshot->tracks.end() ? nullptr : &*found;
}

PlaybackSnapshot PlaybackViewModel::playbackSnapshot() const noexcept { return _playback_snapshot; }

const LyricsDocument& PlaybackViewModel::lyrics() const noexcept { return _lyrics; }

std::uint64_t PlaybackViewModel::lyricsRevision() const noexcept { return _lyrics_revision; }

bool PlaybackViewModel::hasLyrics() const noexcept { return !_lyrics.empty(); }

bool PlaybackViewModel::fullscreen() const noexcept { return _fullscreen; }

void PlaybackViewModel::refresh()
{
    auto next_library = _library.snapshot();
    bool library_changed = false;
    if (next_library && (!_library_snapshot || next_library->revision != _library_revision)) {
        _library_revision = next_library->revision;
        _library_snapshot = std::move(next_library);
        library_changed = true;
    }

    const std::int64_t previous_track_id = _playback_snapshot.track_id;
    _playback_snapshot = _playback.snapshot();
    if (library_changed || _playback_snapshot.track_id != previous_track_id ||
        _lyrics_track_id != _playback_snapshot.track_id) {
        loadLyrics();
    }
}

void PlaybackViewModel::loadLyrics()
{
    _lyrics = {};
    _lyrics_track_id = _playback_snapshot.track_id;
    ++_lyrics_revision;
    const Track* current = track();
    if (!current) {
        return;
    }

    std::string contents = readTextFile(current->lyrics_path);
    if (contents.empty()) {
        contents = current->embedded_lyrics;
    }
    if (!contents.empty()) {
        _lyrics = parseLyrics(contents);
    }
}

std::size_t PlaybackViewModel::currentLyricIndex() const noexcept
{
    if (_lyrics.lines.size() <= 1) {
        return 0;
    }
    if (!_lyrics.synchronized) {
        if (_playback_snapshot.duration_ms <= 0) {
            return 0;
        }
        const double progress = std::clamp(
            static_cast<double>(_playback_snapshot.position_ms) / static_cast<double>(_playback_snapshot.duration_ms),
            0.0, 1.0);
        return std::min(_lyrics.lines.size() - 1,
                        static_cast<std::size_t>(progress * static_cast<double>(_lyrics.lines.size())));
    }

    const auto next =
        std::upper_bound(_lyrics.lines.begin(), _lyrics.lines.end(), _playback_snapshot.position_ms,
                         [](std::int64_t position_ms, const LyricLine& line) { return position_ms < line.time_ms; });
    return next == _lyrics.lines.begin() ? 0 : static_cast<std::size_t>(std::distance(_lyrics.lines.begin(), next) - 1);
}

void PlaybackViewModel::activate(std::uint32_t key)
{
    if (key == music_key::Key4) {
        _fullscreen = !_fullscreen;
    } else if (key == music_key::Key5 || key == music_key::Previous) {
        _playback.previous();
    } else if (key == music_key::Key6 || key == music_key::PlayPause) {
        _playback.toggleCurrent();
    } else if (key == music_key::Key7 || key == music_key::Next) {
        _playback.next();
    } else if (key == music_key::Key8) {
        _playback.cycleMode();
    }
}

}  // namespace music
