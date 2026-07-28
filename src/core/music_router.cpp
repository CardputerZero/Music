#include "core/music_router.hpp"

namespace music {

PageId MusicRouter::page() const noexcept { return _page; }

void MusicRouter::navigate(PageId page) noexcept { _page = page; }

}  // namespace music
