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

namespace music {
namespace {

constexpr int kVolumeShortcutDeltaPercent = 5;

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
    _volume_hud.start(lv_layer_top());
    _cover_flow_view_model.onEnter();
    _cover_flow_view.onEnter(lv_screen_active());
    _help_active = false;
}

void MusicApp::stop()
{
    if (!_started) {
        return;
    }
    if (_help_active) {
        closeHelpPage();
    }
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
    if (key == music_key::VolumeDown || key == music_key::VolumeUp) {
        if (pressed) {
            const int delta = key == music_key::VolumeUp ? kVolumeShortcutDeltaPercent : -kVolumeShortcutDeltaPercent;
#if MUSIC_USE_SDL
            // SDL has no CardputerZero Fn layer. Keep desktop testing side-effect free:
            // simulate the volume state instead of changing the host system volume.
            _desktop_volume_percent = SystemVolumeModel::clampPercent(_desktop_volume_percent + delta);
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
            if (pressed && key == music_key::Escape) {
                _quit_requested = true;
            } else if (pressed && key == music_key::Enter) {
                openSelectedAlbum();
            } else {
                _cover_flow_view_model.onKey(key, pressed);
            }
            break;
        case PageId::AlbumList:
            if (pressed && key == music_key::Escape) {
                returnToCoverFlow();
            } else if (pressed && key == music_key::NowPlaying) {
                openPlaybackPage();
            } else {
                _album_list_view_model.onKey(key, pressed);
                if (!pressed && key == music_key::Enter && _album_list_view_model.takeAlbumInfoRequested()) {
                    openSelectedAlbumInfo();
                }
            }
            break;
        case PageId::Playback:
            if (pressed && key == music_key::Escape) {
                returnFromPlayback();
            } else if (pressed && key == music_key::Up) {
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
            } else if (pressed && key == music_key::Escape) {
                returnFromInfo();
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

void MusicApp::update(float delta_seconds)
{
    _playback.update(delta_seconds);
    _volume_hud.update(delta_seconds);
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
        "S / D: simulate volume down / up",
#else
        "Fn+S / Fn+D: volume down / up",
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
