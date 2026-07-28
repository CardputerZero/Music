#pragma once

#include "ui/page_theme.hpp"
#include "views/view.hpp"

#include <memory>

namespace music {

class InfoPageViewModel;

class MusicMagicView final : public View {
public:
    explicit MusicMagicView(InfoPageViewModel& view_model);
    ~MusicMagicView() override;

    MusicMagicView(const MusicMagicView&) = delete;
    MusicMagicView& operator=(const MusicMagicView&) = delete;

    void setTheme(ui::PageTheme theme);
    void onEnter(lv_obj_t* parent) override;
    void onExit() override;
    void update(float delta_seconds) override;
    void draw() override;

private:
    struct Impl;

    InfoPageViewModel& _view_model;
    ui::PageTheme _theme = ui::defaultPageTheme();
    std::unique_ptr<Impl> _impl;
};

}  // namespace music
