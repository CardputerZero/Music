#include "views/playback_view.hpp"

#include "assets/font_assets.hpp"
#include "assets/runtime_assets.hpp"
#include "input/music_keys.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>
#include <vector>

namespace music {
namespace {

using smooth_ui_toolkit::lvgl_cpp::Container;
using smooth_ui_toolkit::lvgl_cpp::Image;
using smooth_ui_toolkit::lvgl_cpp::Label;

constexpr int kScreenWidth = 320;
constexpr int kScreenHeight = 170;
constexpr int kCoverSize = 110;
constexpr int kCoverY = 14;
constexpr int kCoverWithLyricsX = 17;
constexpr int kCoverCenteredX = (kScreenWidth - kCoverSize) / 2;
constexpr int kLyricsX = 140;
constexpr int kLyricsY = kCoverY;
constexpr int kLyricsWidth = 170;
constexpr int kLyricsHeight = kCoverSize;
constexpr int kLyricsTextWidth = 158;
constexpr int kLyricsLineGap = 9;
constexpr int kLyricsEdgePadding = 48;
constexpr int kMetadataX = 17;
constexpr int kMetadataWidth = 286;
constexpr int kFullscreenContentY = 7;
constexpr int kFullscreenLyricsHeight = 154;
constexpr int kFullscreenTitleY = 121;
constexpr int kFullscreenArtistY = 142;
constexpr int kProgressY = 165;
constexpr int kProgressHeight = 2;

template <typename Object>
void prepareObject(Object& object)
{
    object.setBorderWidth(0);
    object.setShadowWidth(0);
    object.setPaddingAll(0);
    object.removeFlag(LV_OBJ_FLAG_SCROLLABLE);
}

std::string imagePath(const char* name) { return assetPath(std::filesystem::path("images") / name).string(); }

lv_color_t lvColor(ui::Color color) { return lv_color_make(color.red, color.green, color.blue); }

}  // namespace

class PlaybackView::ControlBar {
public:
    explicit ControlBar(lv_obj_t* parent)
        : _slots{Slot{music_key::Key4, -112}, Slot{music_key::Key5, -56}, Slot{music_key::Key6, 0},
                 Slot{music_key::Key7, 56}, Slot{music_key::Key8, 112}}
    {
        for (auto& slot : _slots) {
            slot.icon = std::make_unique<Image>(parent);
            slot.icon->align(LV_ALIGN_CENTER, slot.x, 64);

            slot.indicator = std::make_unique<Container>(parent);
            prepareObject(*slot.indicator);
            slot.indicator->setSize(2, 6);
            slot.indicator->align(LV_ALIGN_CENTER, slot.x, 82);
            slot.indicator->setRadius(1);
            slot.indicator->setBgOpa(LV_OPA_COVER);
        }
    }

    void setImage(std::uint32_t key, std::string path)
    {
        for (auto& slot : _slots) {
            if (slot.key != key || slot.path == path) {
                continue;
            }
            slot.path = std::move(path);
            slot.icon->setSrc(slot.path.c_str());
            return;
        }
    }

    void setIndicatorColor(lv_color_t color)
    {
        for (auto& slot : _slots) {
            slot.indicator->setBgColor(color);
        }
    }

    void setHidden(bool hidden)
    {
        for (auto& slot : _slots) {
            slot.icon->setHidden(hidden);
            slot.indicator->setHidden(hidden);
        }
    }

private:
    struct Slot {
        Slot(std::uint32_t key_value, int x_value) : key(key_value), x(x_value) {}

        std::uint32_t key = 0;
        int x = 0;
        std::string path;
        std::unique_ptr<Image> icon;
        std::unique_ptr<Container> indicator;
    };

    std::array<Slot, 5> _slots;
};

class PlaybackView::LyricsPanel {
public:
    explicit LyricsPanel(lv_obj_t* parent)
    {
        _viewport = std::make_unique<Container>(parent);
        prepareObject(*_viewport);
        _viewport->setPos(kLyricsX, kLyricsY);
        _viewport->setSize(kLyricsWidth, kLyricsHeight);
        _viewport->setBgOpa(LV_OPA_TRANSP);
        _viewport->setScrollDir(LV_DIR_VER);
        _viewport->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
        _viewport->addFlag(LV_OBJ_FLAG_SCROLLABLE);

        _content = std::make_unique<Container>(_viewport->raw_ptr());
        prepareObject(*_content);
        _content->setPos(0, 0);
        _content->setSize(kLyricsTextWidth, kLyricsHeight);
        _content->setBgOpa(LV_OPA_TRANSP);
    }

    void setTheme(ui::PageTheme theme)
    {
        _theme = theme;
        updateLineColors();
    }

    void setHidden(bool hidden) { _viewport->setHidden(hidden); }

    void setExpanded(bool expanded)
    {
        const int next_y = expanded ? kFullscreenContentY : kLyricsY;
        const int next_height = expanded ? kFullscreenLyricsHeight : kLyricsHeight;
        if (_y == next_y && _height == next_height) {
            return;
        }

        _y = next_y;
        _height = next_height;
        _viewport->setPos(kLyricsX, _y);
        _viewport->setSize(kLyricsWidth, _height);

        _content_height = std::max(_height, _document_height);
        _content->setHeight(_content_height);
        scrollToCurrent(LV_ANIM_OFF);
    }

    void setDocument(const LyricsDocument& document, std::size_t current)
    {
        _labels.clear();
        _line_centers.clear();
        _current = std::numeric_limits<std::size_t>::max();

        int y = kLyricsEdgePadding;
        _labels.reserve(document.lines.size());
        _line_centers.reserve(document.lines.size());
        for (const LyricLine& line : document.lines) {
            auto label = std::make_unique<Label>(_content->raw_ptr());
            label->setPos(0, y);
            label->setWidth(kLyricsTextWidth);
            label->setHeight(LV_SIZE_CONTENT);
            label->setLongMode(LV_LABEL_LONG_WRAP);
            label->setTextAlign(LV_TEXT_ALIGN_LEFT);
            label->setTextFont(font(FontFamily::Sans, FontSize::Px14));
            label->setText(line.text);
            label->setTextColor(lvColor(_theme.secondary_text));
            lv_obj_set_style_text_opa(label->raw_ptr(), LV_OPA_60, LV_PART_MAIN);
            lv_obj_set_style_text_line_space(label->raw_ptr(), 2, LV_PART_MAIN);
            lv_obj_update_layout(label->raw_ptr());
            const int line_height = lv_obj_get_height(label->raw_ptr());
            _line_centers.push_back(y + line_height / 2);
            y += line_height + kLyricsLineGap;
            _labels.push_back(std::move(label));
        }

        _document_height = y - kLyricsLineGap + kLyricsEdgePadding;
        _content_height = std::max(_height, _document_height);
        _content->setHeight(_content_height);
        _viewport->scrollToY(0, LV_ANIM_OFF);
        setCurrent(current, LV_ANIM_OFF);
    }

    void setCurrent(std::size_t current, lv_anim_enable_t animation)
    {
        if (_labels.empty()) {
            return;
        }
        current = std::min(current, _labels.size() - 1);
        if (_current == current) {
            return;
        }
        const std::size_t previous = _current;
        _current = current;
        updateLineStyle(previous);
        updateLineStyle(current);
        scrollToCurrent(animation);
    }

    void scrollBy(int offset) { _viewport->scrollByBounded(0, -offset, LV_ANIM_ON); }

private:
    ui::PageTheme _theme = ui::defaultPageTheme();
    std::unique_ptr<Container> _viewport;
    std::unique_ptr<Container> _content;
    std::vector<std::unique_ptr<Label>> _labels;
    std::vector<int> _line_centers;
    std::size_t _current = std::numeric_limits<std::size_t>::max();
    int _y = kLyricsY;
    int _height = kLyricsHeight;
    int _document_height = kLyricsHeight;
    int _content_height = kLyricsHeight;

    void updateLineColors()
    {
        for (std::size_t index = 0; index < _labels.size(); ++index) {
            updateLineStyle(index);
        }
    }

    void updateLineStyle(std::size_t index)
    {
        if (index >= _labels.size()) {
            return;
        }
        const bool current = index == _current;
        _labels[index]->setTextColor(lvColor(current ? _theme.primary_text : _theme.secondary_text));
        lv_obj_set_style_text_opa(_labels[index]->raw_ptr(), current ? LV_OPA_COVER : LV_OPA_60, LV_PART_MAIN);
    }

    void scrollToCurrent(lv_anim_enable_t animation)
    {
        if (_current >= _line_centers.size()) {
            return;
        }
        const int max_scroll = std::max(0, _content_height - _height);
        const int target = std::clamp(_line_centers[_current] - _height / 2, 0, max_scroll);
        _viewport->scrollToY(target, animation);
    }
};

PlaybackView::PlaybackView(PlaybackViewModel& view_model) : _view_model(view_model) {}

PlaybackView::~PlaybackView() { onExit(); }

void PlaybackView::setTheme(ui::PageTheme theme)
{
    _theme = theme;
    applyTheme();
}

void PlaybackView::scrollLyricsBy(int offset)
{
    if (_lyrics_panel && _shown_has_lyrics) {
        _lyrics_panel->scrollBy(offset);
    }
}

void PlaybackView::onEnter(lv_obj_t* parent)
{
    createUi(parent);
    refreshContent();
}

void PlaybackView::onExit()
{
    _progress_fill.reset();
    _progress_track.reset();
    _track_artist.reset();
    _track_title.reset();
    _control_bar.reset();
    _lyrics_panel.reset();
    _cover.reset();
    _root.reset();
    _cover_image = {};
    _cover_descriptor = {};
    _shown_cover_path.clear();
    _shown_track_id = 0;
    _shown_state = PlaybackState::Stopped;
    _shown_mode = PlaybackMode::Sequential;
    _shown_has_lyrics = false;
    _shown_fullscreen = false;
    _shown_lyrics_revision = 0;
    _shown_lyric_index = std::numeric_limits<std::size_t>::max();
    _shown_progress_width = -1;
    _shown_progress_visible = false;
}

void PlaybackView::update(float delta_seconds)
{
    (void)delta_seconds;
    refreshContent();
}

void PlaybackView::draw() {}

void PlaybackView::createUi(lv_obj_t* parent)
{
    if (_root || !parent) {
        return;
    }

    _root = std::make_unique<Container>(parent);
    prepareObject(*_root);
    _root->setPos(0, 0);
    _root->setSize(kScreenWidth, kScreenHeight);
    _root->setRadius(0);
    _root->setBgOpa(LV_OPA_COVER);

    _cover = std::make_unique<Image>(_root->raw_ptr());
    _cover->setPos(kCoverCenteredX, kCoverY);
    _cover->setSize(kCoverSize, kCoverSize);

    _lyrics_panel = std::make_unique<LyricsPanel>(_root->raw_ptr());
    _lyrics_panel->setHidden(true);

    _track_title = std::make_unique<Label>(_root->raw_ptr());
    _track_title->setPos(kMetadataX, kFullscreenTitleY);
    _track_title->setSize(kMetadataWidth, lv_font_get_line_height(font(FontFamily::Sans, FontSize::Px14)));
    _track_title->setLongMode(LV_LABEL_LONG_MODE_DOTS);
    _track_title->setTextFont(font(FontFamily::Sans, FontSize::Px14));

    _track_artist = std::make_unique<Label>(_root->raw_ptr());
    _track_artist->setPos(kMetadataX, kFullscreenArtistY);
    _track_artist->setSize(kMetadataWidth, lv_font_get_line_height(font(FontFamily::Sans, FontSize::Px12)));
    _track_artist->setLongMode(LV_LABEL_LONG_MODE_DOTS);
    _track_artist->setTextFont(font(FontFamily::Sans, FontSize::Px12));

    _progress_track = std::make_unique<Container>(_root->raw_ptr());
    prepareObject(*_progress_track);
    _progress_track->setPos(kMetadataX, kProgressY);
    _progress_track->setSize(kMetadataWidth, kProgressHeight);
    _progress_track->setRadius(kProgressHeight);
    _progress_track->setBgOpa(LV_OPA_30);

    _progress_fill = std::make_unique<Container>(_root->raw_ptr());
    prepareObject(*_progress_fill);
    _progress_fill->setPos(kMetadataX, kProgressY);
    _progress_fill->setSize(1, kProgressHeight);
    _progress_fill->setRadius(kProgressHeight);
    _progress_fill->setBgOpa(LV_OPA_COVER);

    _control_bar = std::make_unique<ControlBar>(_root->raw_ptr());
    _control_bar->setImage(music_key::Key4, imagePath("playback_fullscreen.png"));
    _control_bar->setImage(music_key::Key5, imagePath("playback_previous.png"));
    _control_bar->setImage(music_key::Key7, imagePath("playback_next.png"));
    applyTheme();
    updateControls();
    updateLayout();
}

void PlaybackView::applyTheme()
{
    if (_root) {
        _root->setBgColor(lvColor(_theme.background));
    }
    if (_lyrics_panel) {
        _lyrics_panel->setTheme(_theme);
    }
    if (_control_bar) {
        _control_bar->setIndicatorColor(lvColor(_theme.accent));
    }
    if (_track_title) {
        _track_title->setTextColor(lvColor(_theme.primary_text));
        _track_artist->setTextColor(lvColor(_theme.secondary_text));
        _progress_track->setBgColor(lvColor(_theme.secondary_text));
        _progress_fill->setBgColor(lvColor(_theme.accent));
    }
}

void PlaybackView::refreshContent()
{
    if (!_root) {
        return;
    }
    const PlaybackSnapshot playback = _view_model.playbackSnapshot();
    const Track* track = _view_model.track();
    if (playback.track_id != _shown_track_id || (track && track->cover_path != _shown_cover_path)) {
        _shown_track_id = playback.track_id;
        updateCover(track);
        updateMetadata(playback);
    }
    updateLyrics();
    if (playback.state != _shown_state || playback.mode != _shown_mode) {
        _shown_state = playback.state;
        _shown_mode = playback.mode;
        updateControls();
    }
    if (_view_model.fullscreen() != _shown_fullscreen) {
        _shown_fullscreen = _view_model.fullscreen();
        updateLayout();
    }
    updateProgress(playback);
}

void PlaybackView::updateCover(const Track* track)
{
    _shown_cover_path = track ? track->cover_path : std::filesystem::path{};
    _cover_image = track ? rendering::loadCoverImage(track->cover_path, kCoverSize).value_or(rendering::CoverImage{})
                         : rendering::CoverImage{};
    if (!_cover_image.valid()) {
        _cover->setHidden(true);
        return;
    }

    _cover_descriptor = {};
    _cover_descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
    _cover_descriptor.header.cf = LV_COLOR_FORMAT_RGB565;
    _cover_descriptor.header.w = static_cast<std::uint32_t>(_cover_image.width);
    _cover_descriptor.header.h = static_cast<std::uint32_t>(_cover_image.height);
    _cover_descriptor.header.stride = static_cast<std::uint32_t>(_cover_image.width * 2);
    _cover_descriptor.data_size = static_cast<std::uint32_t>(_cover_image.pixels.size() * sizeof(std::uint16_t));
    _cover_descriptor.data = reinterpret_cast<const std::uint8_t*>(_cover_image.pixels.data());
    _cover->setSrc(&_cover_descriptor);
    _cover->setHidden(false);
}

void PlaybackView::updateLyrics()
{
    const bool has_lyrics = _view_model.hasLyrics();
    if (has_lyrics != _shown_has_lyrics) {
        _shown_has_lyrics = has_lyrics;
        _lyrics_panel->setHidden(!has_lyrics);
        updateLayout();
    }
    if (!has_lyrics) {
        return;
    }

    const std::uint64_t revision = _view_model.lyricsRevision();
    const std::size_t current = _view_model.currentLyricIndex();
    if (_shown_lyrics_revision != revision) {
        _shown_lyrics_revision = revision;
        _shown_lyric_index = current;
        _lyrics_panel->setDocument(_view_model.lyrics(), current);
    } else if (_shown_lyric_index != current) {
        _shown_lyric_index = current;
        _lyrics_panel->setCurrent(current, LV_ANIM_ON);
    }
}

void PlaybackView::updateControls()
{
    _control_bar->setImage(music_key::Key6, imagePath(_shown_state == PlaybackState::Playing ? "playback_pause.png"
                                                                                             : "playback_play.png"));
    const char* mode_image = "playback_mode_sequential.png";
    if (_shown_mode == PlaybackMode::Shuffle) {
        mode_image = "playback_mode_shuffle.png";
    } else if (_shown_mode == PlaybackMode::RepeatOne) {
        mode_image = "playback_mode_repeat_one.png";
    }
    _control_bar->setImage(music_key::Key8, imagePath(mode_image));
}

void PlaybackView::updateMetadata(const PlaybackSnapshot& playback)
{
    _track_title->setText(playback.title.empty() ? "Unknown Title" : playback.title);
    _track_artist->setText(playback.artist.empty() ? "Unknown Artist" : playback.artist);
}

void PlaybackView::updateProgress(const PlaybackSnapshot& playback)
{
    const std::int64_t duration = std::max<std::int64_t>(0, playback.duration_ms);
    const std::int64_t position = std::clamp<std::int64_t>(playback.position_ms, 0, duration);
    const int progress_width = _shown_has_lyrics ? kCoverSize : kMetadataWidth;
    const int width =
        duration > 0 ? static_cast<int>(position * static_cast<std::int64_t>(progress_width) / duration) : 0;
    if (width != _shown_progress_width) {
        _shown_progress_width = width;
        _progress_fill->setWidth(std::max(1, width));
    }
    const bool visible = _shown_fullscreen && width > 0;
    if (visible != _shown_progress_visible) {
        _shown_progress_visible = visible;
        _progress_fill->setHidden(!visible);
    }
}

void PlaybackView::updateLayout()
{
    _control_bar->setHidden(_shown_fullscreen);
    _track_title->setHidden(!_shown_fullscreen);
    _track_artist->setHidden(!_shown_fullscreen);
    _progress_track->setHidden(!_shown_fullscreen);
    _shown_progress_visible = _shown_fullscreen && _shown_progress_width > 0;
    _progress_fill->setHidden(!_shown_progress_visible);

    _cover->setX(_shown_has_lyrics ? kCoverWithLyricsX : kCoverCenteredX);
    _cover->setY(_shown_fullscreen ? kFullscreenContentY : kCoverY);
    _lyrics_panel->setExpanded(_shown_fullscreen && _shown_has_lyrics);

    const int metadata_width = _shown_has_lyrics ? kCoverSize : kMetadataWidth;
    _progress_track->setWidth(metadata_width);
    _progress_fill->setWidth(1);
    _shown_progress_width = -1;
    _track_title->setPos(kMetadataX, kFullscreenTitleY);
    _track_title->setSize(metadata_width, lv_font_get_line_height(font(FontFamily::Sans, FontSize::Px14)));
    _track_title->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _track_artist->setPos(kMetadataX, kFullscreenArtistY);
    _track_artist->setSize(metadata_width, lv_font_get_line_height(font(FontFamily::Sans, FontSize::Px12)));
    _track_artist->setTextAlign(LV_TEXT_ALIGN_CENTER);
}

}  // namespace music
