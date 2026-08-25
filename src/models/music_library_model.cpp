#include "models/music_library_model.hpp"

#include "core/music_guides.hpp"

#include <SQLiteCpp/SQLiteCpp.h>
#include <attachedpictureframe.h>
#include <audioproperties.h>
#include <fileref.h>
#include <flacfile.h>
#include <flacpicture.h>
#include <id3v2tag.h>
#include <mpegfile.h>
#include <mp4coverart.h>
#include <mp4file.h>
#include <mp4item.h>
#include <mp4tag.h>
#include <miniaudio.h>
#include <spdlog/spdlog.h>
#include <tag.h>
#include <tpropertymap.h>
#include <tvariant.h>
#include <wavfile.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace music {
namespace {

namespace fs = std::filesystem;

constexpr int kSchemaVersion = 2;
void appendGuideAlbums(std::vector<Album>& albums)
{
    for (const auto& guide : musicGuides()) {
        albums.push_back(makeGuideAlbum(guide));
    }
}

bool appendExampleTracksIfAvailable(std::vector<Track>& tracks)
{
    const auto& examples = exampleTracks();
    const bool available = std::all_of(examples.begin(), examples.end(), [](const Track& track) {
        std::error_code error;
        if (!fs::is_regular_file(track.path, error) || error) {
            return false;
        }
        return track.lyrics_path.empty() || (fs::is_regular_file(track.lyrics_path, error) && !error);
    });
    if (!available) {
        spdlog::warn("Music library: bundled example tracks are incomplete; showing setup guides only");
        return false;
    }
    tracks.insert(tracks.end(), examples.begin(), examples.end());
    return true;
}

struct FileFingerprint {
    std::uint64_t size_bytes = 0;
    std::int64_t mtime_ns = 0;
};

struct AssetFingerprint : FileFingerprint {
    std::string content_hash;
};

struct AssociatedAssets {
    fs::path external_cover_path;
    std::optional<AssetFingerprint> external_cover_fingerprint;
    fs::path sidecar_lyrics_path;
    std::optional<AssetFingerprint> sidecar_lyrics_fingerprint;
};

struct RootScanStatus {
    fs::path path;
    bool complete = false;
};

struct CoverData {
    TagLib::ByteVector bytes;
    std::string extension;
};

constexpr ma_uint32 kDecoderProbeChannels = 2;
constexpr ma_uint32 kDecoderProbeSampleRate = 48000;

std::string lowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return value;
}

std::string trim(std::string value)
{
    const auto is_space = [](unsigned char character) { return std::isspace(character) != 0; };
    value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), is_space));
    value.erase(std::find_if_not(value.rbegin(), value.rend(), is_space).base(), value.end());
    return value;
}

std::string tagString(const TagLib::String& value) { return trim(value.to8Bit(true)); }

std::string propertyValue(const TagLib::PropertyMap& properties, std::initializer_list<const char*> candidate_keys)
{
    for (const char* candidate : candidate_keys) {
        const TagLib::String key(candidate, TagLib::String::UTF8);
        if (!properties.contains(key)) {
            continue;
        }
        const auto values = properties[key];
        if (!values.isEmpty()) {
            const std::string result = tagString(values.front());
            if (!result.empty()) {
                return result;
            }
        }
    }
    return {};
}

std::string lyricsValue(const TagLib::PropertyMap& properties)
{
    if (const std::string value = propertyValue(properties, {"LYRICS", "USLT", "UNSYNCEDLYRICS", "UNSYNCED LYRICS"});
        !value.empty()) {
        return value;
    }

    for (const auto& [key, values] : properties) {
        const std::string normalized_key = lowerAscii(tagString(key));
        const bool is_lyrics = normalized_key.rfind("lyrics:", 0) == 0 || normalized_key.rfind("uslt:", 0) == 0 ||
                               normalized_key.rfind("unsyncedlyrics:", 0) == 0 ||
                               normalized_key.rfind("unsynced lyrics:", 0) == 0;
        if (is_lyrics && !values.isEmpty()) {
            if (const std::string value = tagString(values.front()); !value.empty()) {
                return value;
            }
        }
    }
    return {};
}

int leadingInteger(const std::string& value)
{
    const auto start =
        std::find_if(value.begin(), value.end(), [](unsigned char character) { return std::isdigit(character) != 0; });
    if (start == value.end()) {
        return 0;
    }

    std::uint64_t result = 0;
    auto cursor = start;
    while (cursor != value.end() && std::isdigit(static_cast<unsigned char>(*cursor)) != 0) {
        result = result * 10 + static_cast<unsigned int>(*cursor - '0');
        if (result > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
            return 0;
        }
        ++cursor;
    }
    return static_cast<int>(result);
}

bool isAudioPath(const fs::path& path)
{
    static const std::unordered_set<std::string> extensions = {
        ".aac", ".aif", ".aiff", ".ape",  ".flac", ".m4a",  ".m4b", ".mp3", ".mp4",
        ".mpc", ".oga", ".ogg",  ".opus", ".wav",  ".wave", ".wma", ".wv",
    };
    return extensions.count(lowerAscii(path.extension().string())) != 0;
}

fs::path normalizedPath(const fs::path& path)
{
    std::error_code error;
    fs::path absolute = fs::absolute(path, error);
    if (error) {
        absolute = path;
        error.clear();
    }
    fs::path canonical = fs::weakly_canonical(absolute, error);
    return error ? absolute.lexically_normal() : canonical;
}

std::optional<FileFingerprint> fingerprint(const fs::path& path)
{
    std::error_code error;
    const std::uintmax_t raw_size = fs::file_size(path, error);
    if (error || raw_size > static_cast<std::uintmax_t>(std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }

    const auto write_time = fs::last_write_time(path, error);
    if (error) {
        return std::nullopt;
    }

    FileFingerprint result;
    result.size_bytes = static_cast<std::uint64_t>(raw_size);
    result.mtime_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(write_time.time_since_epoch()).count();
    return result;
}

std::uint64_t fnv1a(const std::string& value)
{
    std::uint64_t hash = UINT64_C(14695981039346656037);
    for (unsigned char character : value) {
        hash ^= character;
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

std::string hexValue(std::uint64_t value)
{
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16) << value;
    return stream.str();
}

std::string hexHash(const std::string& value) { return hexValue(fnv1a(value)); }

std::optional<AssetFingerprint> assetFingerprint(const fs::path& path)
{
    const auto file_fingerprint = fingerprint(path);
    if (!file_fingerprint) {
        return std::nullopt;
    }

    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return std::nullopt;
    }

    std::uint64_t hash = UINT64_C(14695981039346656037);
    std::array<char, 16 * 1024> buffer{};
    while (stream) {
        stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize bytes_read = stream.gcount();
        for (std::streamsize index = 0; index < bytes_read; ++index) {
            hash ^= static_cast<unsigned char>(buffer[static_cast<std::size_t>(index)]);
            hash *= UINT64_C(1099511628211);
        }
    }
    if (!stream.eof()) {
        return std::nullopt;
    }

    AssetFingerprint result;
    result.size_bytes = file_fingerprint->size_bytes;
    result.mtime_ns = file_fingerprint->mtime_ns;
    result.content_hash = hexValue(hash);
    return result;
}

std::string extensionForMime(const std::string& mime, const TagLib::ByteVector& bytes)
{
    const std::string normalized = lowerAscii(mime);
    if (normalized.find("png") != std::string::npos) {
        return ".png";
    }
    if (normalized.find("gif") != std::string::npos) {
        return ".gif";
    }
    if (normalized.find("bmp") != std::string::npos) {
        return ".bmp";
    }
    if (normalized.find("webp") != std::string::npos) {
        return ".webp";
    }
    if (normalized.find("jpeg") != std::string::npos || normalized.find("jpg") != std::string::npos) {
        return ".jpg";
    }

    const auto size = static_cast<std::size_t>(bytes.size());
    const auto* data = reinterpret_cast<const unsigned char*>(bytes.data());
    if (size >= 8 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') {
        return ".png";
    }
    if (size >= 3 && data[0] == 0xff && data[1] == 0xd8 && data[2] == 0xff) {
        return ".jpg";
    }
    if (size >= 6 && data[0] == 'G' && data[1] == 'I' && data[2] == 'F') {
        return ".gif";
    }
    if (size >= 2 && data[0] == 'B' && data[1] == 'M') {
        return ".bmp";
    }
    if (size >= 12 && data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[8] == 'W' && data[9] == 'E' &&
        data[10] == 'B' && data[11] == 'P') {
        return ".webp";
    }
    return ".bin";
}

bool isRenderableCoverExtension(const std::string& extension)
{
    const std::string normalized = lowerAscii(extension);
    return normalized == ".jpg" || normalized == ".jpeg" || normalized == ".png" || normalized == ".bmp";
}

std::optional<CoverData> pictureFromId3(TagLib::ID3v2::Tag* tag)
{
    if (!tag) {
        return std::nullopt;
    }

    TagLib::ID3v2::AttachedPictureFrame* first = nullptr;
    for (auto* frame : tag->frameList("APIC")) {
        auto* picture = dynamic_cast<TagLib::ID3v2::AttachedPictureFrame*>(frame);
        if (!picture || picture->picture().isEmpty()) {
            continue;
        }
        if (!first) {
            first = picture;
        }
        if (picture->type() == TagLib::ID3v2::AttachedPictureFrame::FrontCover) {
            first = picture;
            break;
        }
    }
    if (!first) {
        return std::nullopt;
    }

    CoverData result;
    result.bytes = first->picture();
    result.extension = extensionForMime(tagString(first->mimeType()), result.bytes);
    return result;
}

std::optional<CoverData> embeddedCover(TagLib::File* file)
{
    if (auto* mpeg = dynamic_cast<TagLib::MPEG::File*>(file)) {
        if (auto cover = pictureFromId3(mpeg->ID3v2Tag())) {
            return cover;
        }
    }

    if (auto* flac = dynamic_cast<TagLib::FLAC::File*>(file)) {
        TagLib::FLAC::Picture* selected = nullptr;
        for (auto* picture : flac->pictureList()) {
            if (!picture || picture->data().isEmpty()) {
                continue;
            }
            if (!selected) {
                selected = picture;
            }
            if (picture->type() == TagLib::FLAC::Picture::FrontCover) {
                selected = picture;
                break;
            }
        }
        if (selected) {
            CoverData result;
            result.bytes = selected->data();
            result.extension = extensionForMime(tagString(selected->mimeType()), result.bytes);
            return result;
        }
    }

    if (auto* mp4 = dynamic_cast<TagLib::MP4::File*>(file)) {
        auto* tag = mp4->tag();
        if (tag) {
            const auto items = tag->itemMap();
            if (items.contains("covr")) {
                const auto covers = items["covr"].toCoverArtList();
                if (!covers.isEmpty() && !covers.front().data().isEmpty()) {
                    CoverData result;
                    result.bytes = covers.front().data();
                    switch (covers.front().format()) {
                        case TagLib::MP4::CoverArt::JPEG:
                            result.extension = ".jpg";
                            break;
                        case TagLib::MP4::CoverArt::PNG:
                            result.extension = ".png";
                            break;
                        case TagLib::MP4::CoverArt::BMP:
                            result.extension = ".bmp";
                            break;
                        case TagLib::MP4::CoverArt::GIF:
                            result.extension = ".gif";
                            break;
                        default:
                            result.extension = extensionForMime({}, result.bytes);
                            break;
                    }
                    return result;
                }
            }
        }
    }

    if (auto* wav = dynamic_cast<TagLib::RIFF::WAV::File*>(file)) {
        if (auto cover = pictureFromId3(wav->ID3v2Tag())) {
            return cover;
        }
    }

    TagLib::VariantMap first_picture;
    for (const auto& picture : file->complexProperties("PICTURE")) {
        if (!picture.contains("data")) {
            continue;
        }
        bool data_ok = false;
        const TagLib::ByteVector bytes = picture.value("data").toByteVector(&data_ok);
        if (!data_ok || bytes.isEmpty()) {
            continue;
        }
        if (first_picture.isEmpty()) {
            first_picture = picture;
        }
        const std::string picture_type = tagString(picture.value("pictureType").toString());
        if (lowerAscii(picture_type).find("front") != std::string::npos) {
            first_picture = picture;
            break;
        }
    }
    if (!first_picture.isEmpty()) {
        bool data_ok = false;
        CoverData result;
        result.bytes = first_picture.value("data").toByteVector(&data_ok);
        if (data_ok && !result.bytes.isEmpty()) {
            result.extension = extensionForMime(tagString(first_picture.value("mimeType").toString()), result.bytes);
            return result;
        }
    }

    return std::nullopt;
}

bool writeBytesAtomically(const fs::path& destination, const TagLib::ByteVector& bytes)
{
    std::error_code error;
    fs::create_directories(destination.parent_path(), error);
    if (error) {
        spdlog::warn("Music library: cannot create cache directory '{}': {}", destination.parent_path().string(),
                     error.message());
        return false;
    }

    const fs::path temporary = destination.string() + ".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) {
            return false;
        }
        stream.write(bytes.data(), bytes.size());
        if (!stream) {
            stream.close();
            fs::remove(temporary, error);
            return false;
        }
    }

    fs::rename(temporary, destination, error);
    if (!error) {
        return true;
    }
    error.clear();
    fs::remove(destination, error);
    error.clear();
    fs::rename(temporary, destination, error);
    if (error) {
        spdlog::warn("Music library: cannot publish cache file '{}': {}", destination.string(), error.message());
        error.clear();
        fs::remove(temporary, error);
        return false;
    }
    return true;
}

bool writeTextAtomically(const fs::path& destination, const std::string& text)
{
    const TagLib::ByteVector bytes(text.data(), static_cast<unsigned int>(text.size()));
    return writeBytesAtomically(destination, bytes);
}

bool copyFileAtomically(const fs::path& source, const fs::path& destination)
{
    std::error_code error;
    if (fs::is_regular_file(destination, error) && !error) {
        return true;
    }

    error.clear();
    fs::create_directories(destination.parent_path(), error);
    if (error) {
        spdlog::warn("Music library: cannot create cache directory '{}': {}", destination.parent_path().string(),
                     error.message());
        return false;
    }

    const fs::path temporary = destination.string() + ".tmp";
    error.clear();
    fs::remove(temporary, error);
    error.clear();
    fs::copy_file(source, temporary, fs::copy_options::overwrite_existing, error);
    if (error) {
        spdlog::warn("Music library: cannot cache external artwork '{}': {}", source.string(), error.message());
        error.clear();
        fs::remove(temporary, error);
        return false;
    }

    error.clear();
    fs::rename(temporary, destination, error);
    if (!error) {
        return true;
    }
    error.clear();
    fs::remove(destination, error);
    error.clear();
    fs::rename(temporary, destination, error);
    if (error) {
        spdlog::warn("Music library: cannot publish external artwork cache '{}': {}", destination.string(),
                     error.message());
        error.clear();
        fs::remove(temporary, error);
        return false;
    }
    return true;
}

class SiblingFileCache {
public:
    fs::path find(const fs::path& audio_path, const std::string& target_filename)
    {
        const fs::path directory = normalizedPath(audio_path.parent_path());
        auto [iterator, inserted] = _directories.try_emplace(directory.string());
        if (inserted) {
            populate(directory, iterator->second);
        }
        const auto sibling = iterator->second.find(lowerAscii(target_filename));
        return sibling == iterator->second.end() ? fs::path{} : sibling->second;
    }

    std::optional<AssetFingerprint> fingerprintFor(const fs::path& path)
    {
        const fs::path normalized = normalizedPath(path);
        auto [iterator, inserted] = _fingerprints.try_emplace(normalized.string());
        if (inserted) {
            iterator->second = assetFingerprint(normalized);
        }
        return iterator->second;
    }

private:
    using DirectoryFiles = std::unordered_map<std::string, fs::path>;
    std::unordered_map<std::string, DirectoryFiles> _directories;
    std::unordered_map<std::string, std::optional<AssetFingerprint>> _fingerprints;

    static void populate(const fs::path& directory, DirectoryFiles& files)
    {
        std::error_code error;
        fs::directory_iterator iterator(directory, fs::directory_options::skip_permission_denied, error);
        for (const fs::directory_iterator end; !error && iterator != end; iterator.increment(error)) {
            if (iterator->is_regular_file(error) && !error) {
                files.emplace(lowerAscii(iterator->path().filename().string()), normalizedPath(iterator->path()));
            }
            error.clear();
        }
    }
};

fs::path externalCoverPath(const fs::path& audio_path, SiblingFileCache& siblings)
{
    static const std::vector<std::string> common_names = {
        "cover.jpg",  "cover.jpeg", "cover.png", "cover.bmp",  "folder.jpg", "folder.jpeg",
        "folder.png", "folder.bmp", "front.jpg", "front.jpeg", "front.png",  "front.bmp",
    };

    static const std::vector<std::string> image_extensions = {".jpg", ".jpeg", ".png", ".bmp"};
    for (const auto& extension : image_extensions) {
        const fs::path same_name = siblings.find(audio_path, audio_path.stem().string() + extension);
        if (!same_name.empty()) {
            return same_name;
        }
    }
    for (const auto& name : common_names) {
        const fs::path common = siblings.find(audio_path, name);
        if (!common.empty()) {
            return common;
        }
    }
    return {};
}

fs::path sidecarLyricsPath(const fs::path& audio_path, SiblingFileCache& siblings)
{
    return siblings.find(audio_path, audio_path.stem().string() + ".lrc");
}

AssociatedAssets associatedAssets(const fs::path& audio_path, SiblingFileCache& siblings)
{
    AssociatedAssets result;
    result.external_cover_path = externalCoverPath(audio_path, siblings);
    if (!result.external_cover_path.empty()) {
        result.external_cover_fingerprint = siblings.fingerprintFor(result.external_cover_path);
        if (!result.external_cover_fingerprint) {
            spdlog::warn("Music library: cannot fingerprint external artwork '{}'",
                         result.external_cover_path.string());
            result.external_cover_path.clear();
        }
    }

    result.sidecar_lyrics_path = sidecarLyricsPath(audio_path, siblings);
    if (!result.sidecar_lyrics_path.empty()) {
        result.sidecar_lyrics_fingerprint = siblings.fingerprintFor(result.sidecar_lyrics_path);
        if (!result.sidecar_lyrics_fingerprint) {
            spdlog::warn("Music library: cannot fingerprint lyrics sidecar '{}'", result.sidecar_lyrics_path.string());
            result.sidecar_lyrics_path.clear();
        }
    }
    return result;
}

bool assetMatches(const fs::path& stored_path, std::uint64_t stored_size, std::int64_t stored_mtime,
                  const std::string& stored_hash, const fs::path& current_path,
                  const std::optional<AssetFingerprint>& current_fingerprint)
{
    if (stored_path != current_path) {
        return false;
    }
    if (current_path.empty()) {
        return stored_size == 0 && stored_mtime == 0 && stored_hash.empty();
    }
    return current_fingerprint && stored_size == current_fingerprint->size_bytes &&
           stored_mtime == current_fingerprint->mtime_ns && stored_hash == current_fingerprint->content_hash;
}

bool associatedAssetsMatch(const Track& track, const AssociatedAssets& assets)
{
    return assetMatches(track.external_cover_path, track.external_cover_size_bytes, track.external_cover_mtime_ns,
                        track.external_cover_hash, assets.external_cover_path, assets.external_cover_fingerprint) &&
           assetMatches(track.sidecar_lyrics_path, track.sidecar_lyrics_size_bytes, track.sidecar_lyrics_mtime_ns,
                        track.sidecar_lyrics_hash, assets.sidecar_lyrics_path, assets.sidecar_lyrics_fingerprint);
}

fs::path cachedExternalCoverPath(const fs::path& source, const AssetFingerprint& asset, const fs::path& cache_dir)
{
    const std::string key = source.string() + '|' + asset.content_hash;
    const std::string extension = lowerAscii(source.extension().string());
    const fs::path destination = cache_dir / "artwork" / "external" / (hexHash(key) + extension);
    return copyFileAtomically(source, destination) ? destination : fs::path{};
}

fs::path cachedEmbeddedLyricsPath(const Track& track, const fs::path& cache_dir)
{
    const std::string key = track.path.string() + '|' + track.embedded_lyrics;
    return cache_dir / "lyrics" / "embedded" / (hexHash(key) + ".lrc");
}

fs::path cachedUserLyricsPath(const Track& track, const fs::path& cache_dir)
{
    return cache_dir / "lyrics" / "user" / (hexHash(track.path.string()) + ".lrc");
}

bool associatedAssetsNeedRefresh(const Track& track, const AssociatedAssets& assets, const fs::path& cache_dir)
{
    if (!associatedAssetsMatch(track, assets)) {
        return true;
    }

    fs::path expected_lyrics;
    if (!assets.sidecar_lyrics_path.empty()) {
        expected_lyrics = assets.sidecar_lyrics_path;
    } else if (!track.embedded_lyrics.empty()) {
        expected_lyrics = cachedEmbeddedLyricsPath(track, cache_dir);
    } else {
        const fs::path user_cache = cachedUserLyricsPath(track, cache_dir);
        if (fs::exists(user_cache) && fs::is_regular_file(user_cache)) {
            expected_lyrics = user_cache;
        }
    }
    if (track.lyrics_path != expected_lyrics) {
        return true;
    }

    if (track.embedded_cover_path.empty() && !assets.external_cover_path.empty()) {
        return track.cover_path.empty() || !fs::is_regular_file(track.cover_path);
    }
    return track.cover_path != track.embedded_cover_path;
}

void applyAssociatedAssets(Track& track, const AssociatedAssets& assets, const fs::path& cache_dir)
{
    track.external_cover_path.clear();
    track.external_cover_size_bytes = 0;
    track.external_cover_mtime_ns = 0;
    track.external_cover_hash.clear();
    track.cover_path = track.embedded_cover_path;
    if (!assets.external_cover_path.empty() && assets.external_cover_fingerprint) {
        track.external_cover_path = assets.external_cover_path;
        track.external_cover_size_bytes = assets.external_cover_fingerprint->size_bytes;
        track.external_cover_mtime_ns = assets.external_cover_fingerprint->mtime_ns;
        track.external_cover_hash = assets.external_cover_fingerprint->content_hash;
        if (track.embedded_cover_path.empty()) {
            const fs::path cached =
                cachedExternalCoverPath(track.external_cover_path, *assets.external_cover_fingerprint, cache_dir);
            track.cover_path = cached.empty() ? track.external_cover_path : cached;
        }
    }

    track.sidecar_lyrics_path.clear();
    track.sidecar_lyrics_size_bytes = 0;
    track.sidecar_lyrics_mtime_ns = 0;
    track.sidecar_lyrics_hash.clear();
    track.lyrics_path.clear();
    if (!assets.sidecar_lyrics_path.empty() && assets.sidecar_lyrics_fingerprint) {
        track.sidecar_lyrics_path = assets.sidecar_lyrics_path;
        track.sidecar_lyrics_size_bytes = assets.sidecar_lyrics_fingerprint->size_bytes;
        track.sidecar_lyrics_mtime_ns = assets.sidecar_lyrics_fingerprint->mtime_ns;
        track.sidecar_lyrics_hash = assets.sidecar_lyrics_fingerprint->content_hash;
        track.lyrics_path = track.sidecar_lyrics_path;
        return;
    }

    if (!track.embedded_lyrics.empty()) {
        const fs::path cached = cachedEmbeddedLyricsPath(track, cache_dir);
        if ((fs::exists(cached) && fs::is_regular_file(cached)) || writeTextAtomically(cached, track.embedded_lyrics)) {
            track.lyrics_path = cached;
            return;
        }
    }

    const fs::path user_cache = cachedUserLyricsPath(track, cache_dir);
    if (fs::exists(user_cache) && fs::is_regular_file(user_cache)) {
        track.lyrics_path = user_cache;
    }
}

std::optional<std::int64_t> miniaudioDuration(const fs::path& path)
{
    ma_decoder decoder{};
    const ma_decoder_config config =
        ma_decoder_config_init(ma_format_f32, kDecoderProbeChannels, kDecoderProbeSampleRate);
    const ma_result init_result = ma_decoder_init_file(path.string().c_str(), &config, &decoder);
    if (init_result != MA_SUCCESS) {
        spdlog::warn("Music library: miniaudio could not open '{}': {} ({})", path.string(),
                     ma_result_description(init_result), static_cast<int>(init_result));
        return std::nullopt;
    }

    ma_uint64 frame_count = 0;
    const ma_result length_result = ma_decoder_get_length_in_pcm_frames(&decoder, &frame_count);
    if (length_result != MA_SUCCESS) {
        spdlog::warn("Music library: miniaudio could not determine the duration for '{}': {} ({})", path.string(),
                     ma_result_description(length_result), static_cast<int>(length_result));
        ma_decoder_uninit(&decoder);
        return std::nullopt;
    }
    if (frame_count == 0) {
        spdlog::warn("Music library: miniaudio reported zero audio frames for '{}'", path.string());
        ma_decoder_uninit(&decoder);
        return std::nullopt;
    }

    std::array<float, kDecoderProbeChannels> probe_frame{};
    ma_uint64 frames_read = 0;
    const ma_result read_result = ma_decoder_read_pcm_frames(&decoder, probe_frame.data(), 1, &frames_read);
    ma_decoder_uninit(&decoder);
    if (frames_read != 1) {
        spdlog::warn("Music library: miniaudio opened '{}' but could not decode audio: {} ({}) frames={}",
                     path.string(), ma_result_description(read_result), static_cast<int>(read_result), frames_read);
        return std::nullopt;
    }

    const ma_uint64 whole_seconds = frame_count / kDecoderProbeSampleRate;
    if (whole_seconds > static_cast<ma_uint64>(std::numeric_limits<std::int64_t>::max() / 1000)) {
        spdlog::warn("Music library: miniaudio reported an implausible duration for '{}': {} frames", path.string(),
                     frame_count);
        return std::nullopt;
    }
    const ma_uint64 remainder = frame_count % kDecoderProbeSampleRate;
    const auto duration_ms =
        static_cast<std::int64_t>(whole_seconds * 1000 + remainder * 1000 / kDecoderProbeSampleRate);
    return std::max<std::int64_t>(1, duration_ms);
}

std::optional<Track> readTrack(const fs::path& path, const FileFingerprint& file_fingerprint, const fs::path& cache_dir,
                               const AssociatedAssets& assets)
{
    Track track;
    track.path = path;
    track.title = path.stem().string();
    track.size_bytes = file_fingerprint.size_bytes;
    track.mtime_ns = file_fingerprint.mtime_ns;

    TagLib::FileRef reference(path.c_str(), true, TagLib::AudioProperties::Fast);
    const bool taglib_valid = !reference.isNull() && reference.file() && reference.file()->isValid();
    const auto* taglib_audio = taglib_valid ? reference.audioProperties() : nullptr;
    if (taglib_audio && taglib_audio->lengthInMilliseconds() > 0) {
        track.duration_ms = taglib_audio->lengthInMilliseconds();
    } else {
        if (taglib_valid) {
            spdlog::warn("Music library: TagLib parsed '{}' but reported no valid duration; trying miniaudio",
                         path.string());
        } else {
            spdlog::warn("Music library: TagLib could not parse '{}'; trying miniaudio", path.string());
        }

        const auto fallback_duration = miniaudioDuration(path);
        if (!fallback_duration) {
            spdlog::warn(
                "Music library: excluding '{}' because both TagLib metadata parsing and miniaudio decoding "
                "failed; it will be retried on the next scan",
                path.string());
            return std::nullopt;
        }
        track.duration_ms = *fallback_duration;
        spdlog::info("Music library: miniaudio accepted '{}' with duration={} ms; using fallback metadata",
                     path.string(), track.duration_ms);
    }

    if (taglib_valid) {
        if (const auto* tag = reference.tag()) {
            const std::string title = tagString(tag->title());
            if (!title.empty()) {
                track.title = title;
            }
            track.artist = tagString(tag->artist());
            track.album = tagString(tag->album());
            track.genre = tagString(tag->genre());
            track.year = static_cast<int>(tag->year());
            track.track_number = static_cast<int>(tag->track());
        }

        const TagLib::PropertyMap properties = reference.file()->properties();
        track.album_artist = propertyValue(properties, {"ALBUMARTIST", "ALBUM ARTIST"});
        const int property_track = leadingInteger(propertyValue(properties, {"TRACKNUMBER", "TRACK"}));
        if (property_track > 0) {
            track.track_number = property_track;
        }
        track.disc_number = leadingInteger(propertyValue(properties, {"DISCNUMBER", "DISC"}));
        const int property_year = leadingInteger(propertyValue(properties, {"DATE", "YEAR"}));
        if (property_year > 0) {
            track.year = property_year;
        }
        track.embedded_lyrics = lyricsValue(properties);

        if (const auto cover = embeddedCover(reference.file()); cover && isRenderableCoverExtension(cover->extension)) {
            const std::string cache_key = path.string() + '|' + std::to_string(file_fingerprint.size_bytes) + '|' +
                                          std::to_string(file_fingerprint.mtime_ns);
            const fs::path destination = cache_dir / "artwork" / (hexHash(cache_key) + cover->extension);
            if ((fs::exists(destination) && fs::is_regular_file(destination)) ||
                writeBytesAtomically(destination, cover->bytes)) {
                track.embedded_cover_path = destination;
            }
        }
    }

    applyAssociatedAssets(track, assets, cache_dir);

    return track;
}

void createSchema(SQLite::Database& database)
{
    database.exec("PRAGMA journal_mode=WAL");
    database.exec("PRAGMA synchronous=NORMAL");
    database.exec("PRAGMA busy_timeout=3000");
    database.exec(R"sql(
        CREATE TABLE IF NOT EXISTS tracks (
            id INTEGER PRIMARY KEY,
            path TEXT NOT NULL UNIQUE,
            size_bytes INTEGER NOT NULL,
            mtime_ns INTEGER NOT NULL,
            title TEXT NOT NULL,
            artist TEXT NOT NULL DEFAULT '',
            album TEXT NOT NULL DEFAULT '',
            album_artist TEXT NOT NULL DEFAULT '',
            genre TEXT NOT NULL DEFAULT '',
            year INTEGER NOT NULL DEFAULT 0,
            track_number INTEGER NOT NULL DEFAULT 0,
            disc_number INTEGER NOT NULL DEFAULT 0,
            duration_ms INTEGER NOT NULL DEFAULT 0,
            cover_path TEXT NOT NULL DEFAULT '',
            embedded_cover_path TEXT NOT NULL DEFAULT '',
            external_cover_path TEXT NOT NULL DEFAULT '',
            external_cover_size_bytes INTEGER NOT NULL DEFAULT 0,
            external_cover_mtime_ns INTEGER NOT NULL DEFAULT 0,
            external_cover_hash TEXT NOT NULL DEFAULT '',
            lyrics_path TEXT NOT NULL DEFAULT '',
            sidecar_lyrics_path TEXT NOT NULL DEFAULT '',
            sidecar_lyrics_size_bytes INTEGER NOT NULL DEFAULT 0,
            sidecar_lyrics_mtime_ns INTEGER NOT NULL DEFAULT 0,
            sidecar_lyrics_hash TEXT NOT NULL DEFAULT '',
            embedded_lyrics TEXT NOT NULL DEFAULT ''
        )
    )sql");

    const auto ensure_column = [&database](const std::string& name, const std::string& declaration) {
        SQLite::Statement columns(database, "PRAGMA table_info(tracks)");
        while (columns.executeStep()) {
            if (columns.getColumn(1).getString() == name) {
                return;
            }
        }
        database.exec(("ALTER TABLE tracks ADD COLUMN " + name + ' ' + declaration).c_str());
    };
    ensure_column("external_cover_size_bytes", "INTEGER NOT NULL DEFAULT 0");
    ensure_column("external_cover_mtime_ns", "INTEGER NOT NULL DEFAULT 0");
    ensure_column("external_cover_hash", "TEXT NOT NULL DEFAULT ''");
    ensure_column("sidecar_lyrics_path", "TEXT NOT NULL DEFAULT ''");
    ensure_column("sidecar_lyrics_size_bytes", "INTEGER NOT NULL DEFAULT 0");
    ensure_column("sidecar_lyrics_mtime_ns", "INTEGER NOT NULL DEFAULT 0");
    ensure_column("sidecar_lyrics_hash", "TEXT NOT NULL DEFAULT ''");
    database.exec("CREATE INDEX IF NOT EXISTS tracks_album_index ON tracks(album, album_artist)");
    database.exec("CREATE INDEX IF NOT EXISTS tracks_path_fingerprint_index ON tracks(path, size_bytes, mtime_ns)");
    database.exec(("PRAGMA user_version=" + std::to_string(kSchemaVersion)).c_str());
}

std::unordered_map<std::string, Track> readIndexedTracks(SQLite::Database& database);

void bindTrack(SQLite::Statement& statement, const Track& track)
{
    statement.bind(1, track.path.string());
    statement.bind(2, static_cast<std::int64_t>(track.size_bytes));
    statement.bind(3, track.mtime_ns);
    statement.bind(4, track.title);
    statement.bind(5, track.artist);
    statement.bind(6, track.album);
    statement.bind(7, track.album_artist);
    statement.bind(8, track.genre);
    statement.bind(9, track.year);
    statement.bind(10, track.track_number);
    statement.bind(11, track.disc_number);
    statement.bind(12, track.duration_ms);
    statement.bind(13, track.cover_path.string());
    statement.bind(14, track.embedded_cover_path.string());
    statement.bind(15, track.external_cover_path.string());
    statement.bind(16, static_cast<std::int64_t>(track.external_cover_size_bytes));
    statement.bind(17, track.external_cover_mtime_ns);
    statement.bind(18, track.external_cover_hash);
    statement.bind(19, track.lyrics_path.string());
    statement.bind(20, track.sidecar_lyrics_path.string());
    statement.bind(21, static_cast<std::int64_t>(track.sidecar_lyrics_size_bytes));
    statement.bind(22, track.sidecar_lyrics_mtime_ns);
    statement.bind(23, track.sidecar_lyrics_hash);
    statement.bind(24, track.embedded_lyrics);
}

void upsertTracks(SQLite::Database& database, const std::vector<Track>& tracks,
                  const std::vector<std::string>& deleted_paths)
{
    SQLite::Transaction transaction(database);
    SQLite::Statement upsert(database, R"sql(
        INSERT INTO tracks (
            path, size_bytes, mtime_ns, title, artist, album, album_artist, genre, year,
            track_number, disc_number, duration_ms, cover_path, embedded_cover_path,
            external_cover_path, external_cover_size_bytes, external_cover_mtime_ns,
            external_cover_hash, lyrics_path, sidecar_lyrics_path, sidecar_lyrics_size_bytes,
            sidecar_lyrics_mtime_ns, sidecar_lyrics_hash, embedded_lyrics
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(path) DO UPDATE SET
            size_bytes=excluded.size_bytes,
            mtime_ns=excluded.mtime_ns,
            title=excluded.title,
            artist=excluded.artist,
            album=excluded.album,
            album_artist=excluded.album_artist,
            genre=excluded.genre,
            year=excluded.year,
            track_number=excluded.track_number,
            disc_number=excluded.disc_number,
            duration_ms=excluded.duration_ms,
            cover_path=excluded.cover_path,
            embedded_cover_path=excluded.embedded_cover_path,
            external_cover_path=excluded.external_cover_path,
            external_cover_size_bytes=excluded.external_cover_size_bytes,
            external_cover_mtime_ns=excluded.external_cover_mtime_ns,
            external_cover_hash=excluded.external_cover_hash,
            lyrics_path=excluded.lyrics_path,
            sidecar_lyrics_path=excluded.sidecar_lyrics_path,
            sidecar_lyrics_size_bytes=excluded.sidecar_lyrics_size_bytes,
            sidecar_lyrics_mtime_ns=excluded.sidecar_lyrics_mtime_ns,
            sidecar_lyrics_hash=excluded.sidecar_lyrics_hash,
            embedded_lyrics=excluded.embedded_lyrics
    )sql");
    for (const auto& track : tracks) {
        bindTrack(upsert, track);
        upsert.exec();
        upsert.reset();
        upsert.clearBindings();
    }

    SQLite::Statement remove(database, "DELETE FROM tracks WHERE path=?");
    for (const auto& path : deleted_paths) {
        remove.bind(1, path);
        remove.exec();
        remove.reset();
        remove.clearBindings();
    }
    transaction.commit();
}

Track trackFromQuery(SQLite::Statement& query)
{
    Track track;
    track.id = query.getColumn(0).getInt64();
    track.path = query.getColumn(1).getString();
    track.size_bytes = static_cast<std::uint64_t>(query.getColumn(2).getInt64());
    track.mtime_ns = query.getColumn(3).getInt64();
    track.title = query.getColumn(4).getString();
    track.artist = query.getColumn(5).getString();
    track.album = query.getColumn(6).getString();
    track.album_artist = query.getColumn(7).getString();
    track.genre = query.getColumn(8).getString();
    track.year = query.getColumn(9).getInt();
    track.track_number = query.getColumn(10).getInt();
    track.disc_number = query.getColumn(11).getInt();
    track.duration_ms = query.getColumn(12).getInt64();
    track.cover_path = query.getColumn(13).getString();
    track.embedded_cover_path = query.getColumn(14).getString();
    track.external_cover_path = query.getColumn(15).getString();
    track.external_cover_size_bytes = static_cast<std::uint64_t>(query.getColumn(16).getInt64());
    track.external_cover_mtime_ns = query.getColumn(17).getInt64();
    track.external_cover_hash = query.getColumn(18).getString();
    track.lyrics_path = query.getColumn(19).getString();
    track.sidecar_lyrics_path = query.getColumn(20).getString();
    track.sidecar_lyrics_size_bytes = static_cast<std::uint64_t>(query.getColumn(21).getInt64());
    track.sidecar_lyrics_mtime_ns = query.getColumn(22).getInt64();
    track.sidecar_lyrics_hash = query.getColumn(23).getString();
    track.embedded_lyrics = query.getColumn(24).getString();
    return track;
}

std::unordered_map<std::string, Track> readIndexedTracks(SQLite::Database& database)
{
    std::unordered_map<std::string, Track> result;
    SQLite::Statement query(database, R"sql(
        SELECT id, path, size_bytes, mtime_ns, title, artist, album, album_artist, genre,
               year, track_number, disc_number, duration_ms, cover_path, embedded_cover_path,
               external_cover_path, external_cover_size_bytes, external_cover_mtime_ns,
               external_cover_hash, lyrics_path, sidecar_lyrics_path, sidecar_lyrics_size_bytes,
               sidecar_lyrics_mtime_ns, sidecar_lyrics_hash, embedded_lyrics
        FROM tracks
    )sql");
    while (query.executeStep()) {
        Track track = trackFromQuery(query);
        result.emplace(track.path.string(), std::move(track));
    }
    return result;
}

void populateAlbums(LibrarySnapshot& snapshot, bool showing_examples)
{
    Album all_music = makeAllMusicAlbum();
    all_music.track_count = snapshot.tracks.size();

    std::map<std::string, Album> albums;
    for (const auto& track : snapshot.tracks) {
        all_music.duration_ms += track.duration_ms;
        if (all_music.cover_path.empty() && !track.cover_path.empty()) {
            all_music.cover_path = track.cover_path;
        }
        if (track.album.empty()) {
            continue;
        }

        const std::string artist = track.album_artist.empty() ? track.artist : track.album_artist;
        const std::string key = lowerAscii(track.album) + '\x1f' + lowerAscii(artist);
        auto [iterator, inserted] = albums.try_emplace(key);
        Album& album = iterator->second;
        if (inserted) {
            album.id = "album-" + hexHash(key);
            album.title = track.album;
            album.artist = artist;
        }
        ++album.track_count;
        album.duration_ms += track.duration_ms;
        if (album.cover_path.empty() && !track.cover_path.empty()) {
            album.cover_path = track.cover_path;
        }
    }

    const bool show_guides = showing_examples || albums.empty();
    snapshot.albums.reserve(albums.size() + 1 + (show_guides ? musicGuides().size() : 0));
    snapshot.albums.push_back(std::move(all_music));
    for (auto& entry : albums) {
        snapshot.albums.push_back(std::move(entry.second));
    }
    if (show_guides) {
        appendGuideAlbums(snapshot.albums);
    }
}

std::shared_ptr<LibrarySnapshot> loadSnapshot(SQLite::Database& database, std::uint64_t revision, bool allow_examples)
{
    auto snapshot = std::make_shared<LibrarySnapshot>();
    snapshot->revision = revision;

    SQLite::Statement query(database, R"sql(
        SELECT id, path, size_bytes, mtime_ns, title, artist, album, album_artist, genre,
               year, track_number, disc_number, duration_ms, cover_path, embedded_cover_path,
               external_cover_path, external_cover_size_bytes, external_cover_mtime_ns,
               external_cover_hash, lyrics_path, sidecar_lyrics_path, sidecar_lyrics_size_bytes,
               sidecar_lyrics_mtime_ns, sidecar_lyrics_hash, embedded_lyrics
        FROM tracks
        ORDER BY lower(album), disc_number, track_number, lower(title), lower(path)
    )sql");
    while (query.executeStep()) {
        snapshot->tracks.push_back(trackFromQuery(query));
    }

    const bool showing_examples =
        allow_examples && snapshot->tracks.empty() && appendExampleTracksIfAvailable(snapshot->tracks);
    populateAlbums(*snapshot, showing_examples);
    return snapshot;
}

bool isUnderDirectory(const fs::path& candidate, const fs::path& directory)
{
    const fs::path relative = candidate.lexically_relative(directory);
    if (relative.empty()) {
        return false;
    }
    const auto first = relative.begin();
    return first == relative.end() || *first != "..";
}

void cleanCacheDirectory(const fs::path& directory, const std::unordered_set<std::string>& referenced)
{
    std::error_code error;
    if (!fs::exists(directory, error)) {
        return;
    }

    fs::directory_iterator iterator(directory, fs::directory_options::skip_permission_denied, error);
    for (const fs::directory_iterator end; !error && iterator != end; iterator.increment(error)) {
        if (!iterator->is_regular_file(error)) {
            error.clear();
            continue;
        }
        const std::string path = normalizedPath(iterator->path()).string();
        if (referenced.count(path) != 0) {
            continue;
        }
        fs::remove(iterator->path(), error);
        if (error) {
            spdlog::warn("Music library: cannot remove stale cache file '{}': {}", iterator->path().string(),
                         error.message());
            error.clear();
        }
    }
}

void cleanCaches(const LibrarySnapshot& snapshot, const fs::path& cache_dir)
{
    std::unordered_set<std::string> embedded_artwork;
    std::unordered_set<std::string> external_artwork;
    std::unordered_set<std::string> embedded_lyrics;
    const fs::path artwork_dir = normalizedPath(cache_dir / "artwork");
    const fs::path external_artwork_dir = normalizedPath(artwork_dir / "external");
    const fs::path lyrics_dir = normalizedPath(cache_dir / "lyrics");
    const fs::path embedded_lyrics_dir = normalizedPath(lyrics_dir / "embedded");
    for (const auto& track : snapshot.tracks) {
        if (!track.embedded_cover_path.empty()) {
            const fs::path path = normalizedPath(track.embedded_cover_path);
            if (isUnderDirectory(path, artwork_dir)) {
                embedded_artwork.insert(path.string());
            }
        }
        if (track.embedded_cover_path.empty() && !track.cover_path.empty()) {
            const fs::path path = normalizedPath(track.cover_path);
            if (isUnderDirectory(path, external_artwork_dir)) {
                external_artwork.insert(path.string());
            }
        }
        if (!track.lyrics_path.empty()) {
            const fs::path path = normalizedPath(track.lyrics_path);
            if (isUnderDirectory(path, embedded_lyrics_dir)) {
                embedded_lyrics.insert(path.string());
            }
        }
    }
    cleanCacheDirectory(artwork_dir, embedded_artwork);
    cleanCacheDirectory(external_artwork_dir, external_artwork);
    cleanCacheDirectory(lyrics_dir, {});
    cleanCacheDirectory(embedded_lyrics_dir, embedded_lyrics);
}

}  // namespace

class MusicLibraryModel::Impl {
public:
    explicit Impl(LibraryConfig config) : _config(std::move(config))
    {
        auto initial = std::make_shared<LibrarySnapshot>();
        populateAlbums(*initial, false);
        std::atomic_store_explicit(&_snapshot, std::shared_ptr<const LibrarySnapshot>(std::move(initial)),
                                   std::memory_order_release);
    }

    ~Impl() { stop(); }

    void start()
    {
        std::lock_guard<std::mutex> lock(_control_mutex);
        if (_running) {
            return;
        }
        _stop_requested.store(false, std::memory_order_release);
        _scan_requested = true;
        _running = true;
        setScanState(ScanState{});
        _worker = std::thread([this] { workerMain(); });
    }

    void stop()
    {
        {
            std::lock_guard<std::mutex> lock(_control_mutex);
            if (!_running) {
                return;
            }
            _stop_requested.store(true, std::memory_order_release);
            _condition.notify_all();
        }
        if (_worker.joinable()) {
            _worker.join();
        }
        {
            std::lock_guard<std::mutex> lock(_control_mutex);
            _running = false;
        }
        ScanState stopped;
        stopped.phase = ScanPhase::Stopped;
        stopped.message = "Music library stopped";
        setScanState(std::move(stopped));
    }

    std::shared_ptr<const LibrarySnapshot> snapshot() const
    {
        return std::atomic_load_explicit(&_snapshot, std::memory_order_acquire);
    }

    ScanState scanState() const
    {
        std::lock_guard<std::mutex> lock(_state_mutex);
        return _scan_state;
    }

    void requestRescan()
    {
        std::lock_guard<std::mutex> lock(_control_mutex);
        _scan_requested = true;
        _condition.notify_all();
    }

private:
    LibraryConfig _config;
    mutable std::mutex _control_mutex;
    std::condition_variable _condition;
    std::thread _worker;
    bool _running = false;
    bool _scan_requested = false;
    std::atomic<bool> _stop_requested{false};

    mutable std::mutex _state_mutex;
    ScanState _scan_state;
    std::shared_ptr<const LibrarySnapshot> _snapshot;
    std::uint64_t _next_revision = 1;
    bool _database_snapshot_loaded = false;

    void setScanState(ScanState state)
    {
        std::lock_guard<std::mutex> lock(_state_mutex);
        _scan_state = std::move(state);
    }

    void updateProgress(ScanPhase phase, std::uint64_t discovered, std::uint64_t processed, std::uint64_t changed,
                        const std::string& message)
    {
        ScanState state;
        state.phase = phase;
        state.files_discovered = discovered;
        state.files_processed = processed;
        state.files_changed = changed;
        state.message = message;
        setScanState(std::move(state));
    }

    void workerMain()
    {
        for (;;) {
            {
                std::unique_lock<std::mutex> lock(_control_mutex);
                _condition.wait(lock,
                                [this] { return _stop_requested.load(std::memory_order_acquire) || _scan_requested; });
                if (_stop_requested.load(std::memory_order_acquire)) {
                    return;
                }
                _scan_requested = false;
            }

            try {
                scanOnce();
            } catch (const std::exception& exception) {
                ScanState failed;
                failed.phase = ScanPhase::Error;
                failed.message = exception.what();
                setScanState(std::move(failed));
                spdlog::error("Music library: scan failed: {}", exception.what());
            }
        }
    }

    void scanOnce()
    {
        ScanState opening;
        opening.phase = ScanPhase::OpeningDatabase;
        opening.message = "Opening music library";
        setScanState(opening);

        std::error_code error;
        if (!_config.database_path.has_parent_path()) {
            _config.database_path = fs::current_path() / _config.database_path;
        } else {
            fs::create_directories(_config.database_path.parent_path(), error);
            if (error) {
                throw std::runtime_error("Cannot create database directory: " + error.message());
            }
        }
        error.clear();
        fs::create_directories(_config.cache_dir, error);
        if (error) {
            throw std::runtime_error("Cannot create cache directory: " + error.message());
        }

        SQLite::Database database(_config.database_path.string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        createSchema(database);
        if (!_database_snapshot_loaded) {
            auto cached_snapshot = loadSnapshot(database, _next_revision++, false);
            std::atomic_store_explicit(&_snapshot, std::shared_ptr<const LibrarySnapshot>(cached_snapshot),
                                       std::memory_order_release);
            _database_snapshot_loaded = true;
        }
        const auto indexed = readIndexedTracks(database);

        std::vector<fs::path> configured_roots;
        configured_roots.reserve(_config.roots.size());
        for (const auto& configured_root : _config.roots) {
            const fs::path normalized = normalizedPath(configured_root);
            if (std::find(configured_roots.begin(), configured_roots.end(), normalized) == configured_roots.end()) {
                configured_roots.push_back(normalized);
            }
        }

        std::vector<RootScanStatus> root_scans;
        root_scans.reserve(configured_roots.size());
        std::unordered_map<std::string, FileFingerprint> discovered;
        updateProgress(ScanPhase::Discovering, 0, 0, 0, "Finding music files");
        for (const auto& root : configured_roots) {
            RootScanStatus& root_scan = root_scans.emplace_back(RootScanStatus{root, false});
            if (_stop_requested.load(std::memory_order_acquire)) {
                return;
            }
            error.clear();
            if (!fs::exists(root, error) || !fs::is_directory(root, error)) {
                spdlog::warn("Music library: scan root is unavailable: '{}'", root.string());
                continue;
            }

            bool complete = true;
            std::vector<fs::path> pending_directories{root};
            while (!pending_directories.empty()) {
                const fs::path directory = std::move(pending_directories.back());
                pending_directories.pop_back();

                error.clear();
                fs::directory_iterator iterator(directory, fs::directory_options::none, error);
                if (error) {
                    complete = false;
                    spdlog::warn("Music library: cannot scan directory '{}': {}", directory.string(), error.message());
                    continue;
                }

                const fs::directory_iterator end;
                while (iterator != end) {
                    if (_stop_requested.load(std::memory_order_acquire)) {
                        return;
                    }

                    const fs::directory_entry entry = *iterator;
                    std::error_code entry_error;
                    const fs::file_status status = entry.symlink_status(entry_error);
                    if (entry_error) {
                        complete = false;
                        spdlog::warn("Music library: cannot inspect '{}': {}", entry.path().string(),
                                     entry_error.message());
                    } else if (fs::is_directory(status)) {
                        pending_directories.push_back(entry.path());
                    } else {
                        bool regular_file = fs::is_regular_file(status);
                        if (fs::is_symlink(status) && isAudioPath(entry.path())) {
                            regular_file = entry.is_regular_file(entry_error);
                            if (entry_error) {
                                complete = false;
                                spdlog::warn("Music library: cannot follow audio link '{}': {}", entry.path().string(),
                                             entry_error.message());
                            }
                        }
                        if (regular_file && !entry_error && isAudioPath(entry.path())) {
                            const fs::path path = normalizedPath(entry.path());
                            if (const auto file_fingerprint = fingerprint(path)) {
                                discovered[path.string()] = *file_fingerprint;
                                if ((discovered.size() & 63U) == 0U) {
                                    updateProgress(ScanPhase::Discovering, discovered.size(), 0, 0,
                                                   "Finding music files");
                                }
                            } else {
                                complete = false;
                                spdlog::warn("Music library: cannot stat audio file '{}'", path.string());
                            }
                        }
                    }

                    error.clear();
                    iterator.increment(error);
                    if (error) {
                        complete = false;
                        spdlog::warn("Music library: directory scan interrupted in '{}': {}", directory.string(),
                                     error.message());
                        break;
                    }
                }
            }
            root_scan.complete = complete;
        }

        std::vector<std::pair<std::string, FileFingerprint>> ordered_files(discovered.begin(), discovered.end());
        std::sort(ordered_files.begin(), ordered_files.end(),
                  [](const auto& left, const auto& right) { return left.first < right.first; });

        std::vector<Track> changed_tracks;
        changed_tracks.reserve(ordered_files.size());
        SiblingFileCache sibling_files;
        std::uint64_t processed = 0;
        std::uint64_t read_failures = 0;
        updateProgress(ScanPhase::ReadingMetadata, ordered_files.size(), processed, 0, "Reading music metadata");
        for (const auto& entry : ordered_files) {
            if (_stop_requested.load(std::memory_order_acquire)) {
                return;
            }
            const auto existing = indexed.find(entry.first);
            const bool audio_unchanged = existing != indexed.end() &&
                                         existing->second.size_bytes == entry.second.size_bytes &&
                                         existing->second.mtime_ns == entry.second.mtime_ns;
            const AssociatedAssets assets = associatedAssets(entry.first, sibling_files);
            if (!audio_unchanged) {
                if (auto track = readTrack(entry.first, entry.second, _config.cache_dir, assets)) {
                    changed_tracks.push_back(std::move(*track));
                } else {
                    ++read_failures;
                }
            } else if (associatedAssetsNeedRefresh(existing->second, assets, _config.cache_dir)) {
                Track refreshed = existing->second;
                applyAssociatedAssets(refreshed, assets, _config.cache_dir);
                changed_tracks.push_back(std::move(refreshed));
            }
            ++processed;
            if ((processed & 15U) == 0U || processed == ordered_files.size()) {
                updateProgress(ScanPhase::ReadingMetadata, ordered_files.size(), processed, changed_tracks.size(),
                               "Reading music metadata");
            }
        }

        std::vector<std::string> deleted_paths;
        deleted_paths.reserve(indexed.size());
        for (const auto& entry : indexed) {
            if (discovered.count(entry.first) != 0) {
                continue;
            }
            const fs::path indexed_path(entry.first);
            const RootScanStatus* owner = nullptr;
            std::size_t owner_depth = 0;
            for (const auto& root_scan : root_scans) {
                if (!isUnderDirectory(indexed_path, root_scan.path)) {
                    continue;
                }
                const std::size_t depth =
                    static_cast<std::size_t>(std::distance(root_scan.path.begin(), root_scan.path.end()));
                if (!owner || depth > owner_depth) {
                    owner = &root_scan;
                    owner_depth = depth;
                }
            }
            if (!owner || owner->complete) {
                deleted_paths.push_back(entry.first);
            }
        }

        updateProgress(ScanPhase::Committing, ordered_files.size(), processed, changed_tracks.size(),
                       "Updating music library");
        upsertTracks(database, changed_tracks, deleted_paths);

        auto next_snapshot = loadSnapshot(database, _next_revision++, true);
        cleanCaches(*next_snapshot, _config.cache_dir);
        std::atomic_store_explicit(&_snapshot, std::shared_ptr<const LibrarySnapshot>(next_snapshot),
                                   std::memory_order_release);

        ScanState complete;
        complete.phase = ScanPhase::Complete;
        complete.files_discovered = ordered_files.size();
        complete.files_processed = processed;
        complete.files_changed = changed_tracks.size();
        complete.files_removed = deleted_paths.size();
        complete.files_failed = read_failures;
        complete.message =
            read_failures == 0 ? "Music library is ready" : std::to_string(read_failures) + " file(s) will be retried";
        setScanState(std::move(complete));
        spdlog::info("Music library: scan complete (tracks={}, changed={}, removed={}, retry={}, albums={})",
                     next_snapshot->tracks.size(), changed_tracks.size(), deleted_paths.size(), read_failures,
                     next_snapshot->albums.size());
    }
};

MusicLibraryModel::MusicLibraryModel(LibraryConfig config) : _impl(std::make_unique<Impl>(std::move(config))) {}

MusicLibraryModel::~MusicLibraryModel() = default;

void MusicLibraryModel::start() { _impl->start(); }

void MusicLibraryModel::stop() { _impl->stop(); }

std::shared_ptr<const LibrarySnapshot> MusicLibraryModel::snapshot() const { return _impl->snapshot(); }

ScanState MusicLibraryModel::scanState() const { return _impl->scanState(); }

void MusicLibraryModel::requestRescan() { _impl->requestRescan(); }

}  // namespace music
