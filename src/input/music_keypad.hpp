#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace music {

class MusicKeypad {
public:
    using KeyCallback = std::function<void(std::uint32_t key, bool pressed)>;

    ~MusicKeypad();

    bool openDefault();
    void close();
    void poll();
    void setKeyCallback(KeyCallback callback);

private:
    std::vector<int> _event_fds;
    KeyCallback _key_callback;

    bool openDevice(const std::string& path, bool require_music_keys);
    void pushLinuxKey(std::uint16_t code, std::int32_t value);
};

}  // namespace music
