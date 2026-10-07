#pragma once

// Desktop window, input and process services for the simulator. Windows uses
// LVGL's native Win32 driver (sim_platform_windows.cpp); macOS and Linux use
// LVGL's SDL2 driver (sim_platform_sdl.cpp). Everything else in the simulator
// is platform-independent.

#include <lvgl.h>
#include <cstdint>

namespace sim_platform {

// Opens the simulated panel window with mouse input and returns its display.
// Automated tests pass hidden=true so they never need an interactive desktop.
lv_display_t* create_display(int32_t width, int32_t height, bool hidden);

// False once the user has closed the simulator window.
bool is_window_open();

// Shows the simulator state and the desktop shortcuts in the window title.
void set_status(const char* status);

// True while the given letter key ('A' to 'Z') is held down.
bool is_key_down(char key);

uint32_t now_ms();
void sleep_ms(uint32_t milliseconds);

// Ends the process immediately with the given exit code (used by tests).
[[noreturn]] void exit_process(int code);

}  // namespace sim_platform
