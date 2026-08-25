#pragma once

#include <filesystem>

namespace music {

std::filesystem::path assetPath(const std::filesystem::path& relative_path);
std::filesystem::path defaultCoverPath();
std::filesystem::path displayCoverPath(const std::filesystem::path& preferred_path);

}  // namespace music
