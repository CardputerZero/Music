#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace music {

enum class PageId : std::uint8_t {
    CoverFlow = 0,
    AlbumList,
    Playback,
    Info,
};

enum class GuideTopic : std::uint8_t {
    None = 0,
    AddMusic,
    CoverArt,
    Lyrics,
};

struct Track {
    std::int64_t id = 0;
    std::filesystem::path path;
    std::string title;
    std::string artist;
    std::string album;
    std::string album_artist;
    std::string genre;
    int year = 0;
    int track_number = 0;
    int disc_number = 0;
    std::int64_t duration_ms = 0;
    std::uint64_t size_bytes = 0;
    std::int64_t mtime_ns = 0;
    std::filesystem::path cover_path;
    std::filesystem::path embedded_cover_path;
    std::filesystem::path external_cover_path;
    std::uint64_t external_cover_size_bytes = 0;
    std::int64_t external_cover_mtime_ns = 0;
    std::string external_cover_hash;
    std::filesystem::path lyrics_path;
    std::filesystem::path sidecar_lyrics_path;
    std::uint64_t sidecar_lyrics_size_bytes = 0;
    std::int64_t sidecar_lyrics_mtime_ns = 0;
    std::string sidecar_lyrics_hash;
    std::string embedded_lyrics;
};

struct Album {
    std::string id;
    std::string title;
    std::string artist;
    std::string description;
    std::filesystem::path cover_path;
    std::size_t track_count = 0;
    std::int64_t duration_ms = 0;
    bool all_music = false;
    GuideTopic guide_topic = GuideTopic::None;
};

struct LibrarySnapshot {
    std::vector<Track> tracks;
    std::vector<Album> albums;
    std::uint64_t revision = 0;
};

enum class ScanPhase : std::uint8_t {
    Idle,
    OpeningDatabase,
    Discovering,
    ReadingMetadata,
    Committing,
    Complete,
    Error,
    Stopped,
};

struct ScanState {
    ScanPhase phase = ScanPhase::Idle;
    std::uint64_t files_discovered = 0;
    std::uint64_t files_processed = 0;
    std::uint64_t files_changed = 0;
    std::uint64_t files_removed = 0;
    std::string message;
};

const char* scanPhaseName(ScanPhase phase);

}  // namespace music
