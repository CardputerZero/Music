#pragma once

#include <cstdint>

namespace music {

class ViewModel {
public:
    virtual ~ViewModel() = default;
    virtual void onEnter() {}
    virtual void onExit() {}
    virtual void onKey(std::uint32_t key, bool pressed) = 0;
    virtual void update(float delta_seconds) = 0;
};

}  // namespace music
