#pragma once

#include <cstdint>
#include <functional>

#include <lvgl.h>

namespace music {

class MusicGraphicsHal {
public:
    using KeyCallback = std::function<void(std::uint32_t key, bool pressed)>;

    MusicGraphicsHal() = default;
    ~MusicGraphicsHal();

    MusicGraphicsHal(const MusicGraphicsHal&) = delete;
    MusicGraphicsHal& operator=(const MusicGraphicsHal&) = delete;

    bool initialize(int logical_width, int logical_height);
    void shutdown();
    bool shouldClose() const;
    float frameDelta();
    void setKeyCallback(KeyCallback callback);

    int logicalWidth() const noexcept;
    int logicalHeight() const noexcept;

private:
    static void desktopKeyboardEvent(lv_event_t* event);

    void handleDesktopKey(lv_indev_t* indev);

    int _logical_width = 0;
    int _logical_height = 0;
    std::uint32_t _last_frame_tick = 0;
    lv_display_t* _display = nullptr;
    lv_indev_t* _keyboard = nullptr;
    lv_indev_t* _mouse = nullptr;
    KeyCallback _key_callback;
    bool _initialized = false;
};

}  // namespace music
