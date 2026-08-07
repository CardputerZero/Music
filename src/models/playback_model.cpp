#include "models/playback_model.hpp"

#include "models/playback_queue.hpp"

#include <miniaudio.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>
#include <utility>
#include <vector>

namespace music {
namespace {

constexpr ma_uint32 kPlaybackChannels = 2;
constexpr ma_uint32 kPlaybackSampleRate = 48000;
constexpr ma_format kPlaybackFormat = ma_format_f32;
constexpr std::size_t kWaveformSamples = 192;
constexpr float kSpectrumAttack = 0.42f;
constexpr float kSpectrumRelease = 0.16f;

const char* maResultName(ma_result result)
{
    switch (result) {
        case MA_SUCCESS:
            return "MA_SUCCESS";
        case MA_NO_BACKEND:
            return "MA_NO_BACKEND";
        case MA_NO_DEVICE:
            return "MA_NO_DEVICE";
        case MA_DEVICE_NOT_INITIALIZED:
            return "MA_DEVICE_NOT_INITIALIZED";
        case MA_FAILED_TO_INIT_BACKEND:
            return "MA_FAILED_TO_INIT_BACKEND";
        case MA_FAILED_TO_OPEN_BACKEND_DEVICE:
            return "MA_FAILED_TO_OPEN_BACKEND_DEVICE";
        default:
            return "MA_ERROR";
    }
}

std::string playbackError(const char* operation, ma_result result)
{
    return std::string(operation) + " failed (" + maResultName(result) + ")";
}

}  // namespace

class PlaybackModel::Impl {
public:
    Impl()
    {
        for (auto& sample : _waveform) {
            sample.store(0.0f, std::memory_order_relaxed);
        }
    }

    ~Impl() { cleanup(); }

    void setQueue(std::vector<Track> tracks)
    {
        const std::int64_t current_id = _snapshot.track_id;
        const std::filesystem::path current_path = _snapshot.path;
        _queue = std::move(tracks);
        _queue_index = findQueueIndex(current_id, current_path);
        if (_queue_index == kNoQueueIndex && !_queue.empty() && !_snapshot.hasTrack()) {
            _queue_index = 0;
        }
    }

    bool play(const Track& track) { return playTrack(track, false); }

    bool playTrack(const Track& track, bool start_paused)
    {
        const Track next_track = track;
        synchronizeQueue(next_track);
        cleanupDecoder();
        resetWaveform();
        _snapshot = {};
        _snapshot.mode = _mode;
        _snapshot.track_id = next_track.id;
        _snapshot.path = next_track.path;
        _snapshot.title = next_track.title.empty() ? next_track.path.stem().string() : next_track.title;
        _snapshot.artist = next_track.artist;
        _snapshot.duration_ms = next_track.duration_ms;
        markRevised();

        ma_decoder_config decoder_config =
            ma_decoder_config_init(kPlaybackFormat, kPlaybackChannels, kPlaybackSampleRate);
        const ma_result decoder_result =
            ma_decoder_init_file(next_track.path.string().c_str(), &decoder_config, &_decoder);
        if (decoder_result != MA_SUCCESS) {
            setError(playbackError("Decoder", decoder_result));
            spdlog::error("Playback: cannot decode '{}': {} {}", next_track.path.string(),
                          static_cast<int>(decoder_result), maResultName(decoder_result));
            return false;
        }
        _decoder_initialized = true;

        ma_uint64 total_frames = 0;
        if (ma_decoder_get_length_in_pcm_frames(&_decoder, &total_frames) == MA_SUCCESS) {
            _total_frames = total_frames;
            _snapshot.duration_ms = static_cast<std::int64_t>(total_frames * 1000ULL / kPlaybackSampleRate);
        }

        if (!ensureDevice()) {
            cleanupDecoder();
            return false;
        }

        _finished.store(false, std::memory_order_release);
        if (start_paused) {
            _playing.store(false, std::memory_order_release);
            _snapshot.state = PlaybackState::Paused;
            markRevised();
            spdlog::info("Playback: loaded paused '{}' ({})", _snapshot.title, next_track.path.string());
            return true;
        }

        _playing.store(true, std::memory_order_release);
        const ma_result start_result = ma_device_start(&_device);
        if (start_result != MA_SUCCESS) {
            _playing.store(false, std::memory_order_release);
            setError(playbackError("Audio device", start_result));
            spdlog::error("Playback: cannot start device: {} {}", static_cast<int>(start_result),
                          maResultName(start_result));
            return false;
        }

        _snapshot.state = PlaybackState::Playing;
        markRevised();
        spdlog::info("Playback: playing '{}' ({})", _snapshot.title, next_track.path.string());
        return true;
    }

    bool toggle(const Track& track)
    {
        if (_snapshot.track_id != track.id || _snapshot.path != track.path) {
            return play(track);
        }
        if (_snapshot.state == PlaybackState::Playing) {
            pause();
            return true;
        }
        if (_snapshot.state != PlaybackState::Paused || !_decoder_initialized || !ensureDevice()) {
            return play(track);
        }

        _finished.store(false, std::memory_order_release);
        _playing.store(true, std::memory_order_release);
        const ma_result start_result = ma_device_start(&_device);
        if (start_result != MA_SUCCESS) {
            _playing.store(false, std::memory_order_release);
            setError(playbackError("Audio device", start_result));
            return false;
        }
        _snapshot.state = PlaybackState::Playing;
        markRevised();
        return true;
    }

    bool toggleCurrent()
    {
        const Track* track = queuedTrack();
        return track ? toggle(*track) : false;
    }

    bool previous() { return playRelative(-1, false); }

    bool next() { return playRelative(1, false); }

    void cycleMode()
    {
        switch (_mode) {
            case PlaybackMode::Sequential:
                _mode = PlaybackMode::Shuffle;
                break;
            case PlaybackMode::Shuffle:
                _mode = PlaybackMode::RepeatOne;
                break;
            case PlaybackMode::RepeatOne:
                _mode = PlaybackMode::Sequential;
                break;
        }
        _snapshot.mode = _mode;
        markRevised();
    }

    void pause()
    {
        if (_snapshot.state != PlaybackState::Playing) {
            return;
        }
        _playing.store(false, std::memory_order_release);
        stopDevice();
        _snapshot.state = PlaybackState::Paused;
        markRevised();
    }

    void stop()
    {
        _playing.store(false, std::memory_order_release);
        stopDevice();
        cleanupDecoder();
        resetWaveform();
        _snapshot = {};
        _snapshot.mode = _mode;
        _queue.clear();
        _queue_index = kNoQueueIndex;
        markRevised();
    }

    void update(float delta_seconds)
    {
        updatePosition();
        updateSpectrum(std::clamp(delta_seconds, 0.0f, 0.1f));
        if (_finished.exchange(false, std::memory_order_acq_rel)) {
            _playing.store(false, std::memory_order_release);
            stopDevice();
            if (!playRelative(1, true)) {
                _snapshot.state = PlaybackState::Stopped;
                _snapshot.position_ms = _snapshot.duration_ms;
                markRevised();
            }
        }
    }

    PlaybackSnapshot snapshot() const { return _snapshot; }

private:
    static constexpr std::size_t kNoQueueIndex = std::numeric_limits<std::size_t>::max();

    ma_context _context{};
    ma_device _device{};
    ma_decoder _decoder{};
    bool _context_initialized = false;
    bool _device_initialized = false;
    bool _decoder_initialized = false;
    std::mutex _decoder_mutex;
    std::atomic<bool> _playing{false};
    std::atomic<bool> _finished{false};
    ma_uint64 _total_frames = 0;
    std::array<std::atomic<float>, kWaveformSamples> _waveform;
    std::atomic<std::uint64_t> _waveform_cursor{0};
    PlaybackSnapshot _snapshot;
    std::vector<Track> _queue;
    std::size_t _queue_index = kNoQueueIndex;
    PlaybackMode _mode = PlaybackMode::Sequential;
    std::uint64_t _revision_counter = 0;
    std::uint32_t _random_state = 0x83d2e1a7U;

    void markRevised() { _snapshot.revision = ++_revision_counter; }

    std::size_t findQueueIndex(std::int64_t track_id, const std::filesystem::path& path) const
    {
        for (std::size_t index = 0; index < _queue.size(); ++index) {
            if (_queue[index].id == track_id && _queue[index].path == path) {
                return index;
            }
        }
        return kNoQueueIndex;
    }

    void synchronizeQueue(const Track& track)
    {
        _queue_index = findQueueIndex(track.id, track.path);
        if (_queue_index != kNoQueueIndex) {
            return;
        }
        _queue = {track};
        _queue_index = 0;
    }

    const Track* queuedTrack() const { return _queue_index < _queue.size() ? &_queue[_queue_index] : nullptr; }

    std::size_t randomQueueIndex()
    {
        _random_state = _random_state * 1664525U + 1013904223U;
        return _queue.empty() ? kNoQueueIndex : static_cast<std::size_t>(_random_state) % _queue.size();
    }

    bool playRelative(int offset, bool automatic)
    {
        if (_queue.empty()) {
            return false;
        }
        const bool keep_paused = !automatic && _snapshot.state == PlaybackState::Paused;
        if (_queue_index >= _queue.size()) {
            _queue_index = findQueueIndex(_snapshot.track_id, _snapshot.path);
        }
        if (_queue_index >= _queue.size()) {
            return false;
        }

        std::size_t next_index = _queue_index;
        if (automatic && _mode == PlaybackMode::RepeatOne) {
            return playTrack(_queue[next_index], false);
        }
        if (_mode == PlaybackMode::Shuffle && _queue.size() > 1) {
            do {
                next_index = randomQueueIndex();
            } while (next_index == _queue_index);
        } else {
            next_index = playback_detail::adjacentQueueIndex(_queue_index, _queue.size(), offset);
        }
        return playTrack(_queue[next_index], keep_paused);
    }

    bool ensureDevice()
    {
        if (_device_initialized) {
            return true;
        }
        if (!_context_initialized) {
            const ma_result context_result = ma_context_init(nullptr, 0, nullptr, &_context);
            if (context_result != MA_SUCCESS) {
                setError(playbackError("Audio context", context_result));
                return false;
            }
            _context_initialized = true;
        }

        ma_device_config config = ma_device_config_init(ma_device_type_playback);
        config.playback.format = kPlaybackFormat;
        config.playback.channels = kPlaybackChannels;
        config.sampleRate = kPlaybackSampleRate;
        config.dataCallback = dataCallback;
        config.pUserData = this;
        const ma_result device_result = ma_device_init(&_context, &config, &_device);
        if (device_result != MA_SUCCESS) {
            setError(playbackError("Audio device", device_result));
            return false;
        }
        _device_initialized = true;
        return true;
    }

    void setError(std::string message)
    {
        _snapshot.state = PlaybackState::Error;
        _snapshot.error = std::move(message);
        markRevised();
    }

    void cleanup()
    {
        _playing.store(false, std::memory_order_release);
        stopDevice();
        cleanupDecoder();
        if (_device_initialized) {
            ma_device_uninit(&_device);
            _device_initialized = false;
        }
        if (_context_initialized) {
            ma_context_uninit(&_context);
            _context_initialized = false;
        }
    }

    void cleanupDecoder()
    {
        stopDevice();
        std::lock_guard<std::mutex> lock(_decoder_mutex);
        if (_decoder_initialized) {
            ma_decoder_uninit(&_decoder);
            _decoder_initialized = false;
        }
        _total_frames = 0;
    }

    void stopDevice()
    {
        if (_device_initialized && ma_device_get_state(&_device) != ma_device_state_stopped) {
            ma_device_stop(&_device);
        }
    }

    void resetWaveform()
    {
        _waveform_cursor.store(0, std::memory_order_release);
        for (auto& sample : _waveform) {
            sample.store(0.0f, std::memory_order_relaxed);
        }
        _snapshot.spectrum.fill(0.0f);
    }

    void updatePosition()
    {
        if (!_decoder_initialized) {
            return;
        }
        std::lock_guard<std::mutex> lock(_decoder_mutex);
        ma_uint64 cursor = 0;
        if (ma_decoder_get_cursor_in_pcm_frames(&_decoder, &cursor) == MA_SUCCESS) {
            _snapshot.position_ms = static_cast<std::int64_t>(cursor * 1000ULL / kPlaybackSampleRate);
        }
    }

    void updateSpectrum(float delta_seconds)
    {
        std::array<float, kWaveformSamples> samples{};
        const std::uint64_t cursor = _waveform_cursor.load(std::memory_order_acquire);
        for (std::size_t index = 0; index < samples.size(); ++index) {
            const std::size_t source = static_cast<std::size_t>((cursor + index) % kWaveformSamples);
            samples[index] = _waveform[source].load(std::memory_order_relaxed);
        }

        constexpr std::size_t kSamplesPerBar = kWaveformSamples / std::tuple_size<decltype(_snapshot.spectrum)>::value;
        for (std::size_t bar = 0; bar < _snapshot.spectrum.size(); ++bar) {
            float energy = 0.0f;
            for (std::size_t index = 0; index < kSamplesPerBar; ++index) {
                const float sample = samples[bar * kSamplesPerBar + index];
                energy += sample * sample;
            }
            const float rms = std::sqrt(energy / static_cast<float>(kSamplesPerBar));
            const float target =
                _snapshot.state == PlaybackState::Playing ? std::clamp(rms * 3.2f, 0.06f, 1.0f) : 0.06f;
            const float response = target > _snapshot.spectrum[bar] ? kSpectrumAttack : kSpectrumRelease;
            const float frame_response = 1.0f - std::pow(1.0f - response, std::max(1.0f, delta_seconds * 60.0f));
            _snapshot.spectrum[bar] += (target - _snapshot.spectrum[bar]) * frame_response;
        }
    }

    static void dataCallback(ma_device* device, void* output, const void* input, ma_uint32 frame_count)
    {
        (void)input;
        auto* self = static_cast<Impl*>(device->pUserData);
        auto* out = static_cast<float*>(output);
        if (!self || !out || frame_count == 0) {
            return;
        }

        std::memset(out, 0, static_cast<std::size_t>(frame_count) * kPlaybackChannels * sizeof(float));
        if (!self->_playing.load(std::memory_order_acquire)) {
            return;
        }

        ma_uint64 frames_read = 0;
        {
            std::lock_guard<std::mutex> lock(self->_decoder_mutex);
            if (!self->_decoder_initialized) {
                return;
            }
            ma_decoder_read_pcm_frames(&self->_decoder, out, frame_count, &frames_read);
        }

        std::uint64_t cursor = self->_waveform_cursor.load(std::memory_order_relaxed);
        for (ma_uint64 frame = 0; frame < frames_read; ++frame) {
            const std::size_t offset = static_cast<std::size_t>(frame) * kPlaybackChannels;
            const float mono = (out[offset] + out[offset + 1]) * 0.5f;
            self->_waveform[static_cast<std::size_t>(cursor % kWaveformSamples)].store(mono, std::memory_order_relaxed);
            ++cursor;
        }
        self->_waveform_cursor.store(cursor, std::memory_order_release);

        if (frames_read < frame_count) {
            self->_playing.store(false, std::memory_order_release);
            self->_finished.store(true, std::memory_order_release);
        }
    }
};

PlaybackModel::PlaybackModel() : _impl(std::make_unique<Impl>()) {}

PlaybackModel::~PlaybackModel() = default;

void PlaybackModel::setQueue(std::vector<Track> tracks) { _impl->setQueue(std::move(tracks)); }

bool PlaybackModel::play(const Track& track) { return _impl->play(track); }

bool PlaybackModel::toggle(const Track& track) { return _impl->toggle(track); }

bool PlaybackModel::toggleCurrent() { return _impl->toggleCurrent(); }

bool PlaybackModel::previous() { return _impl->previous(); }

bool PlaybackModel::next() { return _impl->next(); }

void PlaybackModel::cycleMode() { _impl->cycleMode(); }

void PlaybackModel::pause() { _impl->pause(); }

void PlaybackModel::stop() { _impl->stop(); }

void PlaybackModel::update(float delta_seconds) { _impl->update(delta_seconds); }

PlaybackSnapshot PlaybackModel::snapshot() const { return _impl->snapshot(); }

}  // namespace music
