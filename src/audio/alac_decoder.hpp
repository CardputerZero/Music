#pragma once

// Apple Lossless (ALAC) frame decoder.
//
// A bounds-checked implementation of the ALAC bitstream as specified by Apple's
// reference codec (https://github.com/macosforge/alac, Apache-2.0): adaptive
// Golomb residuals, the adaptive FIR predictor and the stereo matrix. Every
// length read from the file is validated before it is used, so a malformed
// packet is rejected instead of overrunning a buffer.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace music::alac {

struct Config {
    std::uint32_t frame_length = 0;  // PCM frames per packet (usually 4096)
    std::uint8_t bit_depth = 0;      // 16, 20 or 24
    std::uint8_t pb = 0;
    std::uint8_t mb = 0;
    std::uint8_t kb = 0;
    std::uint8_t channels = 0;       // 1..8
    std::uint16_t max_run = 0;
    std::uint32_t sample_rate = 0;
};

// Parses the 24-byte ALACSpecificConfig (the payload of the MP4 'alac' box).
// Returns false for configurations this decoder does not support.
bool parseConfig(const std::uint8_t* data, std::size_t size, Config& out);

class Decoder {
public:
    bool init(const Config& config);
    const Config& config() const { return _config; }

    // Decodes one packet into interleaved samples, left-justified in 32 bits
    // (sample << (32 - bit_depth)). `out` must hold frame_length * channels
    // values. Returns the number of frames decoded, or -1 if the packet is
    // malformed.
    int decode(const std::uint8_t* packet, std::size_t size, std::int32_t* out);

private:
    Config _config{};
    std::vector<std::int32_t> _predictor;
    std::vector<std::int32_t> _mix_u;
    std::vector<std::int32_t> _mix_v;
    std::vector<std::uint16_t> _shift;
    std::vector<std::uint8_t> _padded;
};

}  // namespace music::alac
