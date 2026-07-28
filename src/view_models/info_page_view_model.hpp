#pragma once

#include "models/music_magic_model.hpp"
#include "view_models/view_model.hpp"

#include <cstdint>

namespace music {

class InfoPageViewModel final : public ViewModel {
public:
    void onEnter() override;
    void onExit() override;
    void onKey(std::uint32_t key, bool pressed) override;
    void update(float delta_seconds) override;

    bool magicActive() const noexcept;
    MusicMagicSnapshot magicSnapshot() const;

private:
    MusicMagicModel _magic;
    std::uint32_t _round_serial = 0;
    int _space_count = 0;
    float _space_elapsed = 0.0f;
    bool _magic_active = false;

    void startMagic();
    void leaveMagic();
    void resetTrigger();
};

}  // namespace music
