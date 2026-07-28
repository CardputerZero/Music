#pragma once

#include "models/music_library_model.hpp"
#include "models/playback_model.hpp"
#include "view_models/view_model.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace music {

class AlbumListViewModel final : public ViewModel {
public:
    AlbumListViewModel(MusicLibraryModel& library, PlaybackModel& playback);

    void setAlbumId(std::string album_id);
    void onEnter() override;
    void onKey(std::uint32_t key, bool pressed) override;
    void update(float delta_seconds) override;

    std::shared_ptr<const LibrarySnapshot> snapshot() const;
    const Album* album() const noexcept;
    std::size_t trackCount() const noexcept;
    const Track* trackAt(std::size_t index) const noexcept;
    const Track* selectedTrack() const noexcept;
    int selectedIndex() const noexcept;
    bool enterPressed() const noexcept;
    bool takeAlbumInfoRequested() noexcept;
    PlaybackSnapshot playbackSnapshot() const;

private:
    MusicLibraryModel& _library;
    PlaybackModel& _playback;
    std::shared_ptr<const LibrarySnapshot> _snapshot;
    std::vector<std::size_t> _track_indices;
    std::string _album_id;
    std::int64_t _selected_track_id = 0;
    std::uint64_t _snapshot_revision = 0;
    int _selected_index = -1;
    int _held_direction = 0;
    float _held_seconds = 0.0f;
    float _repeat_seconds = 0.0f;
    bool _enter_pressed = false;
    bool _album_info_requested = false;
    bool _select_first_track_pending = false;

    void refreshSnapshot();
    void rebuildTrackIndices();
    void selectRelative(int offset);
    void activateSelected();
};

}  // namespace music
