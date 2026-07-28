#pragma once

#include "rendering/cover_image_loader.hpp"
#include "ui/page_theme.hpp"
#include "view_models/playback_view_model.hpp"
#include "views/view.hpp"

#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>

#include <lvgl.h>
#include <lvgl/lvgl_cpp/image.hpp>
#include <lvgl/lvgl_cpp/label.hpp>
#include <lvgl/lvgl_cpp/obj.hpp>

namespace music {

class PlaybackView final : public View {
public:
    explicit PlaybackView(PlaybackViewModel& view_model);
    ~PlaybackView() override;

    void setTheme(ui::PageTheme theme);
    void scrollLyricsBy(int offset);
    void onEnter(lv_obj_t* parent) override;
    void onExit() override;
    void update(float delta_seconds) override;
    void draw() override;

private:
    class ControlBar;
    class LyricsPanel;

    PlaybackViewModel& _view_model;
    ui::PageTheme _theme = ui::defaultPageTheme();
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _root;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Image> _cover;
    std::unique_ptr<LyricsPanel> _lyrics_panel;
    std::unique_ptr<ControlBar> _control_bar;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _track_title;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _track_artist;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _progress_track;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _progress_fill;
    rendering::CoverImage _cover_image;
    lv_image_dsc_t _cover_descriptor{};
    std::filesystem::path _shown_cover_path;
    std::int64_t _shown_track_id = 0;
    PlaybackState _shown_state = PlaybackState::Stopped;
    PlaybackMode _shown_mode = PlaybackMode::Sequential;
    bool _shown_has_lyrics = false;
    bool _shown_fullscreen = false;
    std::uint64_t _shown_lyrics_revision = 0;
    std::size_t _shown_lyric_index = std::numeric_limits<std::size_t>::max();
    int _shown_progress_width = -1;
    bool _shown_progress_visible = false;

    void createUi(lv_obj_t* parent);
    void applyTheme();
    void refreshContent();
    void updateCover(const Track* track);
    void updateLyrics();
    void updateControls();
    void updateMetadata(const PlaybackSnapshot& playback);
    void updateProgress(const PlaybackSnapshot& playback);
    void updateLayout();
};

}  // namespace music
