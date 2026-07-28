#include "views/info_page_view.hpp"

#include "assets/font_assets.hpp"
#include "views/music_magic_view.hpp"

#include <algorithm>
#include <utility>

namespace music {
namespace {

constexpr int kScreenWidth = 320;
constexpr int kScreenHeight = 170;
constexpr int kTitleY = 4;
constexpr int kAccentY = 39;
constexpr int kBodyY = 46;
constexpr int kScrollX = 312;
constexpr int kScrollY = 21;
constexpr int kScrollWidth = 3;
constexpr int kScrollHeight = 128;
constexpr int kScrollThumbMinHeight = 17;
constexpr std::uint32_t kHoldRepeatDelayMs = 320;
constexpr std::uint32_t kHoldRepeatMs = 90;

lv_color_t lvColor(ui::Color color) { return lv_color_make(color.red, color.green, color.blue); }

}  // namespace

InfoPageView::InfoPageView(InfoPageViewModel& view_model) : _view_model(view_model) {}

InfoPageView::~InfoPageView() { destroyUi(); }

void InfoPageView::setContent(InfoPageContent content)
{
    _content = std::move(content);
    updateContent();
}

void InfoPageView::setTheme(ui::PageTheme theme)
{
    _theme = theme;
    updateTheme();
}

void InfoPageView::onScrollKey(int offset, bool pressed)
{
    if (pressed) {
        if (_held_scroll_offset == offset) {
            return;
        }
        scrollBy(offset, LV_ANIM_ON);
        _held_scroll_offset = offset;
        _next_scroll_at_ms = lv_tick_get() + kHoldRepeatDelayMs;
    } else if (_held_scroll_offset == offset) {
        _held_scroll_offset = 0;
        _next_scroll_at_ms = 0;
    }
}

void InfoPageView::scrollBy(int offset, lv_anim_enable_t animation)
{
    if (!_viewport || !_viewport->isValid()) {
        return;
    }
    _viewport->scrollByBounded(0, -offset, animation);
}

void InfoPageView::onEnter(lv_obj_t* parent)
{
    createUi(parent);
    updateContent();
}

void InfoPageView::onExit() { destroyUi(); }

void InfoPageView::update(float delta_seconds)
{
    syncMagicVisibility();
    if (_magic_visible) {
        _magic_view->update(delta_seconds);
        return;
    }
    const std::uint32_t now_ms = lv_tick_get();
    if (_held_scroll_offset != 0 && static_cast<std::int32_t>(now_ms - _next_scroll_at_ms) >= 0) {
        scrollBy(_held_scroll_offset, LV_ANIM_OFF);
        _next_scroll_at_ms = now_ms + kHoldRepeatMs;
    }
    refreshScrollbar();
}

void InfoPageView::draw()
{
    if (_magic_visible && _magic_view) {
        _magic_view->draw();
    }
}

void InfoPageView::createUi(lv_obj_t* parent)
{
    if ((_root && _root->isValid()) || !parent) {
        return;
    }

    _root = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(parent);
    _root->setSize(kScreenWidth, kScreenHeight);
    _root->setPos(0, 0);
    _root->setBgOpa(LV_OPA_COVER);
    _root->setBorderWidth(0);
    _root->setRadius(0);
    _root->setShadowWidth(0);
    _root->setPaddingAll(0);
    _root->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _viewport = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(_root->raw_ptr());
    _viewport->setSize(kScreenWidth, kScreenHeight);
    _viewport->setPos(0, 0);
    _viewport->setBgOpa(LV_OPA_TRANSP);
    _viewport->setBorderWidth(0);
    _viewport->setRadius(0);
    _viewport->setShadowWidth(0);
    _viewport->setPaddingAll(0);
    _viewport->setScrollDir(LV_DIR_VER);
    _viewport->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);

    _title = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Label>(_viewport->raw_ptr());
    _title->setSize(292, lv_font_get_line_height(font(FontFamily::Sans, FontSize::Px18)));
    _title->setPos(14, kTitleY);
    _title->setLongMode(LV_LABEL_LONG_DOT);
    _title->setTextFont(font(FontFamily::Sans, FontSize::Px18));

    _accent = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(_viewport->raw_ptr());
    _accent->setSize(42, 2);
    _accent->setPos(14, kAccentY);
    _accent->setBgOpa(LV_OPA_COVER);
    _accent->setBorderWidth(0);
    _accent->setRadius(0);
    _accent->setPaddingAll(0);
    _accent->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _body = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Label>(_viewport->raw_ptr());
    _body->setWidth(292);
    _body->setHeight(LV_SIZE_CONTENT);
    _body->setPos(14, kBodyY);
    _body->setLongMode(LV_LABEL_LONG_WRAP);
    _body->setTextFont(font(FontFamily::Sans, FontSize::Px14));
    lv_obj_set_style_text_line_space(_body->raw_ptr(), 2, LV_PART_MAIN);
    lv_obj_set_style_margin_bottom(_body->raw_ptr(), 12, LV_PART_MAIN);

    createScrollbar();
    updateTheme();
    syncMagicVisibility();
}

void InfoPageView::createScrollbar()
{
    _scroll_track = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(_root->raw_ptr());
    _scroll_track->setSize(kScrollWidth, kScrollHeight);
    _scroll_track->setPos(kScrollX, kScrollY);
    _scroll_track->setBgOpa(LV_OPA_COVER);
    _scroll_track->setBorderWidth(0);
    _scroll_track->setRadius(2);
    _scroll_track->setShadowWidth(0);
    _scroll_track->setPaddingAll(0);
    _scroll_track->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _scroll_track->removeFlag(LV_OBJ_FLAG_CLICKABLE);

    _scroll_thumb = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(_root->raw_ptr());
    _scroll_thumb->setSize(kScrollWidth, kScrollThumbMinHeight);
    _scroll_thumb->setPos(kScrollX, kScrollY);
    _scroll_thumb->setBgOpa(LV_OPA_COVER);
    _scroll_thumb->setBorderWidth(0);
    _scroll_thumb->setRadius(2);
    _scroll_thumb->setShadowWidth(0);
    _scroll_thumb->setPaddingAll(0);
    _scroll_thumb->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _scroll_thumb->removeFlag(LV_OBJ_FLAG_CLICKABLE);
}

void InfoPageView::destroyUi()
{
    _held_scroll_offset = 0;
    _next_scroll_at_ms = 0;
    _last_scroll_top = -1;
    _last_scroll_bottom = -1;
    if (_magic_view) {
        _magic_view->onExit();
    }
    _magic_view.reset();
    _magic_visible = false;
    _scroll_thumb.reset();
    _scroll_track.reset();
    _body.reset();
    _accent.reset();
    _title.reset();
    _viewport.reset();
    _root.reset();
}

void InfoPageView::syncMagicVisibility()
{
    if (!_root || !_root->isValid()) {
        return;
    }

    const bool should_show = _view_model.magicActive();
    if (should_show == _magic_visible) {
        return;
    }

    _magic_visible = should_show;
    _viewport->setHidden(_magic_visible);
    _scroll_track->setHidden(true);
    _scroll_thumb->setHidden(true);
    if (_magic_visible) {
        _held_scroll_offset = 0;
        _next_scroll_at_ms = 0;
        _magic_view = std::make_unique<MusicMagicView>(_view_model);
        _magic_view->setTheme(_theme);
        _magic_view->onEnter(_root->raw_ptr());
        return;
    }

    if (_magic_view) {
        _magic_view->onExit();
    }
    _magic_view.reset();
    _last_scroll_top = -1;
    _last_scroll_bottom = -1;
    refreshScrollbar();
}

void InfoPageView::refreshScrollbar()
{
    if (!_viewport || !_scroll_track || !_scroll_thumb) {
        return;
    }

    const std::int32_t top = lv_obj_get_scroll_top(_viewport->raw_ptr());
    const std::int32_t bottom = lv_obj_get_scroll_bottom(_viewport->raw_ptr());
    if (top == _last_scroll_top && bottom == _last_scroll_bottom) {
        return;
    }
    _last_scroll_top = top;
    _last_scroll_bottom = bottom;

    const std::int32_t range = top + bottom;
    if (range <= 0) {
        _scroll_track->setHidden(true);
        _scroll_thumb->setHidden(true);
        return;
    }

    const std::int32_t content_height = kScreenHeight + range;
    const std::int32_t thumb_height =
        std::clamp(kScreenHeight * kScrollHeight / content_height, kScrollThumbMinHeight, kScrollHeight);
    const std::int32_t travel = kScrollHeight - thumb_height;
    const std::int32_t clamped_top = std::clamp(top, 0, range);
    const std::int32_t thumb_y = kScrollY + (travel > 0 ? clamped_top * travel / range : 0);

    _scroll_thumb->setSize(kScrollWidth, thumb_height);
    _scroll_thumb->setY(thumb_y);
    _scroll_track->setHidden(false);
    _scroll_thumb->setHidden(false);
}

void InfoPageView::updateContent()
{
    if (!_root || !_root->isValid()) {
        return;
    }
    _title->setText(_content.title);
    _body->setText(_content.body);
    _viewport->scrollToY(0, LV_ANIM_OFF);
    lv_obj_update_layout(_viewport->raw_ptr());
    _last_scroll_top = -1;
    _last_scroll_bottom = -1;
    refreshScrollbar();
}

void InfoPageView::updateTheme()
{
    if (!_root || !_root->isValid()) {
        return;
    }
    _root->setBgColor(lvColor(_theme.background));
    lv_obj_set_style_bg_grad_dir(_root->raw_ptr(), LV_GRAD_DIR_NONE, LV_PART_MAIN);
    _title->setTextColor(lvColor(_theme.primary_text));
    _body->setTextColor(lvColor(_theme.secondary_text));
    _accent->setBgColor(lvColor(_theme.accent));
    _scroll_track->setBgColor(lv_color_mix(lvColor(_theme.primary_text), lvColor(_theme.background), LV_OPA_20));
    _scroll_thumb->setBgColor(lv_color_mix(lvColor(_theme.primary_text), lvColor(_theme.background), LV_OPA_50));
    if (_magic_view) {
        _magic_view->setTheme(_theme);
    }
}

}  // namespace music
