// ALAC (.m4a) playback through the shared miniaudio decoder configuration.
//
// The fixtures in tests/fixtures/alac encode a deterministic integer signal
// (see generate.py). ALAC is lossless, so every decoded sample must equal
// signal_sample() exactly -- no reference PCM files and no tolerance.

#include "audio/alac_decoder.hpp"
#include "audio/music_decoder.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr std::int64_t kFrames = 9000;

void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

std::uint32_t mix32(std::uint32_t x)
{
    x ^= x >> 13;
    x *= 0x5BD1E995u;
    x ^= x >> 15;
    return x;
}

// Mirrors signal_sample() in tests/fixtures/alac/generate.py.
std::int64_t signalSample(std::int64_t i, std::int64_t c, int bits)
{
    if (i < 600 || (i >= 3000 && i < 3400)) return 0;
    if (i >= 3400 && i < 3800) return i % (5 + c) == 0 ? 1 + c : 0;
    const std::int64_t amplitude = (std::int64_t(1) << (bits - 1)) - 1;
    const std::uint32_t noise = mix32(static_cast<std::uint32_t>(i * 2654435761ll + c * 40503 + 12345));
    if (i >= kFrames * 3 / 4) return std::int64_t(noise % std::uint64_t(2 * amplitude + 1)) - amplitude;
    const std::int64_t period = 97 + 31 * c;
    const std::int64_t t = i % period;
    const std::int64_t ramp = t < period / 2 ? t : period - t;
    const std::int64_t tri = (ramp * 4 * amplitude) / (period * 2) - amplitude / 2;
    return tri + std::int64_t(noise % std::uint64_t(amplitude >> 6)) - (amplitude >> 7);
}

struct Fixture {
    const char* name;
    ma_uint32 sample_rate;
    ma_uint32 channels;
    int bits;
    // Source (WAV) channel for each decoded channel: ALAC orders 5.1 as C L R Ls Rs LFE.
    std::vector<int> source_channel;
};

const std::vector<Fixture> kFixtures = {
    {"ffmpeg-stereo-16.m4a", 44100, 2, 16, {0, 1}},
    {"ffmpeg-mono-16.m4a", 22050, 1, 16, {0}},
    {"ffmpeg-stereo-24.m4a", 96000, 2, 24, {0, 1}},
    {"ffmpeg-5.1-16.m4a", 48000, 6, 16, {2, 0, 1, 4, 5, 3}},
    {"apple-stereo-16.m4a", 44100, 2, 16, {0, 1}},
    {"apple-stereo-20.m4a", 48000, 2, 20, {0, 1}},
    {"apple-stereo-24.m4a", 96000, 2, 24, {0, 1}},
};

std::int32_t expected(const Fixture& fixture, std::int64_t frame, ma_uint32 channel)
{
    const auto value = signalSample(frame, fixture.source_channel[channel], fixture.bits);
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(value) << (32 - fixture.bits));
}

std::vector<std::uint8_t> readFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void writeFile(const fs::path& path, const std::vector<std::uint8_t>& bytes)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

// Native format (s32, source rate/channels): no miniaudio conversion in the way.
ma_decoder_config nativeConfig() { return music::musicDecoderConfig(ma_format_s32, 0, 0); }

void testBitExact(const fs::path& dir, const Fixture& fixture)
{
    const std::string label = fixture.name;
    ma_decoder decoder;
    const ma_decoder_config config = nativeConfig();
    const ma_result opened = ma_decoder_init_file((dir / fixture.name).string().c_str(), &config, &decoder);
    require(opened == MA_SUCCESS, label + ": open failed (" + std::to_string(opened) + ")");
    require(decoder.outputFormat == ma_format_s32 && decoder.outputChannels == fixture.channels &&
                decoder.outputSampleRate == fixture.sample_rate,
            label + ": unexpected native format");
    ma_uint64 length = 0;
    require(ma_decoder_get_length_in_pcm_frames(&decoder, &length) == MA_SUCCESS && length == ma_uint64(kFrames),
            label + ": length " + std::to_string(length));

    // Read the ALAC data source itself: ma_decoder would remap multichannel
    // layouts to its own default order, which is not what is being tested here.
    ma_data_source* source = decoder.pBackend;
    require(source != nullptr, label + ": no backend");
    ma_uint64 source_length = 0;
    require(ma_data_source_get_length_in_pcm_frames(source, &source_length) == MA_SUCCESS &&
                source_length == ma_uint64(kFrames),
            label + ": source length");

    // Odd chunk sizes so reads straddle packet boundaries.
    std::vector<std::int32_t> buffer(777 * fixture.channels);
    std::int64_t frame = 0;
    for (;;) {
        ma_uint64 got = 0;
        const ma_result result = ma_data_source_read_pcm_frames(source, buffer.data(), 777, &got);
        require(result == MA_SUCCESS || result == MA_AT_END, label + ": read error " + std::to_string(result));
        for (ma_uint64 f = 0; f < got; ++f, ++frame)
            for (ma_uint32 c = 0; c < fixture.channels; ++c)
                require(buffer[f * fixture.channels + c] == expected(fixture, frame, c),
                        label + ": sample mismatch at frame " + std::to_string(frame) + " channel " +
                            std::to_string(c));
        ma_uint64 cursor = 0;
        require(ma_data_source_get_cursor_in_pcm_frames(source, &cursor) == MA_SUCCESS && cursor == ma_uint64(frame),
                label + ": cursor");
        if (got == 0) {
            require(result == MA_AT_END, label + ": empty read without MA_AT_END");
            break;
        }
        require(frame <= kFrames, label + ": decoded past the end");
    }
    require(frame == kFrames, label + ": decoded " + std::to_string(frame) + " frames");

    // Seeks land on the exact sample, including packet boundaries and the end.
    for (std::int64_t target : {std::int64_t(0), std::int64_t(1), std::int64_t(4095), std::int64_t(4096),
                                std::int64_t(6750), std::int64_t(8191), std::int64_t(8192), kFrames - 1, kFrames}) {
        require(ma_data_source_seek_to_pcm_frame(source, ma_uint64(target)) == MA_SUCCESS,
                label + ": seek to " + std::to_string(target));
        ma_uint64 cursor = 0;
        require(ma_data_source_get_cursor_in_pcm_frames(source, &cursor) == MA_SUCCESS && cursor == ma_uint64(target),
                label + ": cursor after seek to " + std::to_string(target));
        ma_uint64 got = 0;
        ma_data_source_read_pcm_frames(source, buffer.data(), 300, &got);
        require(got == ma_uint64(std::min<std::int64_t>(300, kFrames - target)),
                label + ": read after seek to " + std::to_string(target));
        for (ma_uint64 f = 0; f < got; ++f)
            for (ma_uint32 c = 0; c < fixture.channels; ++c)
                require(buffer[f * fixture.channels + c] == expected(fixture, target + std::int64_t(f), c),
                        label + ": sample mismatch after seek to " + std::to_string(target));
    }
    require(ma_data_source_seek_to_pcm_frame(source, ma_uint64(kFrames + 1)) != MA_SUCCESS,
            label + ": seek past the end succeeded");
    ma_decoder_uninit(&decoder);
}

// The configuration PlaybackModel and the library probe use: f32 stereo 48 kHz.
void testPlaybackConfiguration(const fs::path& dir, const Fixture& fixture)
{
    const std::string label = std::string(fixture.name) + " (playback config)";
    ma_decoder decoder;
    const ma_decoder_config config = music::musicDecoderConfig(ma_format_f32, 2, 48000);
    require(ma_decoder_init_file((dir / fixture.name).string().c_str(), &config, &decoder) == MA_SUCCESS,
            label + ": open failed");
    ma_uint64 length = 0;
    require(ma_decoder_get_length_in_pcm_frames(&decoder, &length) == MA_SUCCESS, label + ": no length");
    const double expected_frames = double(kFrames) * 48000.0 / fixture.sample_rate;
    require(std::abs(double(length) - expected_frames) <= 2.0, label + ": resampled length " + std::to_string(length));
    std::vector<float> buffer(1024 * 2);
    ma_uint64 total = 0;
    double energy = 0;
    for (;;) {
        ma_uint64 got = 0;
        ma_decoder_read_pcm_frames(&decoder, buffer.data(), 1024, &got);
        for (ma_uint64 i = 0; i < got * 2; ++i) {
            require(std::isfinite(buffer[i]) && std::abs(buffer[i]) <= 2.0f, label + ": bad float sample");
            energy += double(buffer[i]) * buffer[i];
        }
        total += got;
        if (got == 0) break;
    }
    require(std::abs(double(total) - expected_frames) <= 64.0, label + ": decoded " + std::to_string(total));
    require(energy > 1.0, label + ": silent output");
    ma_decoder_uninit(&decoder);
}

void testRejectsNonAlac(const fs::path& dir, const fs::path& temporary)
{
    // AAC in an M4A container is not handled by the ALAC backend, and miniaudio
    // has no AAC decoder: opening must fail cleanly, exactly as before.
    ma_decoder decoder;
    ma_decoder_config config = music::musicDecoderConfig(ma_format_f32, 2, 48000);
    require(ma_decoder_init_file((dir / "apple-aac.m4a").string().c_str(), &config, &decoder) != MA_SUCCESS,
            "AAC M4A unexpectedly opened");

    // Not MP4 at all, under an .m4a name.
    const fs::path bogus = temporary / "bogus.m4a";
    writeFile(bogus, std::vector<std::uint8_t>(64, 'x'));
    require(ma_decoder_init_file(bogus.string().c_str(), &config, &decoder) != MA_SUCCESS, "garbage M4A opened");
    const fs::path empty = temporary / "empty.m4a";
    writeFile(empty, {});
    require(ma_decoder_init_file(empty.string().c_str(), &config, &decoder) != MA_SUCCESS, "empty M4A opened");
}

// The custom backend must not get in the way of miniaudio's built-in decoders.
void testBuiltinWavStillDecodes(const fs::path& temporary)
{
    const fs::path wav = temporary / "tone.wav";
    std::vector<std::uint8_t> bytes = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ',
                                       16, 0, 0, 0, 1, 0, 1, 0, 0x80, 0xBB, 0, 0, 0, 0x77, 1, 0, 2, 0, 16, 0,
                                       'd', 'a', 't', 'a', 0, 0, 0, 0};
    const std::uint32_t samples = 4800;
    for (std::uint32_t i = 0; i < samples; ++i) {
        const auto value = static_cast<std::int16_t>(signalSample(i, 0, 16));
        bytes.push_back(static_cast<std::uint8_t>(value & 0xFF));
        bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    }
    const std::uint32_t data_size = samples * 2;
    const std::uint32_t riff_size = 36 + data_size;
    for (int b = 0; b < 4; ++b) {
        bytes[4 + b] = static_cast<std::uint8_t>(riff_size >> (8 * b));
        bytes[40 + b] = static_cast<std::uint8_t>(data_size >> (8 * b));
    }
    writeFile(wav, bytes);
    ma_decoder decoder;
    const ma_decoder_config config = music::musicDecoderConfig(ma_format_s16, 1, 48000);
    require(ma_decoder_init_file(wav.string().c_str(), &config, &decoder) == MA_SUCCESS, "WAV no longer opens");
    std::vector<std::int16_t> out(samples);
    ma_uint64 got = 0;
    ma_decoder_read_pcm_frames(&decoder, out.data(), samples, &got);
    require(got == samples, "WAV frame count");
    for (std::uint32_t i = 0; i < samples; ++i)
        require(out[i] == static_cast<std::int16_t>(signalSample(i, 0, 16)), "WAV sample mismatch");
    ma_decoder_uninit(&decoder);
}

// A download cut off inside the audio data still plays the packets it has.
void testTruncatedFile(const fs::path& dir, const fs::path& temporary)
{
    const Fixture& fixture = kFixtures[4];  // apple-stereo-16: moov precedes mdat
    std::vector<std::uint8_t> bytes = readFile(dir / fixture.name);
    bytes.resize(bytes.size() / 2);
    const fs::path cut = temporary / "cut.m4a";
    writeFile(cut, bytes);
    ma_decoder decoder;
    const ma_decoder_config config = nativeConfig();
    require(ma_decoder_init_file(cut.string().c_str(), &config, &decoder) == MA_SUCCESS, "truncated file did not open");
    ma_uint64 length = 0;
    ma_decoder_get_length_in_pcm_frames(&decoder, &length);
    require(length > 0 && length < ma_uint64(kFrames) && length % 4096 == 0, "truncated length " + std::to_string(length));
    std::vector<std::int32_t> buffer(std::size_t(kFrames) * 2);
    ma_uint64 got = 0;
    ma_decoder_read_pcm_frames(&decoder, buffer.data(), ma_uint64(kFrames), &got);
    require(got == length, "truncated file decoded " + std::to_string(got));
    for (ma_uint64 f = 0; f < got; ++f)
        require(buffer[f * 2] == expected(fixture, std::int64_t(f), 0), "truncated file sample mismatch");
    ma_decoder_uninit(&decoder);
}

std::uint32_t nextRandom(std::uint32_t& state)
{
    state = state * 1664525u + 1013904223u;
    return mix32(state);
}

// Damaged files must never crash or hang: random byte corruption anywhere in
// the file (container tables and audio packets), then decode everything. Run
// under -fsanitize=address,undefined to check memory safety, not just survival.
void testCorruptionSurvival(const fs::path& dir, const fs::path& temporary, int iterations)
{
    std::uint32_t state = 0xC0FFEE;
    const fs::path damaged = temporary / "damaged.m4a";
    std::vector<std::int32_t> buffer(4096 * 8);
    int opened = 0;
    for (const Fixture& fixture : kFixtures) {
        const std::vector<std::uint8_t> original = readFile(dir / fixture.name);
        for (int iteration = 0; iteration < iterations; ++iteration) {
            std::vector<std::uint8_t> bytes = original;
            const std::uint32_t flips = 1 + nextRandom(state) % 16;
            for (std::uint32_t f = 0; f < flips; ++f)
                bytes[nextRandom(state) % bytes.size()] = static_cast<std::uint8_t>(nextRandom(state));
            if (nextRandom(state) % 8 == 0) bytes.resize(nextRandom(state) % bytes.size());
            writeFile(damaged, bytes);

            ma_decoder decoder;
            const ma_decoder_config config = nativeConfig();
            if (ma_decoder_init_file(damaged.string().c_str(), &config, &decoder) != MA_SUCCESS) continue;
            ++opened;
            const ma_uint32 channels = decoder.outputChannels;
            require(channels >= 1 && channels <= 8, "corrupt file reported " + std::to_string(channels) + " channels");
            ma_uint64 length = 0;
            ma_decoder_get_length_in_pcm_frames(&decoder, &length);
            const ma_uint64 chunk = 4096;
            for (int reads = 0; reads < 100000; ++reads) {
                ma_uint64 got = 0;
                ma_decoder_read_pcm_frames(&decoder, buffer.data(), chunk, &got);
                if (got == 0) break;
                if (nextRandom(state) % 16 == 0) ma_decoder_seek_to_pcm_frame(&decoder, nextRandom(state) % (length + 2));
            }
            ma_decoder_uninit(&decoder);
        }
    }
    require(opened > 0, "no corrupted file opened; the corruption test exercised nothing");

    // Raw packets straight into the frame decoder, for every configuration shape.
    for (const Fixture& fixture : kFixtures) {
        music::alac::Config config;
        config.frame_length = 4096;
        config.bit_depth = static_cast<std::uint8_t>(fixture.bits);
        config.pb = 40;
        config.mb = 10;
        config.kb = 14;
        config.channels = static_cast<std::uint8_t>(fixture.channels);
        config.max_run = 255;
        config.sample_rate = fixture.sample_rate;
        music::alac::Decoder codec;
        require(codec.init(config), "frame decoder init");
        std::vector<std::uint8_t> packet;
        for (int iteration = 0; iteration < iterations * 4; ++iteration) {
            packet.resize(1 + nextRandom(state) % 4096);
            for (auto& byte : packet) byte = static_cast<std::uint8_t>(nextRandom(state));
            const int frames = codec.decode(packet.data(), packet.size(), buffer.data());
            require(frames <= 4096, "frame decoder returned too many frames");
        }
    }
}

// MSB-first bit writer for hand-built ALAC packets.
struct BitWriter {
    std::vector<std::uint8_t> bytes;
    std::size_t bits = 0;
    void put(std::uint32_t value, int count)
    {
        for (int b = count - 1; b >= 0; --b, ++bits) {
            if (bits % 8 == 0) bytes.push_back(0);
            if ((value >> b) & 1) bytes.back() |= static_cast<std::uint8_t>(0x80 >> (bits % 8));
        }
    }
};

// An uncompressed ("escape") channel-pair packet: tag, instance, 12 zero bits,
// partial|shift|escape flags, frame count, raw interleaved samples, END.
std::vector<std::uint8_t> escapePairPacket(const std::vector<std::int16_t>& left, const std::vector<std::int16_t>& right)
{
    BitWriter w;
    w.put(1, 3);   // ID_CPE
    w.put(0, 4);   // element instance
    w.put(0, 12);  // unused
    w.put(0b1001, 4);  // partial frame, no shift, escape
    w.put(static_cast<std::uint32_t>(left.size()), 32);
    for (std::size_t i = 0; i < left.size(); ++i) {
        w.put(static_cast<std::uint16_t>(left[i]), 16);
        w.put(static_cast<std::uint16_t>(right[i]), 16);
    }
    w.put(7, 3);  // ID_END
    return w.bytes;
}

void testCraftedPackets()
{
    music::alac::Config config;
    config.frame_length = 4;
    config.bit_depth = 16;
    config.pb = 40;
    config.mb = 10;
    config.kb = 14;
    config.channels = 2;
    config.max_run = 255;
    config.sample_rate = 44100;
    const std::vector<std::int16_t> left = {1, -2, 32767};
    const std::vector<std::int16_t> right = {-32768, 5, 0};
    const auto packet = escapePairPacket(left, right);

    music::alac::Decoder stereo;
    require(stereo.init(config), "crafted: init");
    std::vector<std::int32_t> out(8, 0x5A5A5A5A);
    require(stereo.decode(packet.data(), packet.size(), out.data()) == 3, "crafted: frame count");
    for (std::size_t i = 0; i < 3; ++i)
        require(out[i * 2] == left[i] * 65536 && out[i * 2 + 1] == right[i] * 65536, "crafted: escape samples");
    require(out[6] == 0x5A5A5A5A, "crafted: wrote past the decoded frames");

    // A channel pair in a mono stream must not be written past the mono buffer.
    config.channels = 1;
    music::alac::Decoder mono;
    require(mono.init(config), "crafted: mono init");
    const auto full = escapePairPacket({1, 2, 3, 4}, {5, 6, 7, 8});  // a whole 4-frame packet
    std::vector<std::int32_t> guarded(4 + 4, 0x5A5A5A5A);
    mono.decode(full.data(), full.size(), guarded.data());
    for (std::size_t i = 4; i < guarded.size(); ++i)
        require(guarded[i] == 0x5A5A5A5A, "crafted: pair element overflowed a mono output buffer");

    // A partial-frame count above the configured frame length is rejected.
    config.channels = 2;
    config.frame_length = 2;
    music::alac::Decoder short_frames;
    require(short_frames.init(config), "crafted: short init");
    std::vector<std::int32_t> small(2 * 2 + 4, 0x5A5A5A5A);
    require(short_frames.decode(packet.data(), packet.size(), small.data()) == -1, "crafted: oversized frame count");
    for (std::size_t i = 4; i < small.size(); ++i)
        require(small[i] == 0x5A5A5A5A, "crafted: oversized frame count wrote past the buffer");
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cerr << "usage: music_decoder_tests <tests/fixtures/alac> [corruption iterations]\n";
        return 2;
    }
    const fs::path dir = argv[1];
    const int iterations = argc > 2 ? std::max(1, std::atoi(argv[2])) : 200;
    const fs::path temporary =
        fs::temp_directory_path() /
        ("music-decoder-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(temporary);
    try {
        for (const Fixture& fixture : kFixtures) {
            testBitExact(dir, fixture);
            testPlaybackConfiguration(dir, fixture);
        }
        testCraftedPackets();
        testRejectsNonAlac(dir, temporary);
        testBuiltinWavStillDecodes(temporary);
        testTruncatedFile(dir, temporary);
        testCorruptionSurvival(dir, temporary, iterations);
    } catch (const std::exception& error) {
        std::cerr << "music_decoder_test: FAIL: " << error.what() << '\n';
        std::error_code ignored;
        fs::remove_all(temporary, ignored);
        return 1;
    }
    std::error_code ignored;
    fs::remove_all(temporary, ignored);
    std::cout << "music_decoder_test: PASS\n";
    return 0;
}
