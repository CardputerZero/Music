#include "hal/music_graphics_hal.hpp"

#include "input/music_keys.hpp"

#include <algorithm>
#include <cstdlib>
#include <utility>
#include <spdlog/spdlog.h>

#if MUSIC_USE_SDL
#include "src/drivers/sdl/lv_sdl_keyboard.h"
#include "src/drivers/sdl/lv_sdl_mouse.h"
#include "src/drivers/sdl/lv_sdl_window.h"
#elif LV_USE_LINUX_FBDEV
#include "src/drivers/display/fb/lv_linux_fbdev.h"
#endif

namespace music {
namespace {

const char* envOrDefault(const char* name, const char* fallback)
{
    const char* value = std::getenv(name);
    return value && value[0] != '\0' ? value : fallback;
}

#if MUSIC_USE_SDL
float envFloatOrDefault(const char* name, float fallback)
{
    const char* value = std::getenv(name);
    if (!value || value[0] == '\0') {
        return fallback;
    }

    char* end = nullptr;
    const float parsed = std::strtof(value, &end);
    return end && end != value && parsed > 0.0f ? parsed : fallback;
}
#endif

bool displayExists(const lv_display_t* display)
{
    lv_display_t* current = lv_display_get_next(nullptr);
    while (current) {
        if (current == display) {
            return true;
        }
        current = lv_display_get_next(current);
    }
    return false;
}

bool indevExists(const lv_indev_t* indev)
{
    lv_indev_t* current = lv_indev_get_next(nullptr);
    while (current) {
        if (current == indev) {
            return true;
        }
        current = lv_indev_get_next(current);
    }
    return false;
}

}  // namespace

MusicGraphicsHal::~MusicGraphicsHal() { shutdown(); }

bool MusicGraphicsHal::initialize(int logical_width, int logical_height)
{
    if (logical_width <= 0 || logical_height <= 0) {
        spdlog::error("Music graphics: invalid logical display {}x{}", logical_width, logical_height);
        return false;
    }

    _logical_width = logical_width;
    _logical_height = logical_height;

#if MUSIC_USE_SDL
    _display = lv_sdl_window_create(logical_width, logical_height);
    if (!_display) {
        spdlog::error("Music graphics: failed to create the LVGL SDL display");
        return false;
    }

    const float zoom = envFloatOrDefault("MUSIC_SDL_ZOOM", 1.0f);
    lv_sdl_window_set_resizeable(_display, false);
    lv_sdl_window_set_zoom(_display, zoom);
    lv_sdl_window_set_title(_display, envOrDefault("LV_SDL_WINDOW_TITLE", "Music"));

    _mouse = lv_sdl_mouse_create();
    if (_mouse) {
        lv_indev_set_display(_mouse, _display);
    }

    _keyboard = lv_sdl_keyboard_create();
    if (_keyboard) {
        lv_indev_set_display(_keyboard, _display);
        lv_indev_add_event_cb(_keyboard, desktopKeyboardEvent, LV_EVENT_KEY, this);
    } else {
        spdlog::warn("Music graphics: failed to create the LVGL SDL keyboard input");
    }

    spdlog::info("Music graphics: LVGL SDL display {}x{} (zoom {})", logical_width, logical_height, zoom);
#elif LV_USE_LINUX_FBDEV
    _display = lv_linux_fbdev_create();
    if (!_display) {
        spdlog::error("Music graphics: failed to create the LVGL framebuffer display");
        return false;
    }

    const char* device = envOrDefault("LV_LINUX_FBDEV_DEVICE", "/dev/fb0");
    if (lv_linux_fbdev_set_file(_display, device) != LV_RESULT_OK) {
        spdlog::error("Music graphics: failed to open framebuffer {}", device);
        lv_display_delete(_display);
        _display = nullptr;
        return false;
    }
    spdlog::info("Music graphics: LVGL framebuffer display {}x{} on {}", logical_width, logical_height, device);
#else
    spdlog::error("Music graphics: no LVGL display driver enabled");
    return false;
#endif

    lv_display_set_default(_display);
    _last_frame_tick = lv_tick_get();
    _initialized = true;
    return true;
}

void MusicGraphicsHal::shutdown()
{
    if (!_initialized) {
        return;
    }
    _initialized = false;

    if (_keyboard && indevExists(_keyboard)) {
        lv_indev_delete(_keyboard);
    }
    if (_mouse && indevExists(_mouse)) {
        lv_indev_delete(_mouse);
    }
    _keyboard = nullptr;
    _mouse = nullptr;

    if (_display && displayExists(_display)) {
        lv_display_delete(_display);
    }
    _display = nullptr;

#if MUSIC_USE_SDL
    lv_sdl_quit();
#endif
}

bool MusicGraphicsHal::shouldClose() const { return !_initialized || !_display || !displayExists(_display); }

float MusicGraphicsHal::frameDelta()
{
    const std::uint32_t now = lv_tick_get();
    const std::uint32_t elapsed = lv_tick_elaps(_last_frame_tick);
    _last_frame_tick = now;
    return std::clamp(static_cast<float>(elapsed) / 1000.0f, 0.0f, 1.0f / 15.0f);
}

void MusicGraphicsHal::setKeyCallback(KeyCallback callback) { _key_callback = std::move(callback); }

void MusicGraphicsHal::desktopKeyboardEvent(lv_event_t* event)
{
    auto* self = static_cast<MusicGraphicsHal*>(lv_event_get_user_data(event));
    auto* indev = static_cast<lv_indev_t*>(lv_event_get_target(event));
    if (!self || !indev) {
        return;
    }
    self->handleDesktopKey(indev);
}

void MusicGraphicsHal::handleDesktopKey(lv_indev_t* indev)
{
    if (!_key_callback) {
        return;
    }

    const std::uint32_t key = lv_indev_get_key(indev);
    std::uint32_t mapped = 0;
    switch (key) {
        case LV_KEY_UP:
        case 'f':
        case 'F':
            mapped = music_key::Up;
            break;
        case LV_KEY_DOWN:
        case 'x':
        case 'X':
            mapped = music_key::Down;
            break;
        case LV_KEY_LEFT:
        case 'z':
        case 'Z':
            mapped = music_key::Left;
            break;
        case LV_KEY_RIGHT:
        case 'c':
        case 'C':
            mapped = music_key::Right;
            break;
        case LV_KEY_ENTER:
            mapped = music_key::Enter;
            break;
        case LV_KEY_ESC:
            mapped = music_key::Escape;
            break;
        case ' ':
            mapped = music_key::Space;
            break;
        case 'q':
        case 'Q':
            mapped = music_key::PlayPause;
            break;
        case 'w':
        case 'W':
            mapped = music_key::Previous;
            break;
        case 'e':
        case 'E':
            mapped = music_key::Next;
            break;
        // SDL has no Fn layer, so use S/D as desktop aliases for the
        // CardputerZero Fn+S/Fn+D volume shortcuts.
        case 's':
        case 'S':
            mapped = music_key::VolumeDown;
            break;
        case 'd':
        case 'D':
            mapped = music_key::VolumeUp;
            break;
        case 'h':
        case 'H':
            mapped = music_key::Help;
            break;
        case '4':
            mapped = music_key::Key4;
            break;
        case '5':
            mapped = music_key::Key5;
            break;
        case '6':
            mapped = music_key::Key6;
            break;
        case '7':
            mapped = music_key::Key7;
            break;
        case '8':
            mapped = music_key::Key8;
            break;
        default:
            return;
    }
    _key_callback(mapped, lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED);
}

int MusicGraphicsHal::logicalWidth() const noexcept { return _logical_width; }

int MusicGraphicsHal::logicalHeight() const noexcept { return _logical_height; }

}  // namespace music
