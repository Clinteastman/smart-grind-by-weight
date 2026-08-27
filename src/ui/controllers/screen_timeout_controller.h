#pragma once

#include <cstdint>
#include "../../system/screensaver_settings.h"

class UIManager;
class ScreensaverController;
class DisplayManager;

// Implements automatic screen dimming based on touch/weight activity

class ScreenTimeoutController {
public:
    explicit ScreenTimeoutController(UIManager* manager);

    void register_events();
    void update();

    // Apply web/menu preference changes immediately and begin a fresh idle
    // interval so activity recorded before the save cannot postpone the new
    // timeout.
    void apply_runtime_settings();

    void set_screensaver_controller(ScreensaverController* controller) {
        screensaver_controller_ = controller;
    }

private:
    void refresh_settings_if_needed(uint32_t now_ms);
    void restore_normal_display(DisplayManager* display);
    bool is_protected_state() const;

    UIManager* ui_manager_;
    ScreensaverController* screensaver_controller_ = nullptr;
    ScreensaverTimingSettings timing_settings_;
    uint32_t last_settings_refresh_ms_ = 0;
    uint32_t settings_applied_at_ms_ = 0;
    uint32_t last_weight_activity_ms_ = 0;
    bool screen_dimmed_;
    bool panel_off_;
};
