#pragma once

#include "core/music_types.hpp"

namespace music {

class MusicRouter {
public:
    PageId page() const noexcept;
    void navigate(PageId page) noexcept;

private:
    PageId _page = PageId::CoverFlow;
};

}  // namespace music
