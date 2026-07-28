#include "view_models/cover_flow_view_model.hpp"

#include <algorithm>
#include <cmath>

namespace music {
namespace {

constexpr float kSpringStiffness = 145.0f;
constexpr float kSpringDamping = 20.0f;

}  // namespace

CoverFlowViewModel::CoverFlowViewModel(MusicLibraryModel& library) : _library(library) {}

void CoverFlowViewModel::onEnter() { refreshSnapshot(); }

void CoverFlowViewModel::onKey(std::uint32_t key, bool pressed)
{
    if (!pressed) {
        return;
    }

    if (key == music_key::Left) {
        selectRelative(-1);
    } else if (key == music_key::Right) {
        selectRelative(1);
    }
}

void CoverFlowViewModel::update(float delta_seconds)
{
    refreshSnapshot();

    const float dt = std::clamp(delta_seconds, 0.0f, 1.0f / 30.0f);
    const float target = static_cast<float>(_selected_index);
    const float displacement = target - _animated_position;
    const float acceleration = displacement * kSpringStiffness - _selection_velocity * kSpringDamping;
    _selection_velocity += acceleration * dt;
    _animated_position += _selection_velocity * dt;

    if (std::abs(displacement) < 0.0005f && std::abs(_selection_velocity) < 0.002f) {
        _animated_position = target;
        _selection_velocity = 0.0f;
    }
}

std::shared_ptr<const LibrarySnapshot> CoverFlowViewModel::snapshot() const { return _snapshot; }

ScanState CoverFlowViewModel::scanState() const { return _library.scanState(); }

int CoverFlowViewModel::selectedIndex() const noexcept { return _selected_index; }

float CoverFlowViewModel::animatedPosition() const noexcept { return _animated_position; }

const Album* CoverFlowViewModel::selectedAlbum() const noexcept
{
    if (!_snapshot || _selected_index < 0 || _selected_index >= static_cast<int>(_snapshot->albums.size())) {
        return nullptr;
    }
    return &_snapshot->albums[static_cast<std::size_t>(_selected_index)];
}

void CoverFlowViewModel::refreshSnapshot()
{
    auto next = _library.snapshot();
    if (!next || (_snapshot && next->revision == _snapshot_revision)) {
        return;
    }

    if (_snapshot && _selected_index >= 0 && _selected_index < static_cast<int>(_snapshot->albums.size())) {
        _selected_album_id = _snapshot->albums[static_cast<std::size_t>(_selected_index)].id;
    }

    _snapshot = std::move(next);
    _snapshot_revision = _snapshot->revision;

    if (_snapshot->albums.empty()) {
        _selected_index = 0;
        _animated_position = 0.0f;
        _selection_velocity = 0.0f;
        _selected_album_id.clear();
        return;
    }

    const auto selected = std::find_if(_snapshot->albums.begin(), _snapshot->albums.end(),
                                       [this](const Album& album) { return album.id == _selected_album_id; });
    if (selected != _snapshot->albums.end()) {
        _selected_index = static_cast<int>(std::distance(_snapshot->albums.begin(), selected));
    } else {
        _selected_index = std::clamp(_selected_index, 0, static_cast<int>(_snapshot->albums.size()) - 1);
    }
    _selected_album_id = _snapshot->albums[static_cast<std::size_t>(_selected_index)].id;
}

void CoverFlowViewModel::selectRelative(int offset)
{
    if (!_snapshot || _snapshot->albums.empty()) {
        return;
    }

    const int last = static_cast<int>(_snapshot->albums.size()) - 1;
    _selected_index = std::clamp(_selected_index + offset, 0, last);
    _selected_album_id = _snapshot->albums[static_cast<std::size_t>(_selected_index)].id;
}

}  // namespace music
