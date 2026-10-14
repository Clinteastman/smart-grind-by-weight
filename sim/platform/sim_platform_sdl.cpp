#include "sim_platform.h"

#include <src/drivers/sdl/lv_sdl_mouse.h>
#include <src/drivers/sdl/lv_sdl_window.h>
#include LV_SDL_INCLUDE_PATH

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

namespace sim_platform {
namespace {

// Same window scale as the Windows simulator, so the panel is easy to read.
constexpr float kWindowZoom = 1.4f;

lv_display_t* simulator_display = nullptr;

void forget_deleted_display(lv_event_t*) {
    // LVGL's SDL driver deletes the display when the window is closed.
    simulator_display = nullptr;
}

}  // namespace

lv_display_t* create_display(int32_t width, int32_t height, bool hidden) {
    simulator_display = lv_sdl_window_create(width, height);
    if (!simulator_display) {
        return nullptr;
    }

    lv_display_add_event_cb(simulator_display, forget_deleted_display, LV_EVENT_DELETE, nullptr);
    lv_sdl_window_set_zoom(simulator_display, kWindowZoom);
    lv_sdl_mouse_create();
    if (hidden) {
        SDL_HideWindow(lv_sdl_window_get_window(simulator_display));
    }
    return simulator_display;
}

bool is_window_open() {
    return simulator_display != nullptr;
}

void set_status(const char* status) {
    if (simulator_display) {
        const std::string title =
            std::string("Smart Grind Simulator - ") + status + "  [V: view, T: tare]";
        lv_sdl_window_set_title(simulator_display, title.c_str());
    }
}

bool is_key_down(char key) {
    if (key < 'A' || key > 'Z') {
        return false;
    }
    // LVGL's SDL timer pumps window events, which keeps this state current.
    const Uint8* keyboard = SDL_GetKeyboardState(nullptr);
    const auto scancode = static_cast<SDL_Scancode>(SDL_SCANCODE_A + (key - 'A'));
    return keyboard && keyboard[scancode] != 0;
}

uint32_t now_ms() {
    using namespace std::chrono;
    return static_cast<uint32_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

void sleep_ms(uint32_t milliseconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

void exit_process(int code) {
    std::fflush(stdout);
    std::fflush(stderr);
    std::_Exit(code);
}

}  // namespace sim_platform
