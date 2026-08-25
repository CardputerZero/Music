#include "core/music_guides.hpp"

#include "assets/runtime_assets.hpp"

#include <algorithm>

namespace music {
namespace {

const std::array<MusicGuide, 3> kGuides = {
    MusicGuide{
        GuideTopic::AddMusic,
        "guide-add-music",
        "Add Your Music",
        "Press Enter for guide",
        "guide-add-music.jpg",
        "Add Your Music",
        "Copy MP3, FLAC, M4A, WAV, OGG, or OPUS files into the Music folder. Subfolders are optional.\n\n"
        "Album and album-artist tags determine how songs are grouped in Cover Flow.\n\n"
        "Reopen Music after adding or removing songs to refresh the library.",
    },
    MusicGuide{
        GuideTopic::CoverArt,
        "guide-cover-art",
        "Album Covers",
        "Press Enter for guide",
        "guide-cover-art.jpg",
        "Album Covers",
        "Embed a square front cover for the best result.\n\n"
        "Or place a JPG, PNG, or BMP beside the tracks and name it cover, folder, or front, such as cover.jpg. A "
        "file matching the song name works too.\n\n"
        "Embedded artwork takes priority.",
    },
    MusicGuide{
        GuideTopic::Lyrics,
        "guide-lyrics",
        "Add Lyrics",
        "Press Enter for guide",
        "guide-lyrics.jpg",
        "Add Lyrics",
        "Place a UTF-8 LRC file beside the song and give both files the same name.\n\n"
        "song.flac\n"
        "song.lrc\n\n"
        "Add timestamps such as [00:12.50] for synchronized lyrics. Embedded lyrics are supported; a matching LRC "
        "file takes priority.",
    },
};

Track makeExampleTrack(std::int64_t id, const char* filename, const char* title, const char* artist, const char* genre,
                       int year, int track_number, std::int64_t duration_ms)
{
    Track track;
    track.id = id;
    track.path = assetPath(std::filesystem::path("examples") / filename);
    track.title = title;
    track.artist = artist;
    track.album = "Piano Classics";
    track.album_artist = "Various Artists";
    track.genre = genre;
    track.year = year;
    track.track_number = track_number;
    track.duration_ms = duration_ms;
    track.cover_path = assetPath("covers/examples.jpg");
    return track;
}

Track makeDaisyBellTrack()
{
    Track track;
    track.id = -4;
    track.path = assetPath("examples/Daisy Bell (Bicycle Built for Two).mp3");
    track.title = "Daisy Bell (Bicycle Built for Two)";
    track.artist = "IBM 7094";
    track.genre = "Popular Song";
    track.year = 1892;
    track.duration_ms = 38635;
    track.lyrics_path = assetPath("examples/Daisy Bell (Bicycle Built for Two).lrc");
    return track;
}

}  // namespace

const std::array<MusicGuide, 3>& musicGuides() { return kGuides; }

const std::array<Track, 4>& exampleTracks()
{
    static const std::array<Track, 4> tracks = {
        makeExampleTrack(-1, "01 - Maple Leaf Rag.mp3", "Maple Leaf Rag", "Scott Joplin", "Ragtime", 1899, 1, 169561),
        makeExampleTrack(-2, "02 - Clair de Lune.mp3", "Clair de Lune, L. 32", "Claude Debussy", "Classical", 1905, 2,
                         246962),
        makeExampleTrack(-3, "03 - Liebesträume No. 3.mp3", "Liebesträume No. 3, S. 541/3", "Franz Liszt", "Classical",
                         1850, 3, 243879),
        makeDaisyBellTrack(),
    };
    return tracks;
}

const MusicGuide* findMusicGuide(GuideTopic topic)
{
    const auto guide = std::find_if(kGuides.begin(), kGuides.end(),
                                    [topic](const MusicGuide& candidate) { return candidate.topic == topic; });
    return guide == kGuides.end() ? nullptr : &*guide;
}

Album makeAllMusicAlbum()
{
    Album album;
    album.id = "all-music";
    album.title = "All Music";
    album.cover_path = assetPath("covers/all-music.jpg");
    album.all_music = true;
    return album;
}

Album makeGuideAlbum(const MusicGuide& guide)
{
    Album album;
    album.id = guide.album_id;
    album.title = guide.album_title;
    album.artist = guide.album_subtitle;
    album.guide_topic = guide.topic;
    album.cover_path = assetPath(std::filesystem::path("covers") / guide.cover_relative_path);
    return album;
}

}  // namespace music
