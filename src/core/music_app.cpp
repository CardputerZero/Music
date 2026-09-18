#include "core/music_app.hpp"

#include "assets/font_assets.hpp"
#include "assets/runtime_assets.hpp"
#include "core/music_guides.hpp"
#include "input/music_keys.hpp"

#include <lvgl.h>
#include <spdlog/spdlog.h>

#include <iomanip>
#include <sstream>
#include <utility>

#if MUSIC_USE_SDL
#include LV_SDL_INCLUDE_PATH
#endif

namespace music {
namespace {

constexpr int kVolumeShortcutDeltaPercent = 5;
constexpr std::uint32_t kEscLongPressMs = 700;

#if MUSIC_USE_SDL
bool sdlKeyHeld(SDL_Scancode scancode)
{
    int key_count = 0;
    const Uint8* state = SDL_GetKeyboardState(&key_count);
    const int index = static_cast<int>(scancode);
    return state && index >= 0 && index < key_count && state[index] != 0;
}
#endif

bool isMediaShortcut(std::uint32_t key)
{
    return key == music_key::Mute || key == music_key::PlayPause || key == music_key::Previous ||
           key == music_key::Next;
}

std::string albumInfoBody(const Album& album, const Track* representative_track)
{
    std::ostringstream body;
    body << (album.artist.empty() ? "Various Artists" : album.artist) << "\n\n";
    body << album.track_count << (album.track_count == 1 ? " song" : " songs");
    if (album.duration_ms > 0) {
        const auto total_seconds = album.duration_ms / 1000;
        body << "  •  " << total_seconds / 60 << ':' << std::setw(2) << std::setfill('0') << total_seconds % 60;
    }
    if (representative_track) {
        if (representative_track->year > 0) {
            body << "\nReleased " << representative_track->year;
        }
        if (!representative_track->genre.empty()) {
            body << "\n" << representative_track->genre;
        }
    }
    body << "\n\n" << (album.description.empty() ? "No album description is available." : album.description);
    return body.str();
}

}  // namespace

MusicApp::MusicApp(MusicConfig config)
    : _config(std::move(config)),
      _library(_config.library),
      _cover_flow_view_model(_library),
      _album_list_view_model(_library, _playback),
      _playback_view_model(_library, _playback),
      _cover_flow_view(_cover_flow_view_model),
      _album_list_view(_album_list_view_model),
      _playback_view(_playback_view_model),
      _info_page_view(_info_page_view_model),
      _help_info_page_view(_help_info_page_view_model)
{
}

MusicApp::~MusicApp() { stop(); }

void MusicApp::start()
{
    if (_started) {
        return;
    }
    _started = true;
    spdlog::info("MusicApp: starting library at {}", _config.library.database_path.string());
    for (const auto& root : _config.library.roots) {
        spdlog::info("MusicApp: music root {}", root.string());
    }
    _library.start();
    initFontAssets();
    createExitHint();
    _volume_hud.start(lv_layer_top());
    _cover_flow_view_model.onEnter();
    _cover_flow_view.onEnter(lv_screen_active());
    _help_active = false;
    _pressed_media_key = 0;
    _esc_pressed = false;
    _esc_long_consumed = false;
    _esc_exit_armed = false;
    _esc_pressed_at = 0;
}

void MusicApp::stop()
{
    if (!_started) {
        return;
    }
    if (_help_active) {
        closeHelpPage();
    }
    hideExitHint();
    if (_exit_hint && lv_obj_is_valid(_exit_hint)) {
        lv_obj_delete(_exit_hint);
    }
    _exit_hint = nullptr;
    _esc_pressed = false;
    _esc_long_consumed = false;
    _esc_exit_armed = false;
    _esc_pressed_at = 0;
    switch (_router.page()) {
        case PageId::CoverFlow:
            _cover_flow_view.onExit();
            _cover_flow_view_model.onExit();
            break;
        case PageId::AlbumList:
            _album_list_view.onExit();
            _album_list_view_model.onExit();
            break;
        case PageId::Playback:
            _playback_view.onExit();
            _playback_view_model.onExit();
            break;
        case PageId::Info:
            _info_page_view.onExit();
            _info_page_view_model.onExit();
            break;
    }
    _volume_hud.shutdown();
    _playback.stop();
    _artwork_palette_cache.clear();
    shutdownFontAssets();
    _library.stop();
    _started = false;
}

void MusicApp::onKey(std::uint32_t key, bool pressed)
{
    if (key == music_key::Escape) {
        if (pressed) {
            if (_esc_pressed) {
                return;
            }
            _esc_pressed = true;
            _esc_pressed_at = lv_tick_get();
            _esc_long_consumed = false;
            _esc_exit_armed = !_help_active && _router.page() == PageId::CoverFlow;
            if (_esc_exit_armed) {
                showExitHint();
            }
        } else if (_esc_pressed) {
#if MUSIC_USE_SDL
            // LVGL's SDL keyboard driver synthesizes a release immediately
            // after the press. Keep the physical hold armed until SDL reports
            // that Escape is actually no longer down.
            if (sdlKeyHeld(SDL_SCANCODE_ESCAPE)) {
                return;
            }
#endif
            releaseEscPress();
        }
        return;
    }

    if (key == music_key::VolumeDown || key == music_key::VolumeUp) {
        if (pressed) {
            const int delta = key == music_key::VolumeUp ? kVolumeShortcutDeltaPercent : -kVolumeShortcutDeltaPercent;
#if MUSIC_USE_SDL
            // SDL has no CardputerZero Fn layer. Keep desktop testing side-effect free:
            // simulate the volume state instead of changing the host system volume.
            _desktop_volume_percent = SystemVolumeModel::clampPercent(_desktop_volume_percent + delta);
            _desktop_volume_muted = false;
            spdlog::info("MusicApp: SDL volume shortcut simulated (volume={}%, delta={}%)", _desktop_volume_percent,
                         delta);
            _volume_hud.showVolume(_desktop_volume_percent);
#else
            const SystemVolumeResult result = _system_volume.adjustVolume(delta);
            if (!result.success) {
                spdlog::warn("MusicApp: system volume shortcut failed (delta={}%)", delta);
            } else {
                _volume_hud.showVolume(result.state.percent);
            }
#endif
        }
        return;
    }

    // Media shortcuts remain active while browsing albums, reading Info/Help,
    // or viewing the playback page. Trigger on release to keep toggles edge-based.
    if (isMediaShortcut(key)) {
        if (pressed) {
            _pressed_media_key = key;
        } else if (_pressed_media_key == key) {
            _pressed_media_key = 0;
            activateMediaShortcut(key);
        }
        return;
    }

    if (_help_active) {
        if (pressed && (key == music_key::Help || key == music_key::Escape)) {
            closeHelpPage();
        } else if (key == music_key::Up) {
            _help_info_page_view.onScrollKey(-28, pressed);
        } else if (key == music_key::Down) {
            _help_info_page_view.onScrollKey(28, pressed);
        }
        return;
    }
    if (key == music_key::Help) {
        if (pressed) {
            showHelpPage();
        }
        return;
    }

    switch (_router.page()) {
        case PageId::CoverFlow:
            if (pressed && key == music_key::Enter) {
                openSelectedAlbum();
            } else {
                _cover_flow_view_model.onKey(key, pressed);
            }
            break;
        case PageId::AlbumList:
            if (pressed && key == music_key::NowPlaying) {
                openPlaybackPage();
            } else {
                _album_list_view_model.onKey(key, pressed);
                if (!pressed && key == music_key::Enter && _album_list_view_model.takeAlbumInfoRequested()) {
                    openSelectedAlbumInfo();
                }
            }
            break;
        case PageId::Playback:
            if (pressed && key == music_key::Up) {
                _playback_view.scrollLyricsBy(-28);
            } else if (pressed && key == music_key::Down) {
                _playback_view.scrollLyricsBy(28);
            } else {
                _playback_view_model.onKey(key, pressed);
            }
            break;
        case PageId::Info:
            if (_info_page_view_model.magicActive()) {
                _info_page_view_model.onKey(key, pressed);
            } else {
                _info_page_view_model.onKey(key, pressed);
                if (!_info_page_view_model.magicActive()) {
                    if (key == music_key::Up) {
                        _info_page_view.onScrollKey(-28, pressed);
                    } else if (key == music_key::Down) {
                        _info_page_view.onScrollKey(28, pressed);
                    }
                }
            }
            break;
    }
}

void MusicApp::activateMediaShortcut(std::uint32_t key)
{
    if (key == music_key::Previous) {
        (void)_playback.previous();
    } else if (key == music_key::PlayPause) {
        (void)_playback.toggleCurrent();
    } else if (key == music_key::Next) {
        (void)_playback.next();
    } else if (key == music_key::Mute) {
#if MUSIC_USE_SDL
        _desktop_volume_muted = !_desktop_volume_muted;
        spdlog::info("MusicApp: SDL mute shortcut simulated ({})", _desktop_volume_muted ? "muted" : "sound on");
        _volume_hud.showMute(_desktop_volume_muted, _desktop_volume_percent);
#else
        const SystemVolumeResult result = _system_volume.toggleMute();
        if (!result.success) {
            spdlog::warn("MusicApp: system mute shortcut failed");
        } else {
            _volume_hud.showMute(result.state.muted, result.state.percent);
        }
#endif
    }
}

void MusicApp::update(float delta_seconds)
{
    _playback.update(delta_seconds);
    _volume_hud.update(delta_seconds);

    if (_esc_exit_armed && (_router.page() != PageId::CoverFlow || _help_active)) {
        _esc_exit_armed = false;
        hideExitHint();
    }
    if (_esc_pressed && !_esc_long_consumed && _esc_exit_armed && lv_tick_elaps(_esc_pressed_at) >= kEscLongPressMs) {
        _esc_long_consumed = true;
        hideExitHint();
        spdlog::info("MusicApp: quit requested after holding Esc for {} ms", kEscLongPressMs);
        _quit_requested = true;
    }
#if MUSIC_USE_SDL
    if (_esc_pressed && !sdlKeyHeld(SDL_SCANCODE_ESCAPE)) {
        releaseEscPress();
    }
#endif

    if (_help_active) {
        _help_info_page_view.update(delta_seconds);
        return;
    }
    switch (_router.page()) {
        case PageId::CoverFlow:
            _cover_flow_view_model.update(delta_seconds);
            _cover_flow_view.update(delta_seconds);
            break;
        case PageId::AlbumList:
            _album_list_view_model.update(delta_seconds);
            _album_list_view.update(delta_seconds);
            break;
        case PageId::Playback:
            _playback_view_model.update(delta_seconds);
            updatePlaybackTheme();
            _playback_view.update(delta_seconds);
            break;
        case PageId::Info:
            _info_page_view_model.update(delta_seconds);
            _info_page_view.update(delta_seconds);
            break;
    }
}

void MusicApp::draw()
{
    if (_help_active) {
        _help_info_page_view.draw();
        return;
    }
    switch (_router.page()) {
        case PageId::CoverFlow:
            _cover_flow_view.draw();
            break;
        case PageId::AlbumList:
            _album_list_view.draw();
            break;
        case PageId::Playback:
            _playback_view.draw();
            break;
        case PageId::Info:
            _info_page_view.draw();
            break;
    }
}

bool MusicApp::quitRequested() const noexcept { return _quit_requested; }

void MusicApp::openSelectedAlbum()
{
    const Album* album = _cover_flow_view_model.selectedAlbum();
    if (!album) {
        return;
    }
    const MusicGuide* guide = findMusicGuide(album->guide_topic);
    if (guide) {
        showInfoPage({guide->page_title, guide->page_body}, _artwork_palette_cache.themeFor(album->cover_path),
                     PageId::CoverFlow);
        return;
    }

    const std::string album_id = album->id;
    const ui::PageTheme theme = _artwork_palette_cache.themeFor(album->cover_path);
    _cover_flow_view.onExit();
    _cover_flow_view_model.onExit();
    _album_list_view_model.setAlbumId(album_id);
    _album_list_view.setTheme(theme);
    _router.navigate(PageId::AlbumList);
    _album_list_view_model.onEnter();
    _album_list_view.onEnter(lv_screen_active());
}

void MusicApp::openSelectedAlbumInfo()
{
    const Album* album = _album_list_view_model.album();
    if (!album) {
        return;
    }
    const InfoPageContent content{album->title, albumInfoBody(*album, _album_list_view_model.trackAt(0))};
    showInfoPage(content, _artwork_palette_cache.themeFor(album->cover_path), PageId::AlbumList);
}

void MusicApp::openPlaybackPage()
{
    if (_router.page() != PageId::AlbumList || !_playback.snapshot().hasTrack()) {
        return;
    }
    _album_list_view.onExit();
    _album_list_view_model.onExit();
    _router.navigate(PageId::Playback);
    _playback_view_model.onEnter();
    _playback_theme_path.clear();
    updatePlaybackTheme();
    _playback_view.onEnter(lv_screen_active());
}

void MusicApp::updatePlaybackTheme()
{
    const Track* track = _playback_view_model.track();
    const std::filesystem::path cover_path = track ? track->cover_path : std::filesystem::path{};
    const std::filesystem::path display_path = displayCoverPath(cover_path);
    if (display_path == _playback_theme_path) {
        return;
    }
    _playback_theme_path = display_path;
    _playback_view.setTheme(_artwork_palette_cache.themeFor(cover_path));
}

void MusicApp::showInfoPage(InfoPageContent content, ui::PageTheme theme, PageId return_page)
{
    if (_router.page() == PageId::CoverFlow) {
        _cover_flow_view.onExit();
        _cover_flow_view_model.onExit();
    } else if (_router.page() == PageId::AlbumList) {
        _album_list_view.onExit();
        _album_list_view_model.onExit();
    }
    _info_return_page = return_page;
    _info_page_view.setContent(std::move(content));
    _info_page_view.setTheme(theme);
    _router.navigate(PageId::Info);
    _info_page_view_model.onEnter();
    _info_page_view.onEnter(lv_screen_active());
}

void MusicApp::showHelpPage()
{
    if (_help_active) {
        return;
    }

    _help_info_page_view.setContent({
        "Help",
        "Listen to music in the current user's \"music\" directory, with album artwork and lyrics.\n\n"
        "Sample music is hidden when the \"music\" directory contains music.\n\n"
        "Number keys 4-8: operations\n"
#if MUSIC_USE_SDL
        "Q / W / E: play-pause / previous / next\n"
        "A / S / D: simulate mute / volume down / up",
#else
        "Fn+Q / Fn+W / Fn+E: play-pause / previous / next\n"
        "Fn+A / Fn+S / Fn+D: mute / volume down / up",
#endif
    });
    _help_info_page_view.setTheme(ui::defaultPageTheme());
    _help_info_page_view_model.onEnter();
    _help_info_page_view.onEnter(lv_screen_active());
    _help_active = true;
}

void MusicApp::closeHelpPage()
{
    if (!_help_active) {
        return;
    }
    _help_info_page_view.onExit();
    _help_info_page_view_model.onExit();
    _help_active = false;
}

void MusicApp::createExitHint()
{
    if (_exit_hint) {
        return;
    }

    _exit_hint = lv_label_create(lv_layer_top());
    if (!_exit_hint) {
        return;
    }

    lv_label_set_text(_exit_hint, "Hold ESC to exit");
    lv_obj_set_size(_exit_hint, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(_exit_hint, font(FontFamily::Sans, FontSize::Px12), LV_PART_MAIN);
    lv_obj_set_style_text_color(_exit_hint, lv_color_hex(0xFED40D), LV_PART_MAIN);
    lv_obj_set_style_bg_color(_exit_hint, lv_color_hex(0x474747), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_exit_hint, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(_exit_hint, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(_exit_hint, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_left(_exit_hint, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_right(_exit_hint, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_top(_exit_hint, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(_exit_hint, 5, LV_PART_MAIN);
    lv_obj_set_style_text_align(_exit_hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(_exit_hint, LV_ALIGN_BOTTOM_MID, 0, -38);
    lv_obj_clear_flag(_exit_hint, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(_exit_hint, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(_exit_hint, LV_OBJ_FLAG_HIDDEN);
}

void MusicApp::showExitHint()
{
    if (!_exit_hint || !lv_obj_is_valid(_exit_hint)) {
        return;
    }
    lv_obj_remove_flag(_exit_hint, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(_exit_hint);
}

void MusicApp::hideExitHint()
{
    if (_exit_hint && lv_obj_is_valid(_exit_hint)) {
        lv_obj_add_flag(_exit_hint, LV_OBJ_FLAG_HIDDEN);
    }
}

void MusicApp::releaseEscPress()
{
    if (!_esc_pressed) {
        return;
    }

    if (!_esc_long_consumed) {
        handleEscapeNavigation();
    }
    hideExitHint();
    _esc_pressed = false;
    _esc_long_consumed = false;
    _esc_exit_armed = false;
    _esc_pressed_at = 0;
}

void MusicApp::handleEscapeNavigation()
{
    if (_help_active) {
        closeHelpPage();
        return;
    }

    switch (_router.page()) {
        case PageId::CoverFlow:
            // The cover flow is the app root. A short Escape press only shows
            // the hint; leaving the app requires holding it.
            break;
        case PageId::AlbumList:
            returnToCoverFlow();
            break;
        case PageId::Playback:
            returnFromPlayback();
            break;
        case PageId::Info:
            if (_info_page_view_model.magicActive()) {
                _info_page_view_model.onKey(music_key::Escape, true);
            } else {
                returnFromInfo();
            }
            break;
    }
}

void MusicApp::returnFromInfo()
{
    _info_page_view.onExit();
    _info_page_view_model.onExit();
    if (_info_return_page == PageId::AlbumList) {
        _router.navigate(PageId::AlbumList);
        _album_list_view_model.onEnter();
        _album_list_view.onEnter(lv_screen_active());
        return;
    }
    _router.navigate(PageId::CoverFlow);
    _cover_flow_view_model.onEnter();
    _cover_flow_view.onEnter(lv_screen_active());
}

void MusicApp::returnFromPlayback()
{
    if (_router.page() != PageId::Playback) {
        return;
    }
    _playback_view.onExit();
    _playback_view_model.onExit();
    _playback_theme_path.clear();
    _router.navigate(PageId::AlbumList);
    _album_list_view_model.onEnter();
    _album_list_view.onEnter(lv_screen_active());
}

void MusicApp::returnToCoverFlow()
{
    if (_router.page() == PageId::AlbumList) {
        _album_list_view.onExit();
        _album_list_view_model.onExit();
    }
    _router.navigate(PageId::CoverFlow);
    _cover_flow_view_model.onEnter();
    _cover_flow_view.onEnter(lv_screen_active());
}

}  // namespace music
