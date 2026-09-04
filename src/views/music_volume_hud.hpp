#pragma once

#include "ui/page_theme.hpp"

#include <core/animation/animate_value/animate_value.hpp>
#include <lvgl.h>
#include <array>
#include <cstdint>
#include <memory>

#include <lvgl/lvgl_cpp/label.hpp>
#include <lvgl/lvgl_cpp/obj.hpp>

namespace music {

class MusicVolumeHud final {
public:
    MusicVolumeHud();
    ~MusicVolumeHud();

    MusicVolumeHud(const MusicVolumeHud&) = delete;
    MusicVolumeHud& operator=(const MusicVolumeHud&) = delete;

    void start(lv_obj_t* parent);
    void shutdown();
    void showVolume(int percent);
    void update(float delta_seconds);

private:
    using Container = smooth_ui_toolkit::lvgl_cpp::Container;
    using Label = smooth_ui_toolkit::lvgl_cpp::Label;

    static constexpr std::size_t kSegmentCount = 20;
    static constexpr int kWidth = 134;
    static constexpr int kHeight = 86;
    static constexpr std::uint32_t kVisibleDurationMs = 1100;

    std::unique_ptr<Container> _root;
    std::unique_ptr<Label> _icon;
    std::array<std::unique_ptr<Container>, kSegmentCount> _segments;
    smooth_ui_toolkit::AnimateValue _opacity{0.0f};
    std::uint32_t _hide_at_ms = 0;
    bool _waiting_to_hide = false;

    bool create(lv_obj_t* parent);
    void applyOpacity();
    void hide();
};

}  // namespace music
