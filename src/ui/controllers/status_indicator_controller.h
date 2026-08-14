#pragma once
#include <lvgl.h>

class UIManager;

// Shows Bluetooth and Wi-Fi connection status icons with color coding
// Shows diagnostic warning icon when issues detected

class StatusIndicatorController {
public:
    explicit StatusIndicatorController(UIManager* manager);

    void build();
    void update();

private:
    void update_ble_status_icon();
    void update_wifi_status_icon();
    void update_warning_icon();

    UIManager* ui_manager_;
    lv_obj_t* ble_status_icon_ = nullptr;
    lv_obj_t* wifi_status_icon_ = nullptr;
    lv_obj_t* warning_icon_ = nullptr;
    int8_t last_ble_state_ = -1;
    int8_t last_wifi_state_ = -1;
    int8_t last_warning_state_ = -1;
};
