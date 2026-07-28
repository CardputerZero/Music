#pragma once

#include "ui/page_theme.hpp"
#include "view_models/info_page_view_model.hpp"
#include "views/view.hpp"

#include <cstdint>
#include <memory>
#include <string>

#include <lvgl/lvgl_cpp/label.hpp>
#include <lvgl/lvgl_cpp/obj.hpp>

namespace music {

class MusicMagicView;

struct InfoPageContent {
    std::string title;
    std::string body;
};

class InfoPageView final : public View {
public:
    explicit InfoPageView(InfoPageViewModel& view_model);
    ~InfoPageView() override;

    void setContent(InfoPageContent content);
    void setTheme(ui::PageTheme theme);
    void onScrollKey(int offset, bool pressed);

    void onEnter(lv_obj_t* parent) override;
    void onExit() override;
    void update(float delta_seconds) override;
    void draw() override;

private:
    InfoPageViewModel& _view_model;
    InfoPageContent _content;
    ui::PageTheme _theme = ui::defaultPageTheme();
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _root;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _viewport;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _title;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _accent;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _body;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _scroll_track;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _scroll_thumb;
    std::unique_ptr<MusicMagicView> _magic_view;
    int _held_scroll_offset = 0;
    std::uint32_t _next_scroll_at_ms = 0;
    std::int32_t _last_scroll_top = -1;
    std::int32_t _last_scroll_bottom = -1;
    bool _magic_visible = false;

    void createUi(lv_obj_t* parent);
    void createScrollbar();
    void destroyUi();
    void refreshScrollbar();
    void scrollBy(int offset, lv_anim_enable_t animation);
    void syncMagicVisibility();
    void updateContent();
    void updateTheme();
};

}  // namespace music
