#include "view_models/album_list_view_model.hpp"

#include "input/music_keys.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace music {
namespace {

constexpr float kHoldDelaySeconds = 0.32f;
constexpr float kRepeatSeconds = 0.09f;

std::string lowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return value;
}

std::string albumArtist(const Track& track) { return track.album_artist.empty() ? track.artist : track.album_artist; }

}  // namespace

AlbumListViewModel::AlbumListViewModel(MusicLibraryModel& library, PlaybackModel& playback)
    : _library(library), _playback(playback)
{
}

void AlbumListViewModel::setAlbumId(std::string album_id)
{
    if (_album_id == album_id) {
        return;
    }
    _album_id = std::move(album_id);
    _selected_index = 0;
    _selected_track_id = 0;
    _select_first_track_pending = true;
    _snapshot.reset();
    _snapshot_revision = 0;
}

void AlbumListViewModel::onEnter()
{
    _held_direction = 0;
    _held_seconds = 0.0f;
    _repeat_seconds = 0.0f;
    _enter_pressed = false;
    _album_info_requested = false;
    refreshSnapshot();
}

void AlbumListViewModel::onKey(std::uint32_t key, bool pressed)
{
    if (key == music_key::Enter) {
        if (pressed) {
            _enter_pressed = true;
        } else if (_enter_pressed) {
            _enter_pressed = false;
            activateSelected();
        }
        return;
    }

    int direction = 0;
    if (key == music_key::Up) {
        direction = -1;
    } else if (key == music_key::Down) {
        direction = 1;
    } else {
        return;
    }

    if (pressed) {
        selectRelative(direction);
        _held_direction = direction;
        _held_seconds = 0.0f;
        _repeat_seconds = 0.0f;
    } else if (_held_direction == direction) {
        _held_direction = 0;
    }
}

void AlbumListViewModel::update(float delta_seconds)
{
    refreshSnapshot();
    if (_held_direction == 0) {
        return;
    }

    const float elapsed = std::clamp(delta_seconds, 0.0f, 0.1f);
    _held_seconds += elapsed;
    if (_held_seconds < kHoldDelaySeconds) {
        return;
    }

    _repeat_seconds += elapsed;
    while (_repeat_seconds >= kRepeatSeconds) {
        selectRelative(_held_direction);
        _repeat_seconds -= kRepeatSeconds;
    }
}

std::shared_ptr<const LibrarySnapshot> AlbumListViewModel::snapshot() const { return _snapshot; }

const Album* AlbumListViewModel::album() const noexcept
{
    if (!_snapshot) {
        return nullptr;
    }
    const auto found = std::find_if(_snapshot->albums.begin(), _snapshot->albums.end(),
                                    [this](const Album& candidate) { return candidate.id == _album_id; });
    return found == _snapshot->albums.end() ? nullptr : &*found;
}

std::size_t AlbumListViewModel::trackCount() const noexcept { return _track_indices.size(); }

const Track* AlbumListViewModel::trackAt(std::size_t index) const noexcept
{
    if (!_snapshot || index >= _track_indices.size() || _track_indices[index] >= _snapshot->tracks.size()) {
        return nullptr;
    }
    return &_snapshot->tracks[_track_indices[index]];
}

const Track* AlbumListViewModel::selectedTrack() const noexcept
{
    return _selected_index < 0 ? nullptr : trackAt(static_cast<std::size_t>(_selected_index));
}

int AlbumListViewModel::selectedIndex() const noexcept { return _selected_index; }

bool AlbumListViewModel::enterPressed() const noexcept { return _enter_pressed; }

bool AlbumListViewModel::takeAlbumInfoRequested() noexcept { return std::exchange(_album_info_requested, false); }

PlaybackSnapshot AlbumListViewModel::playbackSnapshot() const { return _playback.snapshot(); }

void AlbumListViewModel::refreshSnapshot()
{
    auto next = _library.snapshot();
    if (!next || (_snapshot && next->revision == _snapshot_revision)) {
        return;
    }

    if (const Track* selected = selectedTrack()) {
        _selected_track_id = selected->id;
    }
    _snapshot = std::move(next);
    _snapshot_revision = _snapshot->revision;
    rebuildTrackIndices();
}

void AlbumListViewModel::rebuildTrackIndices()
{
    _track_indices.clear();
    const Album* selected_album = album();
    if (!_snapshot || !selected_album || selected_album->guide_topic != GuideTopic::None) {
        _selected_index = -1;
        return;
    }

    const std::string wanted_album = lowerAscii(selected_album->title);
    const std::string wanted_artist = lowerAscii(selected_album->artist);
    for (std::size_t index = 0; index < _snapshot->tracks.size(); ++index) {
        const Track& track = _snapshot->tracks[index];
        if (selected_album->all_music ||
            (lowerAscii(track.album) == wanted_album && lowerAscii(albumArtist(track)) == wanted_artist)) {
            _track_indices.push_back(index);
        }
    }

    if (_select_first_track_pending && !_track_indices.empty()) {
        _selected_index = 0;
        _selected_track_id = _snapshot->tracks[_track_indices.front()].id;
        _select_first_track_pending = false;
        return;
    }

    const auto previous = std::find_if(_track_indices.begin(), _track_indices.end(), [this](std::size_t index) {
        return _snapshot->tracks[index].id == _selected_track_id;
    });
    _selected_index = previous == _track_indices.end()
                          ? std::clamp(_selected_index, -1, std::max(-1, static_cast<int>(_track_indices.size()) - 1))
                          : static_cast<int>(std::distance(_track_indices.begin(), previous));
    if (const Track* selected = selectedTrack()) {
        _selected_track_id = selected->id;
    }
}

void AlbumListViewModel::selectRelative(int offset)
{
    _selected_index =
        std::clamp(_selected_index + offset, -1, std::max(-1, static_cast<int>(_track_indices.size()) - 1));
    if (const Track* selected = selectedTrack()) {
        _selected_track_id = selected->id;
    } else {
        _selected_track_id = 0;
    }
}

void AlbumListViewModel::activateSelected()
{
    if (const Track* selected = selectedTrack()) {
        std::vector<Track> queue;
        queue.reserve(_track_indices.size());
        for (const std::size_t index : _track_indices) {
            if (_snapshot && index < _snapshot->tracks.size()) {
                queue.push_back(_snapshot->tracks[index]);
            }
        }
        _playback.setQueue(std::move(queue));
        _playback.toggle(*selected);
    } else if (album()) {
        _album_info_requested = true;
    }
}

}  // namespace music
