#pragma once

#include "rendering/cover_image_loader.hpp"
#include "ui/page_theme.hpp"

#include <filesystem>
#include <string>
#include <unordered_map>

namespace music::rendering {

ui::PageTheme extractArtworkTheme(const CoverImage& artwork);

class ArtworkPaletteCache {
public:
    ui::PageTheme themeFor(const std::filesystem::path& artwork_path);
    void clear();

private:
    struct Entry {
        std::uintmax_t size = 0;
        std::filesystem::file_time_type modified_at{};
        ui::PageTheme theme = ui::defaultPageTheme();
    };

    std::unordered_map<std::string, Entry> _entries;
};

}  // namespace music::rendering
