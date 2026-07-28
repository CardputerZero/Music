#pragma once

#include "core/music_lyrics.hpp"
#include "models/music_library_model.hpp"
#include "models/playback_model.hpp"
#include "view_models/view_model.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace music {

class PlaybackViewModel final : public ViewModel {
public:
    PlaybackViewModel(MusicLibraryModel& library, PlaybackModel& playback);

    void onEnter() override;
    void onKey(std::uint32_t key, bool pressed) override;
    void update(float delta_seconds) override;

    const Track* track() const noexcept;
    PlaybackSnapshot playbackSnapshot() const noexcept;
    const LyricsDocument& lyrics() const noexcept;
    std::uint64_t lyricsRevision() const noexcept;
    std::size_t currentLyricIndex() const noexcept;
    bool hasLyrics() const noexcept;
    bool fullscreen() const noexcept;

private:
    MusicLibraryModel& _library;
    PlaybackModel& _playback;
    std::shared_ptr<const LibrarySnapshot> _library_snapshot;
    PlaybackSnapshot _playback_snapshot;
    LyricsDocument _lyrics;
    std::int64_t _lyrics_track_id = 0;
    std::uint64_t _lyrics_revision = 0;
    std::uint64_t _library_revision = 0;
    std::uint32_t _pressed_key = 0;
    bool _fullscreen = false;

    void refresh();
    void loadLyrics();
    void activate(std::uint32_t key);
};

}  // namespace music
