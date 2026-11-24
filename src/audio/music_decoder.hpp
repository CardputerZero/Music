#pragma once

#include <miniaudio.h>

namespace music {

// miniaudio decoder configuration shared by playback and the library's duration
// probe. On top of miniaudio's built-in FLAC/MP3/WAV decoders it registers a
// backend for Apple Lossless (ALAC) in MP4 containers (.m4a/.m4b/.mp4). Other
// MP4 audio, such as AAC, is still rejected as before.
ma_decoder_config musicDecoderConfig(ma_format format, ma_uint32 channels, ma_uint32 sample_rate);

}  // namespace music
