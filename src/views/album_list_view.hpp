#pragma once

#include "rendering/cover_image_loader.hpp"
#include "ui/page_theme.hpp"
#include "view_models/album_list_view_model.hpp"
#include "views/view.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include <lvgl.h>
#include <lvgl/lvgl_cpp/image.hpp>
#include <lvgl/lvgl_cpp/label.hpp>
#include <lvgl/lvgl_cpp/obj.hpp>

namespace music {

class AlbumListView final : public View {
public:
    explicit AlbumListView(AlbumListViewModel& view_model);
    ~AlbumListView() override;

    void setTheme(ui::PageTheme theme);
    void onEnter(lv_obj_t* parent) override;
    void onExit() override;
    void update(float delta_seconds) override;
    void draw() override;

private:
    class TrackMenu;

    AlbumListViewModel& _view_model;
    ui::PageTheme _theme = ui::defaultPageTheme();
    std::unique_ptr<TrackMenu> _menu;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _root;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _header;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Image> _cover;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _title;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _artist;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _metadata;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Image> _cursor;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _playback_bubble;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _playback_indicator;
    std::array<std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container>, 6> _spectrum_bars;
    rendering::CoverImage _cover_image;
    lv_image_dsc_t _cover_descriptor{};
    std::string _cursor_hover_path;
    std::string _cursor_pressed_path;
    std::string _shown_album_id;
    std::uint64_t _shown_revision = 0;
    bool _cursor_pressed = false;

    void createUi(lv_obj_t* parent);
    void rebuildContent();
    void updateHeaderPosition();
    void updateCursor();
    void updatePlayback();
};

}  // namespace music
