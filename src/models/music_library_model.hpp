#pragma once

#include "core/music_types.hpp"

#include <filesystem>
#include <memory>
#include <vector>

namespace music {

struct LibraryConfig {
    std::vector<std::filesystem::path> roots;
    std::filesystem::path database_path;
    std::filesystem::path cache_dir;
};

class MusicLibraryModel {
public:
    explicit MusicLibraryModel(LibraryConfig config);
    ~MusicLibraryModel();

    MusicLibraryModel(const MusicLibraryModel&) = delete;
    MusicLibraryModel& operator=(const MusicLibraryModel&) = delete;

    void start();
    void stop();
    std::shared_ptr<const LibrarySnapshot> snapshot() const;
    ScanState scanState() const;
    void requestRescan();

private:
    class Impl;
    std::unique_ptr<Impl> _impl;
};

}  // namespace music
