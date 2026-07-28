#include "view_models/info_page_view_model.hpp"

#include "input/music_keys.hpp"

#include <algorithm>

namespace music {
namespace {

constexpr float kTriggerTimeoutSeconds = 1.1f;

}  // namespace

void InfoPageViewModel::onEnter()
{
    leaveMagic();
    resetTrigger();
}

void InfoPageViewModel::onExit()
{
    leaveMagic();
    resetTrigger();
}

void InfoPageViewModel::onKey(std::uint32_t key, bool pressed)
{
    if (!pressed) {
        return;
    }

    if (_magic_active) {
        if (key == music_key::Left) {
            _magic.moveLeft();
        } else if (key == music_key::Right) {
            _magic.moveRight();
        } else if (key == music_key::Escape) {
            leaveMagic();
        }
        return;
    }

    if (key != music_key::Space) {
        resetTrigger();
        return;
    }

    if (_space_elapsed > kTriggerTimeoutSeconds) {
        _space_count = 0;
    }
    _space_elapsed = 0.0f;
    ++_space_count;
    if (_space_count >= 3) {
        startMagic();
    }
}

void InfoPageViewModel::update(float delta_seconds)
{
    if (_magic_active) {
        _magic.update(delta_seconds);
        if (_magic.snapshot().phase == MusicMagicPhase::Finished) {
            leaveMagic();
        }
        return;
    }
    if (_space_count > 0) {
        _space_elapsed += std::clamp(delta_seconds, 0.0f, 0.1f);
        if (_space_elapsed > kTriggerTimeoutSeconds) {
            resetTrigger();
        }
    }
}

bool InfoPageViewModel::magicActive() const noexcept { return _magic_active; }

MusicMagicSnapshot InfoPageViewModel::magicSnapshot() const { return _magic.snapshot(); }

void InfoPageViewModel::startMagic()
{
    _magic_active = true;
    resetTrigger();
    ++_round_serial;
    _magic.start(UINT32_C(0x6d757369) ^ (_round_serial * UINT32_C(0x9e3779b9)));
}

void InfoPageViewModel::leaveMagic()
{
    if (_magic_active) {
        _magic.stop();
    }
    _magic_active = false;
}

void InfoPageViewModel::resetTrigger()
{
    _space_count = 0;
    _space_elapsed = 0.0f;
}

}  // namespace music
