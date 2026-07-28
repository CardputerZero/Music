#pragma once

#include <filesystem>

namespace music {

std::filesystem::path assetPath(const std::filesystem::path& relative_path);

}  // namespace music
