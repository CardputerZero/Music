#include "models/music_library_model.hpp"

#include <SQLiteCpp/SQLiteCpp.h>
#include <attachedpictureframe.h>
#include <audioproperties.h>
#include <fileref.h>
#include <tpropertymap.h>
#include <wavfile.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace fs = std::filesystem;

void require(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void appendU16(std::vector<unsigned char>& bytes, std::uint16_t value)
{
    bytes.push_back(static_cast<unsigned char>(value & 0xffU));
    bytes.push_back(static_cast<unsigned char>((value >> 8U) & 0xffU));
}

void appendU32(std::vector<unsigned char>& bytes, std::uint32_t value)
{
    bytes.push_back(static_cast<unsigned char>(value & 0xffU));
    bytes.push_back(static_cast<unsigned char>((value >> 8U) & 0xffU));
    bytes.push_back(static_cast<unsigned char>((value >> 16U) & 0xffU));
    bytes.push_back(static_cast<unsigned char>((value >> 24U) & 0xffU));
}

void appendU64(std::vector<unsigned char>& bytes, std::uint64_t value)
{
    for (unsigned int shift = 0; shift < 64; shift += 8) {
        bytes.push_back(static_cast<unsigned char>((value >> shift) & 0xffU));
    }
}

void appendText(std::vector<unsigned char>& bytes, const char* text, std::size_t size)
{
    bytes.insert(bytes.end(), text, text + size);
}

void createWave(const fs::path& path)
{
    constexpr std::uint32_t sample_rate = 8000;
    constexpr std::uint16_t channels = 1;
    constexpr std::uint16_t bits = 16;
    constexpr std::uint32_t sample_count = 800;
    constexpr std::uint32_t data_size = sample_count * channels * (bits / 8U);

    std::vector<unsigned char> bytes;
    bytes.reserve(44 + data_size);
    appendText(bytes, "RIFF", 4);
    appendU32(bytes, 36 + data_size);
    appendText(bytes, "WAVEfmt ", 8);
    appendU32(bytes, 16);
    appendU16(bytes, 1);
    appendU16(bytes, channels);
    appendU32(bytes, sample_rate);
    appendU32(bytes, sample_rate * channels * (bits / 8U));
    appendU16(bytes, channels * (bits / 8U));
    appendU16(bytes, bits);
    appendText(bytes, "data", 4);
    appendU32(bytes, data_size);
    bytes.resize(44 + data_size, 0);

    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    require(static_cast<bool>(stream), "failed to create WAV fixture");
}

void createWave64(const fs::path& path)
{
    constexpr std::uint32_t sample_rate = 8000;
    constexpr std::uint16_t channels = 1;
    constexpr std::uint16_t bits = 16;
    constexpr std::uint32_t sample_count = 800;
    constexpr std::uint64_t data_size = sample_count * channels * (bits / 8U);
    constexpr std::uint64_t file_size = 40 + 40 + 24 + data_size;
    constexpr unsigned char riff_guid[] = {0x72, 0x69, 0x66, 0x66, 0x2e, 0x91, 0xcf, 0x11,
                                           0xa5, 0xd6, 0x28, 0xdb, 0x04, 0xc1, 0x00, 0x00};
    constexpr unsigned char wave_guid[] = {0x77, 0x61, 0x76, 0x65, 0xf3, 0xac, 0xd3, 0x11,
                                           0x8c, 0xd1, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a};
    constexpr unsigned char fmt_guid[] = {0x66, 0x6d, 0x74, 0x20, 0xf3, 0xac, 0xd3, 0x11,
                                          0x8c, 0xd1, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a};
    constexpr unsigned char data_guid[] = {0x64, 0x61, 0x74, 0x61, 0xf3, 0xac, 0xd3, 0x11,
                                           0x8c, 0xd1, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a};

    std::vector<unsigned char> bytes;
    bytes.reserve(static_cast<std::size_t>(file_size));
    bytes.insert(bytes.end(), std::begin(riff_guid), std::end(riff_guid));
    appendU64(bytes, file_size);
    bytes.insert(bytes.end(), std::begin(wave_guid), std::end(wave_guid));
    bytes.insert(bytes.end(), std::begin(fmt_guid), std::end(fmt_guid));
    appendU64(bytes, 40);
    appendU16(bytes, 1);
    appendU16(bytes, channels);
    appendU32(bytes, sample_rate);
    appendU32(bytes, sample_rate * channels * (bits / 8U));
    appendU16(bytes, channels * (bits / 8U));
    appendU16(bytes, bits);
    bytes.insert(bytes.end(), std::begin(data_guid), std::end(data_guid));
    appendU64(bytes, 24 + data_size);
    bytes.resize(static_cast<std::size_t>(file_size), 0);

    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    require(static_cast<bool>(stream), "failed to create Wave64 fixture");
}

void tagWave(const fs::path& path)
{
    TagLib::RIFF::WAV::File file(path.c_str());
    require(file.isValid(), "TagLib rejected WAV fixture");

    TagLib::PropertyMap properties;
    properties["TITLE"] = TagLib::StringList("Signal");
    properties["ARTIST"] = TagLib::StringList("Test Artist");
    properties["ALBUM"] = TagLib::StringList("Test Album");
    properties["ALBUMARTIST"] = TagLib::StringList("Various Testers");
    properties["GENRE"] = TagLib::StringList("Test");
    properties["DATE"] = TagLib::StringList("2026");
    properties["TRACKNUMBER"] = TagLib::StringList("3/9");
    properties["DISCNUMBER"] = TagLib::StringList("2/2");
    properties["USLT"] = TagLib::StringList("Embedded test lyrics");
    const auto unsupported = file.setProperties(properties);
    require(unsupported.isEmpty(), "WAV fixture metadata contains unsupported properties");

    auto* picture = new TagLib::ID3v2::AttachedPictureFrame();
    picture->setType(TagLib::ID3v2::AttachedPictureFrame::FrontCover);
    picture->setMimeType("image/jpeg");
    const char jpeg[] = {static_cast<char>(0xff), static_cast<char>(0xd8), static_cast<char>(0xff),
                         static_cast<char>(0xd9)};
    picture->setPicture(TagLib::ByteVector(jpeg, sizeof(jpeg)));
    file.ID3v2Tag()->addFrame(picture);
    require(file.save(), "failed to save tagged WAV fixture");
}

void tagWaveWithoutEmbeddedAssets(const fs::path& path)
{
    TagLib::RIFF::WAV::File file(path.c_str());
    require(file.isValid(), "TagLib rejected asset WAV fixture");

    TagLib::PropertyMap properties;
    properties["TITLE"] = TagLib::StringList("Asset Song");
    properties["ARTIST"] = TagLib::StringList("Asset Artist");
    properties["ALBUM"] = TagLib::StringList("Asset Album");
    const auto unsupported = file.setProperties(properties);
    require(unsupported.isEmpty(), "asset WAV fixture metadata contains unsupported properties");
    require(file.save(), "failed to save asset WAV fixture");
}

void writeFile(const fs::path& path, const std::string& contents)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    require(static_cast<bool>(stream), "failed to write fixture " + path.string());
}

std::string readFile(const fs::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    std::ostringstream contents;
    contents << stream.rdbuf();
    require(static_cast<bool>(stream) || stream.eof(), "failed to read fixture " + path.string());
    return contents.str();
}

std::string legacyLyricsCacheName(const fs::path& audio_path)
{
    std::uint64_t hash = UINT64_C(14695981039346656037);
    for (const unsigned char character : audio_path.string()) {
        hash ^= character;
        hash *= UINT64_C(1099511628211);
    }
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16) << hash << ".lrc";
    return stream.str();
}

std::shared_ptr<const music::LibrarySnapshot> waitForRevision(music::MusicLibraryModel& model, std::uint64_t revision)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        const auto snapshot = model.snapshot();
        const auto state = model.scanState();
        if (snapshot && snapshot->revision >= revision && state.phase == music::ScanPhase::Complete) {
            return snapshot;
        }
        if (state.phase == music::ScanPhase::Error) {
            throw std::runtime_error("music scan failed: " + state.message);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    throw std::runtime_error("music scan timed out");
}

void rescan(music::MusicLibraryModel& model, std::uint64_t revision)
{
    model.requestRescan();
    (void)waitForRevision(model, revision);
}

void runLibraryTest(const fs::path& temporary)
{
    const fs::path library = temporary / "library";
    const fs::path cache = temporary / "cache";
    const fs::path song = library / "song.wav";
    fs::create_directories(library);
    createWave(song);
    tagWave(song);

    {
        std::ofstream(library / "song.lrc") << "[00:00.00]Sidecar lyrics\n";
        const char jpeg[] = {static_cast<char>(0xff), static_cast<char>(0xd8), static_cast<char>(0xff),
                             static_cast<char>(0xd9)};
        std::ofstream cover(library / "cover.jpg", std::ios::binary);
        cover.write(jpeg, sizeof(jpeg));
    }

    music::LibraryConfig config;
    config.roots = {library};
    config.database_path = temporary / "data" / "library.db";
    config.cache_dir = cache;

    music::MusicLibraryModel model(config);
    model.start();
    auto snapshot = waitForRevision(model, 1);
    require(snapshot->tracks.size() == 1, "initial scan did not index one track");
    require(!snapshot->albums.empty() && snapshot->albums.front().all_music, "All Music is not the first album");
    require(snapshot->albums.size() == 2, "tagged album was not aggregated");

    const music::Track& track = snapshot->tracks.front();
    require(track.title == "Signal", "title metadata mismatch");
    require(track.artist == "Test Artist", "artist metadata mismatch");
    require(track.album == "Test Album", "album metadata mismatch");
    require(track.album_artist == "Various Testers", "album artist metadata mismatch");
    require(track.genre == "Test", "genre metadata mismatch");
    require(track.year == 2026 && track.track_number == 3 && track.disc_number == 2, "numeric metadata mismatch");
    require(track.duration_ms > 0, "duration metadata is missing");
    require(track.size_bytes > 0 && track.mtime_ns != 0, "file fingerprint is missing");
    require(!track.embedded_cover_path.empty() && fs::exists(track.embedded_cover_path),
            "embedded cover was not cached");
    require(track.cover_path == track.embedded_cover_path, "embedded cover is not preferred");
    require(track.external_cover_path.filename() == "cover.jpg", "external cover was not associated");
    require(track.lyrics_path.filename() == "song.lrc", "sidecar lyrics were not preferred");
    require(track.embedded_lyrics == "Embedded test lyrics", "embedded lyrics metadata mismatch");

    const std::uint64_t first_revision = snapshot->revision;
    rescan(model, first_revision + 1);
    require(model.scanState().files_changed == 0, "unchanged track was parsed again");

    const fs::path second_song = library / "untagged.wav";
    createWave(second_song);
    rescan(model, first_revision + 2);
    snapshot = model.snapshot();
    require(snapshot->tracks.size() == 2, "incremental scan did not add a new track");
    require(model.scanState().files_changed == 1, "incremental scan changed count mismatch");

    fs::remove(song);
    rescan(model, first_revision + 3);
    snapshot = model.snapshot();
    require(snapshot->tracks.size() == 1 && model.scanState().files_removed == 1,
            "incremental scan did not remove deleted track");
    require(snapshot->albums.size() == 4 && snapshot->albums[1].guide_topic == music::GuideTopic::AddMusic &&
                snapshot->albums[2].guide_topic == music::GuideTopic::CoverArt &&
                snapshot->albums[3].guide_topic == music::GuideTopic::Lyrics,
            "guides did not replace the removed real album");

    const fs::path unavailable = temporary / "library-away";
    fs::rename(library, unavailable);
    rescan(model, first_revision + 4);
    require(model.snapshot()->tracks.size() == 1, "temporarily unavailable root erased its index");
    fs::rename(unavailable, library);

    model.stop();
}

void runAssociatedAssetTest(const fs::path& temporary)
{
    const fs::path library = temporary / "asset-library";
    const fs::path cache = temporary / "asset-cache";
    const fs::path database_path = temporary / "asset-data" / "library.db";
    const fs::path song = library / "asset.wav";
    const fs::path cover = library / "cover.jpg";
    const fs::path lyrics = library / "asset.lrc";
    fs::create_directories(library);
    createWave(song);
    tagWaveWithoutEmbeddedAssets(song);

    music::LibraryConfig config;
    config.roots = {library};
    config.database_path = database_path;
    config.cache_dir = cache;

    music::MusicLibraryModel model(config);
    model.start();
    auto snapshot = waitForRevision(model, 1);
    require(snapshot->tracks.size() == 1, "asset fixture was not indexed");
    require(snapshot->albums.size() == 2, "asset fixture album was not aggregated");
    require(snapshot->tracks.front().cover_path.empty(), "asset fixture unexpectedly has artwork");
    require(snapshot->tracks.front().lyrics_path.empty(), "asset fixture unexpectedly has lyrics");
    const std::uint64_t audio_size = snapshot->tracks.front().size_bytes;
    const std::int64_t audio_mtime = snapshot->tracks.front().mtime_ns;

    {
        SQLite::Database database(database_path.string(), SQLite::OPEN_READWRITE);
        database.exec("PRAGMA busy_timeout=3000");
        SQLite::Statement update(database, "UPDATE tracks SET title=? WHERE path=?");
        update.bind(1, "Database Sentinel");
        update.bind(2, song.string());
        require(update.exec() == 1, "failed to install metadata reparse sentinel");
    }

    writeFile(cover, "cover-v1");
    writeFile(lyrics, "[00]one\n");
    rescan(model, snapshot->revision + 1);
    snapshot = model.snapshot();
    require(model.scanState().files_changed == 1, "new associated assets were not reported as changed");
    const music::Track added = snapshot->tracks.front();
    require(added.title == "Database Sentinel", "asset-only update reparsed audio metadata");
    require(added.size_bytes == audio_size && added.mtime_ns == audio_mtime,
            "asset-only update changed the audio fingerprint");
    require(added.external_cover_path == cover, "new external artwork was not associated");
    require(added.cover_path != cover && fs::is_regular_file(added.cover_path),
            "external artwork was not copied to a versioned cache path");
    require(readFile(added.cover_path) == "cover-v1", "cached external artwork content mismatch");
    require(added.sidecar_lyrics_path == lyrics && added.lyrics_path == lyrics,
            "new lyrics sidecar was not associated");
    require(snapshot->albums[1].cover_path == added.cover_path, "album did not publish the cached artwork path");
    const fs::path first_cover_cache = added.cover_path;
    const std::string first_cover_hash = added.external_cover_hash;
    const std::string first_lyrics_hash = added.sidecar_lyrics_hash;

    const auto lyrics_time = fs::last_write_time(lyrics);
    writeFile(lyrics, "[00]two\n");
    fs::last_write_time(lyrics, lyrics_time);
    rescan(model, snapshot->revision + 1);
    snapshot = model.snapshot();
    const music::Track lyrics_modified = snapshot->tracks.front();
    require(model.scanState().files_changed == 1, "modified lyrics sidecar was not reported as changed");
    require(lyrics_modified.title == "Database Sentinel", "lyrics update reparsed audio metadata");
    require(lyrics_modified.sidecar_lyrics_hash != first_lyrics_hash,
            "lyrics content change with stable size/mtime was missed");
    require(readFile(lyrics_modified.lyrics_path) == "[00]two\n", "modified lyrics sidecar was not published");
    require(lyrics_modified.cover_path == first_cover_cache, "lyrics-only update replaced unchanged artwork");

    const auto cover_time = fs::last_write_time(cover);
    writeFile(cover, "cover-v2");
    fs::last_write_time(cover, cover_time);
    rescan(model, snapshot->revision + 1);
    snapshot = model.snapshot();
    const music::Track cover_modified = snapshot->tracks.front();
    require(model.scanState().files_changed == 1, "modified external artwork was not reported as changed");
    require(cover_modified.title == "Database Sentinel", "artwork update reparsed audio metadata");
    require(cover_modified.external_cover_hash != first_cover_hash,
            "artwork content change with stable size/mtime was missed");
    require(cover_modified.cover_path != first_cover_cache,
            "same-path artwork content change did not version the UI source path");
    require(readFile(cover_modified.cover_path) == "cover-v2", "modified external artwork was not cached");
    require(snapshot->albums[1].cover_path == cover_modified.cover_path,
            "album did not publish the changed artwork path");
    require(!fs::exists(first_cover_cache), "stale external artwork cache was not cleaned");
    const fs::path second_cover_cache = cover_modified.cover_path;

    const fs::path legacy_lyrics = cache / "lyrics" / legacyLyricsCacheName(song);
    fs::create_directories(legacy_lyrics.parent_path());
    writeFile(legacy_lyrics, "obsolete derived lyrics");
    fs::remove(lyrics);
    rescan(model, snapshot->revision + 1);
    snapshot = model.snapshot();
    const music::Track lyrics_removed = snapshot->tracks.front();
    require(model.scanState().files_changed == 1, "deleted lyrics sidecar was not reported as changed");
    require(lyrics_removed.sidecar_lyrics_path.empty() && lyrics_removed.lyrics_path.empty(),
            "deleted sidecar or legacy embedded cache resurrected lyrics");
    require(!fs::exists(legacy_lyrics), "ambiguous legacy lyrics cache was not cleaned");
    require(lyrics_removed.cover_path == second_cover_cache, "lyrics deletion replaced unchanged artwork");

    fs::remove(cover);
    rescan(model, snapshot->revision + 1);
    snapshot = model.snapshot();
    const music::Track cover_removed = snapshot->tracks.front();
    require(model.scanState().files_changed == 1, "deleted external artwork was not reported as changed");
    require(cover_removed.title == "Database Sentinel", "artwork deletion reparsed audio metadata");
    require(cover_removed.external_cover_path.empty() && cover_removed.cover_path.empty(),
            "deleted external artwork remained associated");
    require(snapshot->albums[1].cover_path.empty(), "album retained deleted external artwork");
    require(!fs::exists(second_cover_cache), "deleted external artwork cache was not cleaned");

    model.stop();
}

void runNestedUnavailableRootTest(const fs::path& temporary)
{
    const fs::path library = temporary / "nested-root-library";
    const fs::path mounted = library / "mounted";
    const fs::path mounted_away = temporary / "nested-root-away";
    fs::create_directories(mounted);
    createWave(mounted / "nested.wav");

    music::LibraryConfig config;
    config.roots = {library, mounted};
    config.database_path = temporary / "nested-root-data" / "library.db";
    config.cache_dir = temporary / "nested-root-cache";

    music::MusicLibraryModel model(config);
    model.start();
    auto snapshot = waitForRevision(model, 1);
    require(snapshot->tracks.size() == 1, "nested scan root fixture was not indexed");

    fs::rename(mounted, mounted_away);
    rescan(model, snapshot->revision + 1);
    snapshot = model.snapshot();
    require(snapshot->tracks.size() == 1 && snapshot->tracks.front().path.filename() == "nested.wav",
            "available parent root erased an unavailable nested root index");
    require(model.scanState().files_removed == 0, "unavailable nested root was reported as deleted");
    fs::rename(mounted_away, mounted);

    model.stop();
}

void runRetryableParseFailureTest(const fs::path& temporary)
{
    const fs::path library = temporary / "retry-library";
    const fs::path valid_fixture = temporary / "retry-valid.wav";
    const fs::path song = library / "retry.wav";
    fs::create_directories(library);
    createWave(valid_fixture);
    const std::string valid_bytes = readFile(valid_fixture);
    writeFile(song, std::string(valid_bytes.size(), '\0'));
    const auto original_time = fs::last_write_time(song);

    music::LibraryConfig config;
    config.roots = {library};
    config.database_path = temporary / "retry-data" / "library.db";
    config.cache_dir = temporary / "retry-cache";

    music::MusicLibraryModel model(config);
    auto snapshot = model.snapshot();
    require(snapshot->tracks.empty(), "bundled examples appeared before the initial scan completed");
    require(snapshot->albums.size() == 4, "initial snapshot did not contain All Music and the setup guides");

    model.start();
    snapshot = waitForRevision(model, 1);
    require(snapshot->tracks.size() == 4, "empty library did not publish four example tracks");
    require(model.scanState().files_changed == 0, "failed audio parse was reported as indexed");
    require(model.scanState().files_failed == 1, "failed audio parse was not reported in the scan result");
    require(snapshot->albums.size() == 5,
            "empty library did not publish All Music, the example album, and three guides");
    require(snapshot->albums[0].cover_path.filename() == "all-music.jpg" &&
                snapshot->albums[0].cover_path.parent_path().filename() == "covers",
            "All Music did not use its bundled cover");
    require(snapshot->albums[1].title == "Piano Classics" && snapshot->albums[1].track_count == 3 &&
                snapshot->albums[1].cover_path.filename() == "examples.jpg",
            "empty library example album metadata is incorrect");
    require(snapshot->albums[2].guide_topic == music::GuideTopic::AddMusic &&
                snapshot->albums[3].guide_topic == music::GuideTopic::CoverArt &&
                snapshot->albums[4].guide_topic == music::GuideTopic::Lyrics,
            "empty library guide order or topics are incorrect");
    std::size_t piano_tracks = 0;
    bool found_daisy = false;
    for (const auto& track : snapshot->tracks) {
        require(track.id < 0 && fs::is_regular_file(track.path), "bundled example track is invalid");
        if (track.album == "Piano Classics") {
            ++piano_tracks;
        }
        if (track.id == -4) {
            found_daisy = track.title == "Daisy Bell (Bicycle Built for Two)" && track.artist == "IBM 7094" &&
                          track.album.empty() && fs::is_regular_file(track.lyrics_path);
        }
        TagLib::FileRef reference(track.path.c_str(), true, TagLib::AudioProperties::Fast);
        require(!reference.isNull() && reference.file() && reference.file()->isValid() && reference.audioProperties() &&
                    reference.audioProperties()->lengthInMilliseconds() > 0,
                "TagLib could not parse a bundled example track");
    }
    require(piano_tracks == 3, "Piano Classics example album track count changed unexpectedly");
    require(found_daisy, "Daisy Bell example or its synchronized lyrics are missing");

    writeFile(song, valid_bytes);
    fs::last_write_time(song, original_time);
    rescan(model, snapshot->revision + 1);
    snapshot = model.snapshot();
    require(snapshot->tracks.size() == 1 && snapshot->tracks.front().id > 0,
            "audio parse retry did not replace the bundled examples with user music");
    require(snapshot->tracks.front().duration_ms > 0, "retried audio metadata was not parsed");

    model.stop();
}

void runDecoderFallbackTest(const fs::path& temporary)
{
    const fs::path library = temporary / "fallback-library";
    const fs::path song = library / "fallback.wav";
    const fs::path cover = library / "fallback.jpg";
    const fs::path lyrics = library / "fallback.lrc";
    fs::create_directories(library);
    createWave64(song);
    writeFile(cover, "fallback-cover");
    writeFile(lyrics, "[00:00.00]Fallback lyrics\n");

    TagLib::FileRef reference(song.c_str(), true, TagLib::AudioProperties::Fast);
    require(reference.isNull() || !reference.file() || !reference.file()->isValid() || !reference.audioProperties() ||
                reference.audioProperties()->lengthInMilliseconds() <= 0,
            "TagLib unexpectedly accepted the miniaudio fallback fixture");

    music::LibraryConfig config;
    config.roots = {library};
    config.database_path = temporary / "fallback-data" / "library.db";
    config.cache_dir = temporary / "fallback-cache";

    music::MusicLibraryModel model(config);
    model.start();
    const auto snapshot = waitForRevision(model, 1);
    require(snapshot->tracks.size() == 1, "miniaudio fallback track was not indexed");
    const music::Track& track = snapshot->tracks.front();
    require(track.title == "fallback", "fallback track did not use its filename as the title");
    require(track.duration_ms >= 95 && track.duration_ms <= 105, "fallback track duration is incorrect");
    require(track.external_cover_path == cover && !track.cover_path.empty() && fs::is_regular_file(track.cover_path),
            "fallback track did not retain its external artwork");
    require(track.sidecar_lyrics_path == lyrics && track.lyrics_path == lyrics,
            "fallback track did not retain its sidecar lyrics");
    require(model.scanState().files_changed == 1 && model.scanState().files_failed == 0,
            "successful miniaudio fallback was reported as a scan failure");
    model.stop();
}

}  // namespace

int main()
{
    const fs::path temporary =
        fs::temp_directory_path() /
        ("music-library-test-" + std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    try {
        runLibraryTest(temporary);
        runAssociatedAssetTest(temporary);
        runNestedUnavailableRootTest(temporary);
        runRetryableParseFailureTest(temporary);
        runDecoderFallbackTest(temporary);
        std::error_code error;
        fs::remove_all(temporary, error);
        std::cout << "music_library_model_test: PASS\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "music_library_model_test: FAIL: " << exception.what() << '\n';
        std::error_code error;
        fs::remove_all(temporary, error);
        return 1;
    }
}
