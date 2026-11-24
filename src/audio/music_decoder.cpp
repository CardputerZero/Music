#include "audio/music_decoder.hpp"

#include "audio/alac_decoder.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <vector>

namespace music {
namespace {

// Reading an ALAC track out of an MP4/M4A container: just enough of ISO BMFF to
// find the ALAC sample description and the sample tables (stts/stsz/stsc/stco).

constexpr std::uint32_t fourcc(const char (&code)[5])
{
    return (std::uint32_t(std::uint8_t(code[0])) << 24) | (std::uint32_t(std::uint8_t(code[1])) << 16) |
           (std::uint32_t(std::uint8_t(code[2])) << 8) | std::uint32_t(std::uint8_t(code[3]));
}

constexpr std::uint64_t kMaxMoovBytes = 64ull << 20;   // sample tables of a multi-day recording
constexpr std::uint32_t kMaxPackets = 1u << 24;
constexpr std::uint32_t kMaxConsecutiveBadPackets = 16;

std::uint16_t be16(const std::uint8_t* p) { return static_cast<std::uint16_t>((p[0] << 8) | p[1]); }
std::uint32_t be32(const std::uint8_t* p)
{
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}
std::uint64_t be64(const std::uint8_t* p) { return (std::uint64_t(be32(p)) << 32) | be32(p + 4); }

struct Box {
    std::uint32_t type = 0;
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
};

// Walks the child boxes of an in-memory box payload.
class BoxIterator {
public:
    BoxIterator(const std::uint8_t* data, std::size_t size) : _p(data), _end(data + size) {}

    bool next(Box& box)
    {
        const std::size_t left = static_cast<std::size_t>(_end - _p);
        if (left < 8) return false;
        std::uint64_t size = be32(_p);
        std::size_t header = 8;
        if (size == 1) {
            if (left < 16) return false;
            size = be64(_p + 8);
            header = 16;
        } else if (size == 0) {
            size = left;
        }
        if (size < header || size > left) return false;
        box.type = be32(_p + 4);
        box.data = _p + header;
        box.size = static_cast<std::size_t>(size) - header;
        _p += size;
        return true;
    }

private:
    const std::uint8_t* _p;
    const std::uint8_t* _end;
};

bool findChild(const Box& parent, std::uint32_t type, Box& out)
{
    BoxIterator children(parent.data, parent.size);
    Box box;
    while (children.next(box)) {
        if (box.type == type) {
            out = box;
            return true;
        }
    }
    return false;
}

// Full-box payload after version/flags, with an entry count that fits the box.
bool tableEntries(const Box& box, std::size_t header, std::size_t entry_size, std::uint32_t& count,
                  const std::uint8_t*& entries)
{
    if (box.size < header) return false;
    count = be32(box.data + header - 4);
    entries = box.data + header;
    return count <= (box.size - header) / entry_size;
}

struct SampleToChunk {
    std::uint32_t first_chunk;  // 0-based
    std::uint32_t samples_per_chunk;
};

struct TimeRun {
    std::uint32_t count;
    std::uint32_t delta;
};

struct Track {
    alac::Config config;
    std::uint32_t timescale = 0;
    std::vector<TimeRun> times;
    std::vector<std::uint32_t> sizes;
    std::vector<std::uint64_t> offsets;  // per packet
    std::uint64_t length = 0;            // PCM frames
};

// Finds the ALACSpecificConfig inside an 'alac' sample entry. QuickTime sound
// description versions 1 and 2 add 16/36 bytes before the child boxes; fall back
// to scanning in case a muxer wrote something unusual.
bool alacConfigFromSampleEntry(const Box& entry, alac::Config& config)
{
    if (entry.size < 28) return false;
    const std::uint16_t version = be16(entry.data + 8);
    const std::size_t start = 28 + (version == 1 ? 16 : version == 2 ? 36 : 0);
    if (start <= entry.size) {
        Box children{0, entry.data + start, entry.size - start};
        Box box;
        if (findChild(children, fourcc("alac"), box) && box.size >= 28 &&
            alac::parseConfig(box.data + 4, box.size - 4, config))
            return true;
    }
    for (std::size_t at = 28; at + 36 <= entry.size; ++at) {
        if (be32(entry.data + at + 4) == fourcc("alac") && be32(entry.data + at) >= 36 &&
            alac::parseConfig(entry.data + at + 12, entry.size - at - 12, config))
            return true;
    }
    return false;
}

bool parseAlacTrack(const Box& trak, std::uint64_t file_size, Track& track)
{
    Box mdia, hdlr, mdhd, minf, stbl, stsd, stts, stsz, stsc, chunk_box;
    if (!findChild(trak, fourcc("mdia"), mdia) || !findChild(mdia, fourcc("hdlr"), hdlr) || hdlr.size < 12 ||
        be32(hdlr.data + 8) != fourcc("soun") || !findChild(mdia, fourcc("mdhd"), mdhd) ||
        !findChild(mdia, fourcc("minf"), minf) || !findChild(minf, fourcc("stbl"), stbl) ||
        !findChild(stbl, fourcc("stsd"), stsd) || !findChild(stbl, fourcc("stts"), stts) ||
        !findChild(stbl, fourcc("stsz"), stsz) || !findChild(stbl, fourcc("stsc"), stsc))
        return false;
    const bool co64 = findChild(stbl, fourcc("co64"), chunk_box);
    if (!co64 && !findChild(stbl, fourcc("stco"), chunk_box)) return false;

    if (mdhd.size < 24) return false;
    track.timescale = mdhd.data[0] == 1 ? (mdhd.size >= 32 ? be32(mdhd.data + 20) : 0) : be32(mdhd.data + 12);
    if (track.timescale == 0) return false;

    // Sample description: only ALAC is handled here; AAC and others fall through.
    if (stsd.size < 8 || be32(stsd.data + 4) < 1) return false;
    BoxIterator entries(stsd.data + 8, stsd.size - 8);
    Box entry;
    if (!entries.next(entry) || entry.type != fourcc("alac") || !alacConfigFromSampleEntry(entry, track.config))
        return false;

    std::uint32_t count = 0;
    const std::uint8_t* table = nullptr;

    // Packet durations.
    if (!tableEntries(stts, 8, 8, count, table)) return false;
    std::uint64_t packets_timed = 0;
    std::uint64_t total_time = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        const TimeRun run{be32(table + i * 8), be32(table + i * 8 + 4)};
        if (run.count == 0) continue;
        track.times.push_back(run);
        packets_timed += run.count;
        total_time += std::uint64_t(run.count) * run.delta;
    }

    // Packet sizes.
    if (stsz.size < 12) return false;
    const std::uint32_t constant_size = be32(stsz.data + 4);
    const std::uint32_t packets = be32(stsz.data + 8);
    if (packets == 0 || packets > kMaxPackets || packets != packets_timed) return false;
    if (constant_size == 0 && (stsz.size - 12) / 4 < packets) return false;
    track.sizes.resize(packets);
    for (std::uint32_t i = 0; i < packets; ++i)
        track.sizes[i] = constant_size ? constant_size : be32(stsz.data + 12 + i * 4);

    // Chunk offsets.
    std::uint32_t chunks = 0;
    const std::uint8_t* chunk_table = nullptr;
    if (!tableEntries(chunk_box, 8, co64 ? 8 : 4, chunks, chunk_table) || chunks == 0) return false;

    // Sample-to-chunk runs.
    if (!tableEntries(stsc, 8, 12, count, table) || count == 0) return false;
    std::vector<SampleToChunk> runs;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t first = be32(table + i * 12);
        const std::uint32_t per_chunk = be32(table + i * 12 + 4);
        if (first == 0 || per_chunk == 0 || (!runs.empty() && first - 1 <= runs.back().first_chunk)) return false;
        runs.push_back({first - 1, per_chunk});
    }
    if (runs.front().first_chunk != 0) return false;

    // Resolve every packet's file offset once, and keep only packets that lie
    // wholly inside the file, so a truncated download still plays what it has.
    track.offsets.reserve(packets);
    std::uint32_t packet = 0;
    for (std::size_t r = 0; r < runs.size() && packet < packets; ++r) {
        const std::uint32_t last_chunk = r + 1 < runs.size() ? runs[r + 1].first_chunk : chunks;
        for (std::uint32_t chunk = runs[r].first_chunk; chunk < last_chunk && chunk < chunks && packet < packets; ++chunk) {
            std::uint64_t offset = co64 ? be64(chunk_table + std::size_t(chunk) * 8) : be32(chunk_table + std::size_t(chunk) * 4);
            for (std::uint32_t i = 0; i < runs[r].samples_per_chunk && packet < packets; ++i, ++packet) {
                track.offsets.push_back(offset);
                offset += track.sizes[packet];
            }
        }
    }
    std::size_t playable = track.offsets.size();
    while (playable > 0 && (track.offsets[playable - 1] > file_size ||
                            track.sizes[playable - 1] > file_size - track.offsets[playable - 1]))
        --playable;
    if (playable == 0) return false;
    if (playable < packets) {
        // Drop the missing tail from the timing table too.
        std::uint64_t keep = playable;
        std::vector<TimeRun> kept;
        total_time = 0;
        for (const TimeRun& run : track.times) {
            if (keep == 0) break;
            const std::uint32_t n = static_cast<std::uint32_t>(std::min<std::uint64_t>(run.count, keep));
            kept.push_back({n, run.delta});
            total_time += std::uint64_t(n) * run.delta;
            keep -= n;
        }
        track.times = std::move(kept);
        track.sizes.resize(playable);
        track.offsets.resize(playable);
    }
    track.length = total_time * track.config.sample_rate / track.timescale;
    return track.length > 0;
}

bool readAlacTrack(std::FILE* file, Track& track)
{
    if (fseeko(file, 0, SEEK_END) != 0) return false;
    const auto end = ftello(file);
    if (end <= 0) return false;
    const std::uint64_t file_size = static_cast<std::uint64_t>(end);

    std::uint64_t position = 0;
    while (position + 8 <= file_size) {
        std::uint8_t header[16];
        if (fseeko(file, static_cast<off_t>(position), SEEK_SET) != 0 || std::fread(header, 1, 8, file) != 8)
            return false;
        std::uint64_t size = be32(header);
        std::uint64_t header_size = 8;
        if (size == 1) {
            if (std::fread(header + 8, 1, 8, file) != 8) return false;
            size = be64(header + 8);
            header_size = 16;
        } else if (size == 0) {
            size = file_size - position;
        }
        if (size < header_size || size > file_size - position) return false;
        if (be32(header + 4) == fourcc("moov")) {
            const std::uint64_t payload = size - header_size;
            if (payload > kMaxMoovBytes) return false;
            std::vector<std::uint8_t> moov(static_cast<std::size_t>(payload));
            if (std::fread(moov.data(), 1, moov.size(), file) != moov.size()) return false;
            BoxIterator children(moov.data(), moov.size());
            Box box;
            while (children.next(box)) {
                if (box.type == fourcc("trak") && parseAlacTrack(box, file_size, track)) return true;
            }
            return false;
        }
        position += size;
    }
    return false;
}

bool hasMp4AudioExtension(const char* path)
{
    const char* dot = std::strrchr(path, '.');
    if (!dot) return false;
    std::string extension(dot);
    for (char& c : extension) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
    return extension == ".m4a" || extension == ".m4b" || extension == ".mp4";
}

// Default ALAC channel layouts (ALACAudioTypes.h, ALACChannelLayoutTags).
void alacChannelMap(std::uint32_t channels, ma_channel* map)
{
    static const ma_channel kLayouts[8][8] = {
        {MA_CHANNEL_MONO},
        {MA_CHANNEL_FRONT_LEFT, MA_CHANNEL_FRONT_RIGHT},
        {MA_CHANNEL_FRONT_CENTER, MA_CHANNEL_FRONT_LEFT, MA_CHANNEL_FRONT_RIGHT},
        {MA_CHANNEL_FRONT_CENTER, MA_CHANNEL_FRONT_LEFT, MA_CHANNEL_FRONT_RIGHT, MA_CHANNEL_BACK_CENTER},
        {MA_CHANNEL_FRONT_CENTER, MA_CHANNEL_FRONT_LEFT, MA_CHANNEL_FRONT_RIGHT, MA_CHANNEL_BACK_LEFT,
         MA_CHANNEL_BACK_RIGHT},
        {MA_CHANNEL_FRONT_CENTER, MA_CHANNEL_FRONT_LEFT, MA_CHANNEL_FRONT_RIGHT, MA_CHANNEL_BACK_LEFT,
         MA_CHANNEL_BACK_RIGHT, MA_CHANNEL_LFE},
        {MA_CHANNEL_FRONT_CENTER, MA_CHANNEL_FRONT_LEFT, MA_CHANNEL_FRONT_RIGHT, MA_CHANNEL_BACK_LEFT,
         MA_CHANNEL_BACK_RIGHT, MA_CHANNEL_BACK_CENTER, MA_CHANNEL_LFE},
        {MA_CHANNEL_FRONT_CENTER, MA_CHANNEL_FRONT_LEFT_CENTER, MA_CHANNEL_FRONT_RIGHT_CENTER, MA_CHANNEL_FRONT_LEFT,
         MA_CHANNEL_FRONT_RIGHT, MA_CHANNEL_BACK_LEFT, MA_CHANNEL_BACK_RIGHT, MA_CHANNEL_LFE},
    };
    std::memcpy(map, kLayouts[channels - 1], channels * sizeof(ma_channel));
}

// miniaudio data source: decodes one packet at a time, so memory stays at a few
// packet-sized buffers plus the sample tables, independent of track length.
struct AlacSource {
    ma_data_source_base base;  // must be first
    std::FILE* file = nullptr;
    Track track;
    alac::Decoder codec;
    std::vector<std::uint8_t> packet;
    std::vector<std::int32_t> pcm;
    std::uint32_t next_packet = 0;
    std::uint32_t available = 0;  // decoded frames not yet returned
    std::uint32_t offset = 0;     // first unreturned frame in pcm
    std::uint32_t bad_packets = 0;
    ma_uint64 cursor = 0;

    ~AlacSource()
    {
        if (file) std::fclose(file);
        ma_data_source_uninit(&base);
    }

    // Frames that packet `index` covers according to the sample tables.
    std::uint64_t packetDuration(std::uint32_t index) const
    {
        for (const TimeRun& run : track.times) {
            if (index < run.count) return std::uint64_t(run.delta) * track.config.sample_rate / track.timescale;
            index -= run.count;
        }
        return 0;
    }

    // Decodes packet `next_packet` into pcm. A damaged packet becomes silence of
    // its nominal length, so one bad packet does not end the track; a run of them
    // (a corrupt or truncated file) does.
    ma_result decodeNext()
    {
        const std::uint32_t index = next_packet++;
        const std::uint32_t size = track.sizes[index];
        const std::uint32_t channels = track.config.channels;
        int frames = -1;
        if (size > 0 && size <= packet.size() &&
            fseeko(file, static_cast<off_t>(track.offsets[index]), SEEK_SET) == 0 &&
            std::fread(packet.data(), 1, size, file) == size)
            frames = codec.decode(packet.data(), size, pcm.data());
        if (frames < 0) {
            if (++bad_packets > kMaxConsecutiveBadPackets) return MA_INVALID_DATA;
            frames = static_cast<int>(std::min<std::uint64_t>(packetDuration(index), track.config.frame_length));
            std::fill(pcm.begin(), pcm.begin() + std::size_t(frames) * channels, 0);
        } else {
            bad_packets = 0;
        }
        offset = 0;
        available = static_cast<std::uint32_t>(frames);
        return MA_SUCCESS;
    }
};

ma_result sourceRead(ma_data_source* data_source, void* output, ma_uint64 frame_count, ma_uint64* frames_read)
{
    auto* source = reinterpret_cast<AlacSource*>(data_source);
    const std::uint32_t channels = source->track.config.channels;
    ma_uint64 total = 0;
    while (total < frame_count) {
        if (source->available == 0) {
            if (source->next_packet >= source->track.sizes.size()) break;
            const ma_result result = source->decodeNext();
            if (result != MA_SUCCESS) {
                if (frames_read) *frames_read = total;
                return total > 0 ? MA_SUCCESS : result;
            }
            continue;
        }
        const auto count = static_cast<std::uint32_t>(std::min<ma_uint64>(frame_count - total, source->available));
        if (output)
            std::memcpy(static_cast<std::int32_t*>(output) + total * channels,
                        source->pcm.data() + std::size_t(source->offset) * channels,
                        std::size_t(count) * channels * sizeof(std::int32_t));
        total += count;
        source->offset += count;
        source->available -= count;
        source->cursor += count;
    }
    if (frames_read) *frames_read = total;
    return total < frame_count ? MA_AT_END : MA_SUCCESS;
}

ma_result sourceSeek(ma_data_source* data_source, ma_uint64 frame)
{
    auto* source = reinterpret_cast<AlacSource*>(data_source);
    if (frame > source->track.length) return MA_BAD_SEEK;
    // Every ALAC packet decodes independently: jump to the packet that holds
    // `frame` and discard the frames before it.
    std::uint32_t index = 0;
    ma_uint64 start = 0;
    const auto packets = static_cast<std::uint32_t>(source->track.sizes.size());
    while (index < packets) {
        const std::uint64_t duration = source->packetDuration(index);
        if (frame < start + duration) break;
        start += duration;
        ++index;
    }
    source->next_packet = index;
    source->available = 0;
    source->offset = 0;
    source->bad_packets = 0;
    source->cursor = frame;
    if (index >= packets) return MA_SUCCESS;  // seek to the end
    const ma_result result = source->decodeNext();
    if (result != MA_SUCCESS) return result;
    const auto skip = static_cast<std::uint32_t>(std::min<ma_uint64>(frame - start, source->available));
    source->offset = skip;
    source->available -= skip;
    return MA_SUCCESS;
}

ma_result sourceFormat(ma_data_source* data_source, ma_format* format, ma_uint32* channels, ma_uint32* sample_rate,
                       ma_channel* channel_map, size_t channel_map_capacity)
{
    const auto& config = reinterpret_cast<AlacSource*>(data_source)->track.config;
    if (format) *format = ma_format_s32;
    if (channels) *channels = config.channels;
    if (sample_rate) *sample_rate = config.sample_rate;
    if (channel_map && channel_map_capacity >= config.channels) alacChannelMap(config.channels, channel_map);
    return MA_SUCCESS;
}

ma_result sourceCursor(ma_data_source* data_source, ma_uint64* cursor)
{
    *cursor = reinterpret_cast<AlacSource*>(data_source)->cursor;
    return MA_SUCCESS;
}

ma_result sourceLength(ma_data_source* data_source, ma_uint64* length)
{
    *length = reinterpret_cast<AlacSource*>(data_source)->track.length;
    return MA_SUCCESS;
}

const ma_data_source_vtable kSourceVtable = {sourceRead, sourceSeek, sourceFormat, sourceCursor, sourceLength,
                                             nullptr, 0};

ma_result backendInitFile(void*, const char* path, const ma_decoding_backend_config*,
                          const ma_allocation_callbacks*, ma_data_source** output)
{
    *output = nullptr;
    // Cheap rejection for FLAC/MP3/WAV, which miniaudio's built-in decoders handle.
    if (!path || !hasMp4AudioExtension(path)) return MA_INVALID_FILE;

    std::unique_ptr<AlacSource> source(new (std::nothrow) AlacSource());
    if (!source) return MA_OUT_OF_MEMORY;
    ma_data_source_config config = ma_data_source_config_init();
    config.vtable = &kSourceVtable;
    if (ma_data_source_init(&config, &source->base) != MA_SUCCESS) return MA_ERROR;

    source->file = std::fopen(path, "rb");
    if (!source->file) return MA_INVALID_FILE;
    if (!readAlacTrack(source->file, source->track) || !source->codec.init(source->track.config))
        return MA_INVALID_FILE;  // not ALAC (e.g. AAC) or not a usable MP4

    const auto& alac = source->track.config;
    const std::uint32_t largest = *std::max_element(source->track.sizes.begin(), source->track.sizes.end());
    const std::size_t packet_limit = std::size_t(alac.frame_length) * alac.channels * 4 + 1024;
    source->packet.resize(std::min<std::size_t>(largest, packet_limit));
    source->pcm.resize(std::size_t(alac.frame_length) * alac.channels);
    *output = &source.release()->base;
    return MA_SUCCESS;
}

void backendUninit(void*, ma_data_source* data_source, const ma_allocation_callbacks*)
{
    delete reinterpret_cast<AlacSource*>(data_source);
}

ma_decoding_backend_vtable kAlacBackend = {nullptr, backendInitFile, nullptr, nullptr, backendUninit};
ma_decoding_backend_vtable* kBackends[] = {&kAlacBackend};

}  // namespace

ma_decoder_config musicDecoderConfig(ma_format format, ma_uint32 channels, ma_uint32 sample_rate)
{
    ma_decoder_config config = ma_decoder_config_init(format, channels, sample_rate);
    config.ppCustomBackendVTables = kBackends;
    config.customBackendCount = 1;
    return config;
}

}  // namespace music
