// Apple Lossless (ALAC) frame decoder.
//
// Derived from the decoder in Apple's ALAC reference codec (ALACDecoder.cpp,
// ag_dec.c, dp_dec.c, matrix_dec.c), Copyright (c) 2011 Apple Inc., licensed
// under the Apache License, Version 2.0:
//     https://github.com/macosforge/alac
//     http://www.apache.org/licenses/LICENSE-2.0
// Modified: rewritten in C++17; every length and count read from the file is
// validated against the configured frame size and the packet size, signed
// overflow is replaced by explicit two's-complement wrapping, and 32-bit
// output is not supported.

#include "audio/alac_decoder.hpp"

#include <algorithm>
#include <cstring>

namespace music::alac {
namespace {

// Element tags (ALACAudioTypes.h).
enum : std::uint32_t { kSce = 0, kCpe = 1, kCce = 2, kLfe = 3, kDse = 4, kPce = 5, kFil = 6, kEnd = 7 };

// Adaptive Golomb constants (aglib.h).
constexpr std::uint32_t kQbShift = 9;
constexpr std::uint32_t kQb = 1u << kQbShift;
constexpr std::uint32_t kMmulShift = 2;
constexpr std::uint32_t kMdenShift = kQbShift - kMmulShift - 1;
constexpr std::uint32_t kMoff = 1u << (kMdenShift - 2);
constexpr std::uint32_t kBitOff = 24;
constexpr std::uint32_t kMaxPrefix = 9;
constexpr std::uint32_t kMeanClamp = 0xffff;
constexpr std::uint32_t kMaxFrameLength = 1u << 16;
constexpr std::size_t kPadding = 8;

std::uint32_t countLeadingZeros(std::uint32_t value)
{
    if (value == 0) return 32;
#if defined(__GNUC__) || defined(__clang__)
    return static_cast<std::uint32_t>(__builtin_clz(value));
#else
    std::uint32_t count = 0;
    while (!(value & 0x80000000u)) {
        value <<= 1;
        ++count;
    }
    return count;
#endif
}

// Sign-extends the low `bits` bits of `value` (1..32).
std::int32_t signExtend(std::uint32_t value, std::uint32_t bits)
{
    const std::uint32_t shift = 32 - bits;
    return static_cast<std::int32_t>(value << shift) >> shift;
}

std::int32_t signOf(std::int32_t value) { return (value > 0) - (value < 0); }

// The reference codec relies on 32-bit two's-complement wraparound; spell it out
// so that malformed input cannot trigger signed-overflow undefined behaviour.
std::int32_t wrap(std::int64_t value) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(value)); }

// MSB-first reader over a buffer followed by kPadding zero bytes. Reads past the
// end return zeros and mark the reader as overrun; callers check overrun() before
// trusting what they read, and every loop is bounded by the frame length.
class BitReader {
public:
    BitReader(const std::uint8_t* data, std::size_t size) : _data(data), _size_bits(size * 8) {}

    bool overrun() const { return _position > _size_bits; }
    bool atEnd() const { return _position >= _size_bits; }

    std::uint32_t peek32() const
    {
        if (_position > _size_bits) return 0;
        const std::uint8_t* p = _data + (_position >> 3);
        const std::uint64_t window = (std::uint64_t(p[0]) << 32) | (std::uint64_t(p[1]) << 24) |
                                     (std::uint64_t(p[2]) << 16) | (std::uint64_t(p[3]) << 8) | p[4];
        return static_cast<std::uint32_t>((window << (_position & 7)) >> 8);
    }

    std::uint32_t read(std::uint32_t count)
    {
        if (count == 0) return 0;
        const std::uint32_t value = peek32() >> (32 - count);
        skip(count);
        return value;
    }

    void skip(std::size_t count)
    {
        if (count > _size_bits - std::min(_position, _size_bits)) {
            _position = _size_bits + 1;
            return;
        }
        _position += count;
    }

    void alignToByte() { skip((8 - (_position & 7)) & 7); }

private:
    const std::uint8_t* _data;
    std::size_t _size_bits;
    std::size_t _position = 0;
};

// dyn_get_32bit(): one residual magnitude.
std::uint32_t readResidual(BitReader& bits, std::uint32_t m, std::uint32_t k, std::uint32_t max_bits)
{
    const std::uint32_t stream = bits.peek32();
    std::uint32_t prefix = countLeadingZeros(~stream);
    if (prefix >= kMaxPrefix) {
        bits.skip(kMaxPrefix);
        return bits.read(max_bits);
    }
    bits.skip(prefix + 1);
    if (k == 1) return prefix;
    const std::uint32_t value = (stream << (prefix + 1)) >> (32 - k);
    std::uint32_t result = prefix * m;
    if (value >= 2) {
        result += value - 1;
        bits.skip(k);
    } else {
        bits.skip(k - 1);
    }
    return result;
}

// dyn_get(): the length of a run of zero residuals.
std::uint32_t readRunLength(BitReader& bits, std::uint32_t m, std::uint32_t k)
{
    const std::uint32_t stream = bits.peek32();
    const std::uint32_t prefix = countLeadingZeros(~stream);
    if (prefix >= kMaxPrefix) {
        bits.skip(kMaxPrefix);
        return bits.read(16);
    }
    bits.skip(prefix + 1);
    const std::uint32_t value = (stream << (prefix + 1)) >> (32 - k);
    if (value >= 2) {
        bits.skip(k);
        return prefix * m + value - 1;
    }
    bits.skip(k - 1);
    return prefix * m;
}

// dyn_decomp(): adaptive Golomb-coded prediction residuals.
bool decodeResiduals(BitReader& bits, const Config& config, std::uint32_t pb_factor, std::int32_t* out,
                     std::uint32_t count, std::uint32_t chan_bits)
{
    const std::uint32_t pb = (config.pb * pb_factor) / 4;
    const std::uint32_t kb = config.kb;
    const std::uint32_t wb = (1u << kb) - 1;
    std::uint32_t mb = config.mb;
    std::uint32_t zmode = 0;
    std::uint32_t c = 0;
    while (c < count) {
        if (bits.atEnd()) return false;
        std::uint32_t k = 31 - countLeadingZeros((mb >> kQbShift) + 3);
        k = std::min(k, kb);
        const std::uint32_t m = (1u << k) - 1;
        const std::uint32_t n = readResidual(bits, m, k, chan_bits);
        if (bits.overrun()) return false;

        const std::uint32_t ndecode = n + zmode;
        const std::int64_t magnitude = (std::int64_t(ndecode) + 1) >> 1;
        out[c++] = wrap((ndecode & 1) ? -magnitude : magnitude);

        mb = pb * (n + zmode) + mb - ((pb * mb) >> kQbShift);
        if (n > kMeanClamp) mb = kMeanClamp;

        zmode = 0;
        if (((mb << kMmulShift) < kQb) && c < count) {
            zmode = 1;
            const std::uint32_t run_k = countLeadingZeros(mb) - kBitOff + ((mb + kMoff) >> kMdenShift);
            const std::uint32_t run_m = ((1u << run_k) - 1) & wb;
            const std::uint32_t run = readRunLength(bits, run_m, run_k);
            if (bits.overrun() || run > count - c) return false;
            std::fill(out + c, out + c + run, 0);
            c += run;
            if (run >= 65535) zmode = 0;
            mb = 0;
        }
    }
    return true;
}

// unpc_block(): undoes the adaptive FIR predictor. `pc` and `out` may alias only
// for num_active == 31, exactly as in the reference decoder.
void unpredict(const std::int32_t* pc, std::int32_t* out, std::uint32_t count, std::int16_t* coefs,
               std::uint32_t num_active, std::uint32_t chan_bits, std::uint32_t den_shift)
{
    out[0] = pc[0];
    if (num_active == 0) {
        if (count > 1 && pc != out) std::memcpy(out + 1, pc + 1, (count - 1) * sizeof(std::int32_t));
        return;
    }
    if (num_active == 31) {
        std::int32_t previous = out[0];
        for (std::uint32_t j = 1; j < count; ++j) {
            previous = signExtend(static_cast<std::uint32_t>(wrap(std::int64_t(pc[j]) + previous)), chan_bits);
            out[j] = previous;
        }
        return;
    }
    for (std::uint32_t j = 1; j <= num_active && j < count; ++j)
        out[j] = signExtend(static_cast<std::uint32_t>(wrap(std::int64_t(pc[j]) + out[j - 1])), chan_bits);

    const std::uint32_t lim = num_active + 1;
    const std::int32_t den_half = den_shift ? (1 << (den_shift - 1)) : 0;
    for (std::uint32_t j = lim; j < count; ++j) {
        const std::int32_t top = out[j - lim];
        const std::int32_t* recent = out + j - 1;
        std::uint32_t sum = 0;
        for (std::uint32_t k = 0; k < num_active; ++k)
            sum += static_cast<std::uint32_t>(std::int64_t(coefs[k]) * wrap(std::int64_t(recent[-std::int64_t(k)]) - top));
        std::int32_t del = pc[j];
        std::int32_t del0 = del;
        const std::int32_t sign = signOf(del);
        del = wrap(std::int64_t(del) + top + (wrap(std::int64_t(static_cast<std::int32_t>(sum)) + den_half) >> den_shift));
        out[j] = signExtend(static_cast<std::uint32_t>(del), chan_bits);

        if (sign > 0) {
            for (std::int32_t k = static_cast<std::int32_t>(num_active) - 1; k >= 0; --k) {
                const std::int32_t dd = wrap(std::int64_t(top) - recent[-k]);
                const std::int32_t s = signOf(dd);
                coefs[k] = static_cast<std::int16_t>(coefs[k] - s);
                del0 = wrap(std::int64_t(del0) - std::int64_t(num_active - k) * wrap((std::int64_t(s) * dd) >> den_shift));
                if (del0 <= 0) break;
            }
        } else if (sign < 0) {
            for (std::int32_t k = static_cast<std::int32_t>(num_active) - 1; k >= 0; --k) {
                const std::int32_t dd = wrap(std::int64_t(top) - recent[-k]);
                const std::int32_t s = signOf(dd);
                coefs[k] = static_cast<std::int16_t>(coefs[k] + s);
                del0 = wrap(std::int64_t(del0) - std::int64_t(num_active - k) * wrap((-std::int64_t(s) * dd) >> den_shift));
                if (del0 >= 0) break;
            }
        }
    }
}

struct ChannelParams {
    std::uint32_t mode = 0;
    std::uint32_t den_shift = 0;
    std::uint32_t pb_factor = 0;
    std::uint32_t num_coefs = 0;
    std::int16_t coefs[32] = {};
};

// Left-justifies a decoded sample in 32 bits, keeping the low bit_depth bits as
// the reference decoder's 16/20/24-bit writers do.
std::int32_t toOutput(std::int32_t value, std::uint32_t bit_depth)
{
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(value) << (32 - bit_depth));
}

}  // namespace

bool parseConfig(const std::uint8_t* data, std::size_t size, Config& out)
{
    if (!data || size < 24) return false;
    auto be32 = [&](std::size_t at) {
        return (std::uint32_t(data[at]) << 24) | (std::uint32_t(data[at + 1]) << 16) |
               (std::uint32_t(data[at + 2]) << 8) | data[at + 3];
    };
    Config config;
    config.frame_length = be32(0);
    const std::uint8_t compatible_version = data[4];
    config.bit_depth = data[5];
    config.pb = data[6];
    config.mb = data[7];
    config.kb = data[8];
    config.channels = data[9];
    config.max_run = static_cast<std::uint16_t>((data[10] << 8) | data[11]);
    config.sample_rate = be32(20);
    // 32-bit ALAC needs 33-bit intermediate samples; it is not produced by
    // iTunes/Music and is left unsupported rather than decoded wrongly.
    const bool supported_depth = config.bit_depth == 16 || config.bit_depth == 20 || config.bit_depth == 24;
    if (compatible_version != 0 || !supported_depth || config.channels < 1 || config.channels > 8 ||
        config.frame_length == 0 || config.frame_length > kMaxFrameLength || config.kb < 1 || config.kb > 22 ||
        config.sample_rate == 0 || config.sample_rate > 768000)
        return false;
    out = config;
    return true;
}

bool Decoder::init(const Config& config)
{
    if (config.frame_length == 0 || config.frame_length > kMaxFrameLength || config.channels == 0) return false;
    _config = config;
    _predictor.assign(config.frame_length, 0);
    _mix_u.assign(config.frame_length, 0);
    _mix_v.assign(config.frame_length, 0);
    _shift.assign(static_cast<std::size_t>(config.frame_length) * 2, 0);
    return true;
}

int Decoder::decode(const std::uint8_t* packet, std::size_t size, std::int32_t* out)
{
    const Config& config = _config;
    if (!packet || !out || size == 0 || _predictor.empty()) return -1;
    // Generous bound: an escape (uncompressed) packet of 8 channels x 24 bits.
    if (size > static_cast<std::size_t>(config.frame_length) * config.channels * 4 + 1024) return -1;
    _padded.assign(packet, packet + size);
    _padded.resize(size + kPadding, 0);
    BitReader bits(_padded.data(), size);

    const std::uint32_t channels = config.channels;
    const std::uint32_t depth = config.bit_depth;
    std::uint32_t channel = 0;
    std::int64_t frames = -1;

    while (channel < channels) {
        if (bits.atEnd()) return -1;
        const std::uint32_t tag = bits.read(3);
        if (tag == kEnd) break;
        if (tag == kCce || tag == kPce) return -1;
        if (tag == kFil) {
            std::uint32_t count = bits.read(4);
            if (count == 15) count += bits.read(8) - 1;
            bits.skip(std::size_t(count) * 8);
            if (bits.overrun()) return -1;
            continue;
        }
        if (tag == kDse) {
            bits.read(4);  // element instance tag
            const bool align = bits.read(1) != 0;
            std::uint32_t count = bits.read(8);
            if (count == 255) count += bits.read(8);
            if (align) bits.alignToByte();
            bits.skip(std::size_t(count) * 8);
            if (bits.overrun()) return -1;
            continue;
        }

        // kSce, kLfe (one channel) or kCpe (a channel pair).
        const std::uint32_t element_channels = tag == kCpe ? 2 : 1;
        if (channel + element_channels > channels) break;  // as the reference decoder: stop, zero the rest
        bits.read(4);                                      // element instance tag
        if (bits.read(12) != 0) return -1;
        const std::uint32_t header = bits.read(4);
        const bool partial = (header >> 3) != 0;
        std::uint32_t bytes_shifted = (header >> 1) & 3;
        const bool escape = (header & 1) != 0;
        if (bytes_shifted == 3) return -1;

        std::uint32_t count = config.frame_length;
        if (partial) count = bits.read(32);
        if (bits.overrun() || count == 0 || count > config.frame_length) return -1;
        if (frames >= 0 && count != frames) return -1;
        frames = count;

        std::int32_t* mix[2] = {_mix_u.data(), _mix_v.data()};
        std::uint32_t mix_bits = 0;
        std::int32_t mix_res = 0;
        if (!escape) {
            mix_bits = bits.read(8);
            mix_res = static_cast<std::int8_t>(bits.read(8));
            ChannelParams params[2];
            for (std::uint32_t c = 0; c < element_channels; ++c) {
                params[c].mode = bits.read(4);
                params[c].den_shift = bits.read(4);
                params[c].pb_factor = bits.read(3);
                params[c].num_coefs = bits.read(5);
                for (std::uint32_t i = 0; i < params[c].num_coefs; ++i)
                    params[c].coefs[i] = static_cast<std::int16_t>(bits.read(16));
            }
            if (bits.overrun()) return -1;
            const std::int32_t chan_bits = std::int32_t(depth) - std::int32_t(bytes_shifted * 8) + (tag == kCpe ? 1 : 0);
            if (chan_bits < 1 || chan_bits > 32) return -1;

            // The low "shifted off" bytes are stored ahead of the residuals.
            BitReader shift_bits = bits;
            if (bytes_shifted) bits.skip(std::size_t(bytes_shifted) * 8 * element_channels * count);
            if (bits.overrun()) return -1;

            for (std::uint32_t c = 0; c < element_channels; ++c) {
                if (!decodeResiduals(bits, config, params[c].pb_factor, _predictor.data(), count,
                                     static_cast<std::uint32_t>(chan_bits)))
                    return -1;
                if (params[c].mode != 0)
                    unpredict(_predictor.data(), _predictor.data(), count, nullptr, 31,
                              static_cast<std::uint32_t>(chan_bits), 0);
                unpredict(_predictor.data(), mix[c], count, params[c].coefs, params[c].num_coefs,
                          static_cast<std::uint32_t>(chan_bits), params[c].den_shift);
            }

            if (bytes_shifted) {
                const std::uint32_t shift = bytes_shifted * 8;
                for (std::uint32_t i = 0; i < count; ++i)
                    for (std::uint32_t c = 0; c < element_channels; ++c)
                        _shift[i * 2 + c] = static_cast<std::uint16_t>(shift_bits.read(shift));
                if (shift_bits.overrun()) return -1;
            }
        } else {
            // Uncompressed ("escape") frame: raw samples, interleaved within the element.
            for (std::uint32_t i = 0; i < count; ++i)
                for (std::uint32_t c = 0; c < element_channels; ++c)
                    mix[c][i] = signExtend(bits.read(depth), depth);
            if (bits.overrun()) return -1;
            bytes_shifted = 0;
        }

        if (tag == kCpe && mix_res != 0 && mix_bits >= 32) return -1;
        const std::uint32_t shift = bytes_shifted * 8;
        // Only the 24-bit writers restore shifted-off bytes in the reference decoder.
        const bool restore_shift = bytes_shifted != 0 && depth == 24;
        for (std::uint32_t i = 0; i < count; ++i) {
            std::int32_t value[2];
            if (tag == kCpe) {
                const std::int32_t u = mix[0][i];
                const std::int32_t v = mix[1][i];
                std::int32_t l = u;
                std::int32_t r = v;
                if (mix_res != 0) {
                    l = wrap(std::int64_t(u) + v - ((std::int64_t(mix_res) * v) >> mix_bits));
                    r = wrap(std::int64_t(l) - v);
                }
                value[0] = l;
                value[1] = r;
            } else {
                value[0] = mix[0][i];
            }
            for (std::uint32_t c = 0; c < element_channels; ++c) {
                std::int32_t sample = value[c];
                if (restore_shift)
                    sample = static_cast<std::int32_t>((static_cast<std::uint32_t>(sample) << shift) | _shift[i * 2 + c]);
                out[std::size_t(i) * channels + channel + c] = toOutput(sample, depth);
            }
        }
        channel += element_channels;
    }

    if (frames <= 0) return -1;
    for (; channel < channels; ++channel)
        for (std::int64_t i = 0; i < frames; ++i) out[std::size_t(i) * channels + channel] = 0;
    return static_cast<int>(frames);
}

}  // namespace music::alac
