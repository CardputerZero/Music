#include "input/music_keypad.hpp"

#include "input/music_keys.hpp"

#include <spdlog/spdlog.h>
#include <utility>

#if !MUSIC_USE_SDL && defined(__linux__)
#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace music {

#if !MUSIC_USE_SDL && defined(__linux__)
namespace {

template <std::size_t N>
bool testBit(const std::array<unsigned long, N>& bits, unsigned int bit)
{
    constexpr unsigned int kBitsPerWord = sizeof(unsigned long) * 8;
    const unsigned int index = bit / kBitsPerWord;
    const unsigned int offset = bit % kBitsPerWord;
    return index < bits.size() && ((bits[index] >> offset) & 1UL) != 0;
}

bool hasMusicKeys(int fd)
{
    constexpr std::size_t kKeyBitsSize = (KEY_MAX + sizeof(unsigned long) * 8) / (sizeof(unsigned long) * 8);
    std::array<unsigned long, kKeyBitsSize> key_bits{};
    if (::ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits.data()) < 0) {
        return false;
    }
    return testBit(key_bits, KEY_ESC) || testBit(key_bits, KEY_ENTER) || testBit(key_bits, KEY_LEFT) ||
           testBit(key_bits, KEY_RIGHT) || testBit(key_bits, KEY_F) || testBit(key_bits, KEY_X) ||
           testBit(key_bits, KEY_Z) || testBit(key_bits, KEY_C) || testBit(key_bits, KEY_SPACE) ||
           testBit(key_bits, KEY_PLAYPAUSE) || testBit(key_bits, KEY_PREVIOUSSONG) || testBit(key_bits, KEY_NEXTSONG) ||
           testBit(key_bits, KEY_REWIND) || testBit(key_bits, KEY_FASTFORWARD) || testBit(key_bits, KEY_HELP);
}

}  // namespace
#endif

MusicKeypad::~MusicKeypad() { close(); }

bool MusicKeypad::openDefault()
{
#if !MUSIC_USE_SDL && defined(__linux__)
    if (const char* path = std::getenv("MUSIC_KEYBOARD_DEVICE")) {
        return openDevice(path, false);
    }
    if (const char* path = std::getenv("APPLAUNCH_LINUX_KEYBOARD_DEVICE")) {
        return openDevice(path, false);
    }
    if (openDevice("/dev/input/by-path/platform-3f804000.i2c-event", false)) {
        return true;
    }

    for (int index = 0; index < 32; ++index) {
        if (openDevice("/dev/input/event" + std::to_string(index), true)) {
            return true;
        }
    }
    spdlog::warn("MusicKeypad: no compatible input device found");
#endif
    return false;
}

bool MusicKeypad::openDevice(const std::string& path, bool require_music_keys)
{
#if !MUSIC_USE_SDL && defined(__linux__)
    const int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    if (require_music_keys && !hasMusicKeys(fd)) {
        ::close(fd);
        return false;
    }
    _event_fds.push_back(fd);
    spdlog::info("MusicKeypad: opened {} (shared, no EVIOCGRAB)", path);
    return true;
#else
    (void)path;
    (void)require_music_keys;
    return false;
#endif
}

void MusicKeypad::close()
{
#if !MUSIC_USE_SDL && defined(__linux__)
    for (const int fd : _event_fds) {
        if (fd >= 0) {
            ::close(fd);
        }
    }
#endif
    _event_fds.clear();
}

void MusicKeypad::poll()
{
#if !MUSIC_USE_SDL && defined(__linux__)
    for (const int fd : _event_fds) {
        while (true) {
            input_event event{};
            const ssize_t bytes_read = ::read(fd, &event, sizeof(event));
            if (bytes_read == sizeof(event)) {
                if (event.type == EV_KEY) {
                    pushLinuxKey(event.code, event.value);
                }
                continue;
            }
            if (bytes_read < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
                break;
            }
            if (bytes_read < 0) {
                spdlog::warn("MusicKeypad: input read failed: {}", std::strerror(errno));
            }
            break;
        }
    }
#endif
}

void MusicKeypad::setKeyCallback(KeyCallback callback) { _key_callback = std::move(callback); }

void MusicKeypad::pushLinuxKey(std::uint16_t code, std::int32_t value)
{
#if !MUSIC_USE_SDL && defined(__linux__)
    if (!_key_callback || (value != 0 && value != 1)) {
        return;
    }

    std::uint32_t key = 0;
    switch (code) {
        case KEY_UP:
        case KEY_F:
            key = music_key::Up;
            break;
        case KEY_DOWN:
        case KEY_X:
            key = music_key::Down;
            break;
        case KEY_LEFT:
        case KEY_Z:
            key = music_key::Left;
            break;
        case KEY_RIGHT:
        case KEY_C:
            key = music_key::Right;
            break;
        case KEY_ENTER:
        case KEY_KPENTER:
            key = music_key::Enter;
            break;
        case KEY_ESC:
            key = music_key::Escape;
            break;
        case KEY_SPACE:
            key = music_key::Space;
            break;
        case KEY_PLAYPAUSE:
            key = music_key::PlayPause;
            break;
        case KEY_PREVIOUSSONG:
        case KEY_REWIND:
            key = music_key::Previous;
            break;
        case KEY_NEXTSONG:
        case KEY_FASTFORWARD:
            key = music_key::Next;
            break;
        case KEY_HELP:
            key = music_key::Help;
            break;
        case KEY_4:
        case KEY_KP4:
            key = music_key::Key4;
            break;
        case KEY_5:
        case KEY_KP5:
            key = music_key::Key5;
            break;
        case KEY_6:
        case KEY_KP6:
            key = music_key::Key6;
            break;
        case KEY_7:
        case KEY_KP7:
            key = music_key::Key7;
            break;
        case KEY_8:
        case KEY_KP8:
            key = music_key::Key8;
            break;
        default:
            return;
    }
    _key_callback(key, value == 1);
#else
    (void)code;
    (void)value;
#endif
}

}  // namespace music
