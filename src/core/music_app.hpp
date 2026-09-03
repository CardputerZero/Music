#pragma once

#include "core/music_config.hpp"
#include "core/music_router.hpp"
#include "models/music_library_model.hpp"
#include "models/playback_model.hpp"
#include "rendering/artwork_palette.hpp"
#include "view_models/album_list_view_model.hpp"
#include "view_models/cover_flow_view_model.hpp"
#include "view_models/info_page_view_model.hpp"
#include "view_models/playback_view_model.hpp"
#include "views/album_list_view.hpp"
#include "views/cover_flow_view.hpp"
#include "views/info_page_view.hpp"
#include "views/playback_view.hpp"

#include <cstdint>
#include <filesystem>

namespace music {

class MusicApp {
public:
    explicit MusicApp(MusicConfig config);
    ~MusicApp();

    MusicApp(const MusicApp&) = delete;
    MusicApp& operator=(const MusicApp&) = delete;

    void start();
    void stop();
    void onKey(std::uint32_t key, bool pressed);
    void update(float delta_seconds);
    void draw();
    bool quitRequested() const noexcept;

private:
    MusicConfig _config;
    MusicRouter _router;
    MusicLibraryModel _library;
    PlaybackModel _playback;
    CoverFlowViewModel _cover_flow_view_model;
    AlbumListViewModel _album_list_view_model;
    PlaybackViewModel _playback_view_model;
    InfoPageViewModel _info_page_view_model;
    CoverFlowView _cover_flow_view;
    AlbumListView _album_list_view;
    PlaybackView _playback_view;
    InfoPageView _info_page_view;
    InfoPageViewModel _help_info_page_view_model;
    InfoPageView _help_info_page_view;
    rendering::ArtworkPaletteCache _artwork_palette_cache;
    std::filesystem::path _playback_theme_path;
    PageId _info_return_page = PageId::CoverFlow;
    bool _help_active = false;
    bool _started = false;
    bool _quit_requested = false;

    void openSelectedAlbum();
    void openSelectedAlbumInfo();
    void openPlaybackPage();
    void updatePlaybackTheme();
    void showInfoPage(InfoPageContent content, ui::PageTheme theme, PageId return_page);
    void returnFromInfo();
    void showHelpPage();
    void closeHelpPage();
    void returnFromPlayback();
    void returnToCoverFlow();
};

}  // namespace music
