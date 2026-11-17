#include "sim_platform.h"

#include <src/drivers/windows/lv_windows_display.h>
#include <windows.h>

namespace sim_platform {
namespace {

HWND simulator_window = nullptr;

}  // namespace

lv_display_t* create_display(int32_t width, int32_t height, bool hidden) {
    lv_display_t* display = lv_windows_create_display(
        L"Smart Grind-by-Weight Simulator",
        width,
        height,
        140,
        true,
        true);
    if (!display) {
        return nullptr;
    }

    simulator_window = lv_windows_get_display_window_handle(display);
    if (hidden) {
        ShowWindow(simulator_window, SW_HIDE);
    }
    return display;
}

bool is_window_open() {
    return IsWindow(simulator_window) != FALSE;
}

void set_status(const char* status) {
    if (simulator_window) {
        wchar_t title[160];
        swprintf_s(title, L"Smart Grind Simulator - %S  [V: view, T: tare]", status);
        SetWindowTextW(simulator_window, title);
    }
}

bool is_key_down(char key) {
    return (GetAsyncKeyState(key) & 0x8000) != 0;
}

uint32_t now_ms() {
    return static_cast<uint32_t>(GetTickCount64());
}

void sleep_ms(uint32_t milliseconds) {
    Sleep(milliseconds);
}

void exit_process(int code) {
    ExitProcess(static_cast<UINT>(code));
}

}  // namespace sim_platform
