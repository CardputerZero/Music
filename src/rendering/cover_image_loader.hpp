#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace music::rendering {

struct CoverImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint16_t> pixels;

    bool valid() const noexcept
    {
        return width > 0 && height > 0 && pixels.size() == static_cast<std::size_t>(width * height);
    }
};

std::optional<CoverImage> loadCoverImage(const std::filesystem::path& path, int output_size);

}  // namespace music::rendering
