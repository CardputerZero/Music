#include "views/album_list_view.hpp"

#include "assets/font_assets.hpp"
#include "assets/runtime_assets.hpp"

#include <widget/select_menu/smooth_selector.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>
#include <vector>

namespace music {
namespace {

using smooth_ui_toolkit::lvgl_cpp::Container;
using smooth_ui_toolkit::lvgl_cpp::Image;
using smooth_ui_toolkit::lvgl_cpp::Label;
using smooth_ui_toolkit::lvgl_cpp::Object;

constexpr int kScreenWidth = 320;
constexpr int kScreenHeight = 170;
constexpr int kHeaderX = 17;
constexpr int kHeaderY = 15;
constexpr int kCoverSize = 58;
constexpr int kTrackStartY = 88;
constexpr int kRowSelectorHeight = 20;
constexpr int kRowPitch = 27;
constexpr int kFooterGap = 14;
constexpr int kCameraPadding = 10;
constexpr int kCameraBottomSafeArea = 56;
constexpr int kScrollX = 312;
constexpr int kScrollY = 21;
constexpr int kScrollWidth = 3;
constexpr int kScrollHeight = 128;
constexpr int kScrollThumbMinHeight = 17;
constexpr int kCursorSize = 25;
constexpr int kPlayingDotSize = 4;

lv_color_t lvColor(ui::Color color) { return lv_color_make(color.red, color.green, color.blue); }

ui::Color mix(ui::Color first, ui::Color second, float amount)
{
    const auto channel = [amount](std::uint8_t a, std::uint8_t b) {
        return static_cast<std::uint8_t>(std::lround(a + (b - a) * amount));
    };
    return {channel(first.red, second.red), channel(first.green, second.green), channel(first.blue, second.blue)};
}

void prepareObject(Object& object)
{
    object.setBorderWidth(0);
    object.setShadowWidth(0);
    object.setPaddingAll(0);
    object.removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    object.removeFlag(LV_OBJ_FLAG_CLICKABLE);
}

std::string formatDuration(std::int64_t duration_ms)
{
    const auto total_seconds = std::max<std::int64_t>(0, duration_ms / 1000);
    const auto minutes = total_seconds / 60;
    const auto seconds = total_seconds % 60;
    std::ostringstream text;
    text << minutes << ':' << std::setw(2) << std::setfill('0') << seconds;
    return text.str();
}

std::string albumMetadata(const Album& album)
{
    std::ostringstream text;
    text << album.track_count << (album.track_count == 1 ? " song" : " songs");
    if (album.duration_ms > 0) {
        text << "  •  " << formatDuration(album.duration_ms);
    }
    return text.str();
}

int textWidth(const std::string& text, const lv_font_t* text_font)
{
    lv_point_t size{};
    lv_text_get_size(&size, text.c_str(), text_font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x;
}

int trackRowContentHeight() { return lv_font_get_line_height(font(FontFamily::Sans, FontSize::Px14)); }

}  // namespace

class AlbumListView::TrackMenu final : public smooth_ui_toolkit::SmoothSelectorMenu {
private:
    struct Row {
        Row(Container& parent, const Track& track, std::size_t index, ui::PageTheme theme)
            : container(std::make_unique<Container>(parent.raw_ptr())),
              playing_dot(std::make_unique<Container>(container->raw_ptr())),
              number(std::make_unique<Label>(container->raw_ptr())),
              title(std::make_unique<Label>(container->raw_ptr())),
              duration(std::make_unique<Label>(container->raw_ptr())),
              track_id(track.id)
        {
            prepareObject(*container);
            container->setSize(294, trackRowContentHeight());
            container->setBgOpa(LV_OPA_TRANSP);

            prepareObject(*playing_dot);
            playing_dot->setSize(kPlayingDotSize, kPlayingDotSize);
            playing_dot->setPos(7, (trackRowContentHeight() - kPlayingDotSize) / 2);
            playing_dot->setRadius(LV_RADIUS_CIRCLE);
            playing_dot->setBgColor(lvColor(theme.accent));

            std::ostringstream number_text;
            number_text << std::setw(2) << std::setfill('0') << index + 1;
            number->setText(number_text.str());
            number->setPos(17, 2);
            number->setSize(22, LV_SIZE_CONTENT);
            number->setTextFont(font(FontFamily::Sans, FontSize::Px12));
            number->setTextColor(lvColor(theme.secondary_text));

            const std::string title_text = track.title.empty() ? track.path.stem().string() : track.title;
            title->setText(title_text);
            title->setLongMode(LV_LABEL_LONG_DOT);
            title->setPos(42, 0);
            title->setSize(184, trackRowContentHeight());
            title->setTextFont(font(FontFamily::Sans, FontSize::Px14));
            title_overflows = textWidth(title_text, font(FontFamily::Sans, FontSize::Px14)) > 184;

            duration->setText(formatDuration(track.duration_ms));
            duration->setPos(244, 2);
            duration->setSize(43, LV_SIZE_CONTENT);
            duration->setTextAlign(LV_TEXT_ALIGN_RIGHT);
            duration->setTextFont(font(FontFamily::Sans, FontSize::Px12));
            duration->setTextColor(lvColor(theme.secondary_text));
        }

        void setSelected(bool selected)
        {
            const bool should_scroll = selected && title_overflows;
            if (scrolling == should_scroll) {
                return;
            }
            scrolling = should_scroll;
            title->setLongMode(scrolling ? LV_LABEL_LONG_MODE_SCROLL_CIRCULAR : LV_LABEL_LONG_MODE_DOTS);
        }

        std::unique_ptr<Container> container;
        std::unique_ptr<Container> playing_dot;
        std::unique_ptr<Label> number;
        std::unique_ptr<Label> title;
        std::unique_ptr<Label> duration;
        std::int64_t track_id = 0;
        bool title_overflows = false;
        bool scrolling = false;
    };

public:
    TrackMenu(Container& parent, ui::PageTheme theme)
        : _parent(parent),
          _theme(theme),
          _empty(std::make_unique<Label>(parent.raw_ptr())),
          _scroll_track(std::make_unique<Container>(parent.raw_ptr())),
          _scroll_thumb(std::make_unique<Container>(parent.raw_ptr()))
    {
        _empty->setText("No tracks");
        _empty->setTextFont(font(FontFamily::Sans, FontSize::Px14));
        _empty->setTextColor(lvColor(theme.secondary_text));
        _empty->setSize(280, LV_SIZE_CONTENT);
        _empty->setPos(20, kTrackStartY + 8);
        _empty->setTextAlign(LV_TEXT_ALIGN_CENTER);

        prepareObject(*_scroll_track);
        _scroll_track->setSize(kScrollWidth, kScrollHeight);
        _scroll_track->setPos(kScrollX, kScrollY);
        _scroll_track->setRadius(2);
        _scroll_track->setBgColor(lvColor(mix(theme.background, theme.primary_text, 0.16f)));

        prepareObject(*_scroll_thumb);
        _scroll_thumb->setSize(kScrollWidth, kScrollThumbMinHeight);
        _scroll_thumb->setPos(kScrollX, kScrollY);
        _scroll_thumb->setRadius(2);
        _scroll_thumb->setBgColor(lvColor(mix(theme.background, theme.primary_text, 0.52f)));

        getSelectorPostion().x.springOptions().visualDuration = 0.32f;
        getSelectorPostion().x.springOptions().bounce = 0.30f;
        getSelectorPostion().y.springOptions() = getSelectorPostion().x.springOptions();
        getSelectorShape().x.springOptions().visualDuration = 0.30f;
        getSelectorShape().x.springOptions().bounce = 0.18f;
        getSelectorShape().y.springOptions() = getSelectorShape().x.springOptions();
        getCamera().y.springOptions().visualDuration = 0.44f;
        getCamera().y.springOptions().bounce = 0.0f;
        setCameraSize(kScreenWidth, kScreenHeight);
        setConfig().moveInLoop = false;
        setConfig().readInputInterval = 0;
        setConfig().renderInterval = 0;
    }

    void setTracks(const AlbumListViewModel& view_model, std::int64_t playing_track_id)
    {
        _rows.clear();
        _data.option_list.clear();
        _data.selected_option_index = 0;
        addOption({{17.0f, 15.0f, 260.0f, static_cast<float>(kCoverSize)}, nullptr});

        const std::size_t count = view_model.trackCount();
        if (count == 0) {
            _empty->setHidden(false);
            _scroll_track->setHidden(true);
            _scroll_thumb->setHidden(true);
            jumpInstant(view_model.selectedIndex() + 1);
            render();
            return;
        }
        _empty->setHidden(true);

        _rows.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            const Track* track = view_model.trackAt(index);
            if (!track) {
                continue;
            }
            const std::string title = track->title.empty() ? track->path.stem().string() : track->title;
            const int option_width = std::clamp(42 + textWidth(title, font(FontFamily::Sans, FontSize::Px14)), 86, 244);
            addOption({{15.0f, static_cast<float>(kTrackStartY + static_cast<int>(index) * kRowPitch),
                        static_cast<float>(option_width), static_cast<float>(kRowSelectorHeight)},
                       nullptr});
            auto row = std::make_unique<Row>(_parent, *track, index, _theme);
            row->playing_dot->setHidden(track->id != playing_track_id);
            _rows.push_back(std::move(row));
        }

        jumpInstant(view_model.selectedIndex() + 1);
        render();
    }

    void setSelectedIndex(int index)
    {
        if (_data.option_list.empty()) {
            return;
        }
        const int clamped = std::clamp(index + 1, 0, static_cast<int>(_data.option_list.size()) - 1);
        if (clamped == _data.selected_option_index) {
            return;
        }
        _data.selected_option_index = clamped;
        _update_selector_keyframe();
        _update_camera_keyframe();
    }

    void setPressed(bool pressed)
    {
        if (_pressed == pressed || _data.option_list.empty()) {
            return;
        }
        _pressed = pressed;
        if (pressed) {
            auto frame = getSelectorCurrentFrame();
            frame.x += 2.0f;
            frame.width = std::max(1.0f, frame.width - 4.0f);
            press(frame);
        } else {
            release();
        }
    }

    void setPlayingTrack(std::int64_t track_id)
    {
        for (auto& row : _rows) {
            row->playing_dot->setHidden(track_id == 0 || row->track_id != track_id);
        }
    }

    void update(std::uint32_t now) override
    {
        smooth_ui_toolkit::SmoothSelectorMenu::update(now);
        render();
    }

    float cameraY() { return getCameraOffset().y; }
    bool hasSelection() const { return !_data.option_list.empty(); }

    lv_point_t cursorTarget()
    {
        if (_data.option_list.empty()) {
            return {};
        }
        const auto selector = getSelectorCurrentFrame();
        return {static_cast<lv_coord_t>(std::round(selector.x + selector.width + 6.0f - kCursorSize / 2.0f)),
                static_cast<lv_coord_t>(
                    std::round(selector.y - getCameraOffset().y + selector.height / 2.0f - kCursorSize / 2.0f + 2.0f))};
    }

private:
    Container& _parent;
    ui::PageTheme _theme;
    std::unique_ptr<Label> _empty;
    std::unique_ptr<Container> _scroll_track;
    std::unique_ptr<Container> _scroll_thumb;
    std::vector<std::unique_ptr<Row>> _rows;
    bool _pressed = false;

    int maxCameraY() const
    {
        if (_data.option_list.size() <= 1) {
            return 0;
        }
        const auto& last = _data.option_list.back().keyframe;
        return std::max(0, static_cast<int>(std::round(last.y + last.height)) + kCameraBottomSafeArea - kScreenHeight);
    }

    int cameraYFor(const smooth_ui_toolkit::Vector4& frame)
    {
        if (_data.option_list.size() > 2 && frame.y >= _data.option_list.back().keyframe.y) {
            return maxCameraY();
        }
        if (frame.y <= static_cast<float>(kTrackStartY)) {
            return 0;
        }
        int offset = static_cast<int>(std::round(getCameraOffset().y));
        const int top = static_cast<int>(std::round(frame.y));
        const int bottom = static_cast<int>(std::round(frame.y + frame.height));
        if (top - offset < kCameraPadding) {
            offset = top - kCameraPadding;
        } else if (bottom - offset > kScreenHeight - kCameraBottomSafeArea) {
            offset = bottom - kScreenHeight + kCameraBottomSafeArea;
        }
        return std::clamp(offset, 0, maxCameraY());
    }

    void jumpInstant(int index)
    {
        _data.selected_option_index = std::clamp(index, 0, static_cast<int>(_data.option_list.size()) - 1);
        const auto& frame = getSelectedKeyframe();
        getSelectorPostion().teleport(frame.x, frame.y);
        getSelectorShape().teleport(frame.width, frame.height);
        getCamera().teleport(0, cameraYFor(frame));
    }

    void _update_camera_keyframe() override
    {
        if (!_data.option_list.empty()) {
            getCamera().move(0, cameraYFor(getSelectedKeyframe()));
        }
    }

    void render()
    {
        const int camera_y = static_cast<int>(std::round(getCameraOffset().y));
        const int row_content_height = trackRowContentHeight();
        for (std::size_t index = 0; index < _rows.size(); ++index) {
            const auto& frame = _data.option_list[index + 1].keyframe;
            const int y = static_cast<int>(std::round(frame.y)) - camera_y;
            auto& row = *_rows[index];
            row.container->setPos(0, y);
            const bool visible = y > -row_content_height && y < kScreenHeight;
            row.container->setHidden(!visible);
            const bool selected = index + 1 == static_cast<std::size_t>(_data.selected_option_index);
            row.setSelected(selected);
            row.title->setTextColor(lvColor(selected ? _theme.primary_text : _theme.secondary_text));
        }

        const int max_offset = maxCameraY();
        if (max_offset <= 0) {
            _scroll_track->setHidden(true);
            _scroll_thumb->setHidden(true);
            return;
        }
        const int thumb_height = std::max(
            kScrollThumbMinHeight, static_cast<int>(static_cast<float>(kScreenHeight) /
                                                    static_cast<float>(max_offset + kScreenHeight) * kScrollHeight));
        const float progress = std::clamp(getCameraOffset().y, 0.0f, static_cast<float>(max_offset)) / max_offset;
        _scroll_thumb->setSize(kScrollWidth, thumb_height);
        _scroll_thumb->setY(kScrollY + static_cast<int>(std::round(progress * (kScrollHeight - thumb_height))));
        _scroll_track->setHidden(false);
        _scroll_thumb->setHidden(false);
    }
};

AlbumListView::AlbumListView(AlbumListViewModel& view_model) : _view_model(view_model) {}

AlbumListView::~AlbumListView() { onExit(); }

void AlbumListView::setTheme(ui::PageTheme theme) { _theme = theme; }

void AlbumListView::onEnter(lv_obj_t* parent)
{
    createUi(parent);
    rebuildContent();
}

void AlbumListView::onExit()
{
    for (auto& bar : _spectrum_bars) {
        bar.reset();
    }
    _playback_indicator.reset();
    _playback_bubble.reset();
    _cursor.reset();
    _menu.reset();
    _metadata.reset();
    _artist.reset();
    _title.reset();
    _cover.reset();
    _header.reset();
    _root.reset();
    _cover_image = {};
    _cover_descriptor = {};
    _shown_album_id.clear();
    _shown_revision = 0;
    _cursor_pressed = false;
}

void AlbumListView::update(float delta_seconds)
{
    (void)delta_seconds;
    if (!_root) {
        return;
    }
    _menu->setSelectedIndex(_view_model.selectedIndex());
    _menu->setPressed(_view_model.enterPressed());
    _menu->update(lv_tick_get());
    updateHeaderPosition();
    updateCursor();
    updatePlayback();
}

void AlbumListView::draw()
{
    const auto snapshot = _view_model.snapshot();
    const Album* album = _view_model.album();
    if (snapshot && album && (snapshot->revision != _shown_revision || album->id != _shown_album_id)) {
        rebuildContent();
    }
}

void AlbumListView::createUi(lv_obj_t* parent)
{
    if (_root || !parent) {
        return;
    }
    _root = std::make_unique<Container>(parent);
    prepareObject(*_root);
    _root->setSize(kScreenWidth, kScreenHeight);
    _root->setPos(0, 0);
    _root->setBgColor(lvColor(_theme.background));
    _root->setBgOpa(LV_OPA_COVER);
    _root->setRadius(0);

    _header = std::make_unique<Container>(_root->raw_ptr());
    prepareObject(*_header);
    _header->setSize(kScreenWidth, 80);
    _header->setBgOpa(LV_OPA_TRANSP);

    _cover = std::make_unique<Image>(_header->raw_ptr());
    _cover->setPos(kHeaderX, kHeaderY);
    _cover->setSize(kCoverSize, kCoverSize);

    _title = std::make_unique<Label>(_header->raw_ptr());
    _title->setPos(88, 20);
    _title->setSize(213, lv_font_get_line_height(font(FontFamily::Sans, FontSize::Px18)));
    _title->setLongMode(LV_LABEL_LONG_DOT);
    _title->setTextFont(font(FontFamily::Sans, FontSize::Px18));
    _title->setTextColor(lvColor(_theme.primary_text));

    _artist = std::make_unique<Label>(_header->raw_ptr());
    _artist->setPos(88, 47);
    _artist->setSize(213, lv_font_get_line_height(font(FontFamily::Sans, FontSize::Px14)));
    _artist->setLongMode(LV_LABEL_LONG_DOT);
    _artist->setTextFont(font(FontFamily::Sans, FontSize::Px14));
    _artist->setTextColor(lvColor(_theme.secondary_text));

    _menu = std::make_unique<TrackMenu>(*_root, _theme);

    _metadata = std::make_unique<Label>(_root->raw_ptr());
    _metadata->setPos(17, kScreenHeight - 25);
    _metadata->setSize(220, lv_font_get_line_height(font(FontFamily::Sans, FontSize::Px12)));
    _metadata->setTextAlign(LV_TEXT_ALIGN_LEFT);
    _metadata->setTextFont(font(FontFamily::Sans, FontSize::Px12));
    _metadata->setTextColor(lvColor(_theme.secondary_text));

    _cursor_hover_path = assetPath("images/cursor_hover.png").string();
    _cursor_pressed_path = assetPath("images/cursor_pressed.png").string();
    _cursor = std::make_unique<Image>(_root->raw_ptr());
    _cursor->setSrc(_cursor_hover_path.c_str());
    _cursor->setSize(kCursorSize, kCursorSize);

    _playback_bubble = std::make_unique<Container>(_root->raw_ptr());
    prepareObject(*_playback_bubble);
    _playback_bubble->setPos(248, 120);
    _playback_bubble->setSize(47, 40);
    _playback_bubble->setRadius(16);
    _playback_bubble->setBgColor(lvColor(mix(_theme.background, _theme.primary_text, 0.14f)));

    for (std::size_t index = 0; index < _spectrum_bars.size(); ++index) {
        _spectrum_bars[index] = std::make_unique<Container>(_playback_bubble->raw_ptr());
        prepareObject(*_spectrum_bars[index]);
        _spectrum_bars[index]->setSize(2, 4);
        _spectrum_bars[index]->setX(11 + static_cast<int>(index) * 5);
        _spectrum_bars[index]->setRadius(1);
        _spectrum_bars[index]->setBgColor(lvColor(_theme.primary_text));
    }

    _playback_indicator = std::make_unique<Container>(_root->raw_ptr());
    prepareObject(*_playback_indicator);
    _playback_indicator->setPos(271, 164);
    _playback_indicator->setSize(2, 6);
    _playback_indicator->setRadius(1);
    _playback_indicator->setBgColor(lvColor(_theme.accent));
}

void AlbumListView::rebuildContent()
{
    const auto snapshot = _view_model.snapshot();
    const Album* album = _view_model.album();
    if (!snapshot || !album || !_root) {
        return;
    }
    _shown_revision = snapshot->revision;
    _shown_album_id = album->id;
    _title->setText(album->title);
    _artist->setText(album->artist.empty() ? "Various Artists" : album->artist);
    const std::string metadata = albumMetadata(*album);
    _metadata->setText(metadata);

    _cover_image = rendering::loadCoverImage(album->cover_path, kCoverSize).value_or(rendering::CoverImage{});
    if (_cover_image.valid()) {
        _cover_descriptor = {};
        _cover_descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
        _cover_descriptor.header.cf = LV_COLOR_FORMAT_RGB565;
        _cover_descriptor.header.w = static_cast<std::uint32_t>(_cover_image.width);
        _cover_descriptor.header.h = static_cast<std::uint32_t>(_cover_image.height);
        _cover_descriptor.header.stride = static_cast<std::uint32_t>(_cover_image.width * 2);
        _cover_descriptor.data_size = static_cast<std::uint32_t>(_cover_image.pixels.size() * sizeof(std::uint16_t));
        _cover_descriptor.data = reinterpret_cast<const std::uint8_t*>(_cover_image.pixels.data());
        _cover->setSrc(&_cover_descriptor);
    }

    const PlaybackSnapshot playback = _view_model.playbackSnapshot();
    _menu->setTracks(_view_model, playback.track_id);
    _metadata->moveForeground();
    updateHeaderPosition();
    updateCursor();
    updatePlayback();
}

void AlbumListView::updateHeaderPosition()
{
    if (_header && _menu) {
        const int camera_y = static_cast<int>(std::round(_menu->cameraY()));
        _header->setY(-camera_y);
        const int footer_y = _view_model.trackCount() == 0
                                 ? kScreenHeight - 25
                                 : kTrackStartY + static_cast<int>(_view_model.trackCount() - 1) * kRowPitch +
                                       trackRowContentHeight() + kFooterGap;
        _metadata->setY(footer_y - camera_y);
    }
}

void AlbumListView::updateCursor()
{
    if (!_cursor || !_menu || !_menu->hasSelection()) {
        if (_cursor) {
            _cursor->setHidden(true);
        }
        return;
    }
    const bool pressed = _view_model.enterPressed();
    if (_cursor_pressed != pressed) {
        _cursor_pressed = pressed;
        _cursor->setSrc((_cursor_pressed ? _cursor_pressed_path : _cursor_hover_path).c_str());
    }
    const lv_point_t target = _menu->cursorTarget();
    _cursor->setPos(target.x, target.y);
    _cursor->setHidden(false);
    _cursor->moveForeground();
}

void AlbumListView::updatePlayback()
{
    const PlaybackSnapshot playback = _view_model.playbackSnapshot();
    _menu->setPlayingTrack(playback.track_id);
    const bool visible = playback.hasTrack() && playback.state != PlaybackState::Error;
    if (!visible) {
        _playback_bubble->setHidden(true);
        _playback_indicator->setHidden(true);
        return;
    }
    _playback_bubble->setHidden(false);
    _playback_indicator->setHidden(false);
    for (std::size_t index = 0; index < _spectrum_bars.size(); ++index) {
        const int height = std::clamp(static_cast<int>(std::lround(playback.spectrum[index] * 20.0f)), 3, 20);
        _spectrum_bars[index]->setSize(2, height);
        _spectrum_bars[index]->setY(20 - height / 2);
    }
    _playback_bubble->moveForeground();
    _playback_indicator->moveForeground();
}

}  // namespace music
