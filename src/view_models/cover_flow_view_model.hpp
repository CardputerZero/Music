#pragma once

#include "input/music_keys.hpp"
#include "models/music_library_model.hpp"
#include "view_models/view_model.hpp"

#include <memory>
#include <string>

namespace music {

class CoverFlowViewModel final : public ViewModel {
public:
    explicit CoverFlowViewModel(MusicLibraryModel& library);

    void onEnter() override;
    void onKey(std::uint32_t key, bool pressed) override;
    void update(float delta_seconds) override;

    std::shared_ptr<const LibrarySnapshot> snapshot() const;
    ScanState scanState() const;
    int selectedIndex() const noexcept;
    float animatedPosition() const noexcept;
    const Album* selectedAlbum() const noexcept;

private:
    MusicLibraryModel& _library;
    std::shared_ptr<const LibrarySnapshot> _snapshot;
    std::uint64_t _snapshot_revision = 0;
    std::string _selected_album_id;
    int _selected_index = 0;
    float _animated_position = 0.0f;
    float _selection_velocity = 0.0f;

    void refreshSnapshot();
    void selectRelative(int offset);
};

}  // namespace music
