#include "core/music_app.hpp"
#include "core/music_config.hpp"
#include "hal/music_graphics_hal.hpp"
#include "input/music_keypad.hpp"

#include <core/hal/hal.hpp>
#include <csignal>
#include <lvgl.h>
#include <spdlog/spdlog.h>
#include <unistd.h>

namespace {

volatile std::sig_atomic_t exit_requested = 0;

void handleExitSignal(int) { exit_requested = 1; }

}  // namespace

int main()
{
    music::MusicConfig config = music::defaultMusicConfig();

    std::signal(SIGINT, handleExitSignal);
    std::signal(SIGTERM, handleExitSignal);

    lv_init();
    music::MusicGraphicsHal graphics;
    if (!graphics.initialize(config.screen_width, config.screen_height)) {
        return 1;
    }

    lv_display_t* display = lv_display_get_default();
    if (!display) {
        spdlog::error("Music: failed to create the LVGL display");
        return 1;
    }
    spdlog::info("Music: display {}x{}", static_cast<int>(lv_display_get_horizontal_resolution(display)),
                 static_cast<int>(lv_display_get_vertical_resolution(display)));

    smooth_ui_toolkit::ui_hal::on_get_tick([]() { return lv_tick_get(); });
    smooth_ui_toolkit::ui_hal::on_delay([](std::uint32_t milliseconds) { usleep(milliseconds * 1000); });

    music::MusicApp app(config);
    music::MusicKeypad keypad;
    graphics.setKeyCallback([&app](std::uint32_t key, bool pressed) { app.onKey(key, pressed); });
#if !MUSIC_USE_SDL
    keypad.setKeyCallback([&app](std::uint32_t key, bool pressed) { app.onKey(key, pressed); });
    keypad.openDefault();
#endif

    app.start();
    lv_obj_invalidate(lv_screen_active());
    while (!exit_requested && !app.quitRequested() && !graphics.shouldClose()) {
#if !MUSIC_USE_SDL
        keypad.poll();
#endif
        app.update(graphics.frameDelta());
        app.draw();
        lv_timer_handler();
        usleep(10000);
    }

    spdlog::info("Music: exit requested");
    app.stop();
    keypad.close();
    graphics.shutdown();
    lv_deinit();
    return 0;
}
