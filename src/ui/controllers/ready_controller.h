#pragma once
#include <lvgl.h>
#include "../event_bridge_lvgl.h"

class UIManager;

// Handles profile tab navigation, long-press editing, and swipe mode switching

class ReadyUIController {
public:
    explicit ReadyUIController(UIManager* manager);

    void register_events();
    void update();
    void refresh_profiles();
    void handle_tab_change(int tab);
    void handle_profile_long_press();
    void toggle_mode();

private:
    UIManager* ui_manager_;
    uint32_t last_network_update_ms_ = 0;
};
