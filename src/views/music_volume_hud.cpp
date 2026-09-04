#include "views/music_volume_hud.hpp"

#include <core/easing/ease.hpp>

#include <algorithm>
#include <cmath>

namespace music {
namespace {

constexpr std::uint32_t kPanelColor = 0x474747;
constexpr std::uint32_t kTextColor = 0xFFFFFF;
constexpr int kRadius = 14;
constexpr int kPanelOffsetY = -10;
constexpr int kIconY = 22;
constexpr int kSegmentStartX = 18;
constexpr int kSegmentY = 61;
constexpr int kSegmentWidth = 3;
constexpr int kSegmentHeight = 7;
constexpr int kSegmentPitch = 5;

lv_opa_t toOpacity(float value)
{
    return static_cast<lv_opa_t>(std::clamp(static_cast<int>(std::lround(value)), 0, 255));
}

bool deadlineReached(std::uint32_t now_ms, std::uint32_t deadline_ms)
{
    return static_cast<std::int32_t>(now_ms - deadline_ms) >= 0;
}

void prepareObject(smooth_ui_toolkit::lvgl_cpp::Object& object)
{
    object.setBorderWidth(0);
    object.setShadowWidth(0);
    object.setPaddingAll(0);
    object.removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    object.removeFlag(LV_OBJ_FLAG_CLICKABLE);
}

}  // namespace

MusicVolumeHud::MusicVolumeHud()
{
    _opacity.easingOptions().duration = 0.18f;
    _opacity.easingOptions().easingFunction = smooth_ui_toolkit::ease::ease_out_quad;
}

MusicVolumeHud::~MusicVolumeHud() { shutdown(); }

void MusicVolumeHud::start(lv_obj_t* parent)
{
    if (!_root || !_root->isValid()) {
        (void)create(parent);
    }
}

void MusicVolumeHud::shutdown()
{
    for (auto& segment : _segments) {
        segment.reset();
    }
    _icon.reset();
    _root.reset();
    _opacity.teleport(0.0f);
    _hide_at_ms = 0;
    _waiting_to_hide = false;
}

void MusicVolumeHud::showVolume(int percent)
{
    if (!_root || !_root->isValid()) {
        return;
    }

    const int clamped = std::clamp(percent, 0, 100);
    _icon->setText(clamped == 0 ? LV_SYMBOL_MUTE : LV_SYMBOL_VOLUME_MAX);
    const std::size_t active =
        clamped == 0 ? 0 : static_cast<std::size_t>((clamped * static_cast<int>(kSegmentCount) + 99) / 100);
    for (std::size_t index = 0; index < _segments.size(); ++index) {
        _segments[index]->setBgOpa(index < active ? LV_OPA_COVER : LV_OPA_20);
    }

    _root->setHidden(false);
    _root->moveForeground();
    _opacity.move(255.0f);
    _hide_at_ms = lv_tick_get() + kVisibleDurationMs;
    _waiting_to_hide = true;
    applyOpacity();
}

void MusicVolumeHud::update(float delta_seconds)
{
    (void)delta_seconds;
    if (!_root || !_root->isValid() || _root->hasFlag(LV_OBJ_FLAG_HIDDEN)) {
        return;
    }

    const std::uint32_t now_ms = lv_tick_get();
    if (_waiting_to_hide && deadlineReached(now_ms, _hide_at_ms)) {
        _waiting_to_hide = false;
        _opacity.move(0.0f);
    }

    _opacity.update();
    applyOpacity();
    if (!_waiting_to_hide && _opacity.done() && _opacity.directValue() <= 0.0f) {
        hide();
    }
}

bool MusicVolumeHud::create(lv_obj_t* parent)
{
    if (!parent) {
        return false;
    }

    _root = std::make_unique<Container>(parent);
    prepareObject(*_root);
    _root->setSize(kWidth, kHeight);
    _root->align(LV_ALIGN_CENTER, 0, kPanelOffsetY);
    _root->setBgColor(lv_color_hex(kPanelColor));
    _root->setBgOpa(LV_OPA_COVER);
    _root->setRadius(kRadius);
    _root->addFlag(LV_OBJ_FLAG_IGNORE_LAYOUT);

    _icon = std::make_unique<Label>(_root->raw_ptr());
    _icon->setSize(kWidth, 26);
    _icon->setPos(0, kIconY);
    _icon->setTextFont(&lv_font_montserrat_20);
    _icon->setTextColor(lv_color_hex(kTextColor));
    _icon->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _icon->setText(LV_SYMBOL_VOLUME_MAX);

    for (std::size_t index = 0; index < _segments.size(); ++index) {
        _segments[index] = std::make_unique<Container>(_root->raw_ptr());
        prepareObject(*_segments[index]);
        _segments[index]->setSize(kSegmentWidth, kSegmentHeight);
        _segments[index]->setPos(kSegmentStartX + static_cast<int>(index) * kSegmentPitch, kSegmentY);
        _segments[index]->setRadius(1);
        _segments[index]->setBgColor(lv_color_hex(kTextColor));
    }

    _root->setHidden(true);
    applyOpacity();
    return true;
}

void MusicVolumeHud::applyOpacity()
{
    if (_root && _root->isValid()) {
        lv_obj_set_style_opa_layered(_root->raw_ptr(), toOpacity(_opacity.directValue()), LV_PART_MAIN);
    }
}

void MusicVolumeHud::hide()
{
    if (_root && _root->isValid()) {
        _root->setHidden(true);
    }
}

}  // namespace music
