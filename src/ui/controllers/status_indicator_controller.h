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
    void prompt_firmware_update();
    void update_wifi_status_icon();
    void update_ble_status_icon();

private:
    void update_warning_icon();
    void update_firmware_update_icon();

    void draw_wifi_icon(lv_event_t* e);

    UIManager* ui_manager_;
    lv_obj_t* ble_status_icon_ = nullptr;
    lv_obj_t* wifi_status_icon_ = nullptr;
    lv_obj_t* warning_icon_ = nullptr;
    lv_obj_t* firmware_update_icon_ = nullptr;
    int8_t last_ble_state_ = -1;
    int last_ble_visible_tab_ = -1;
    int8_t last_wifi_state_ = -1;
    int8_t last_wifi_level_ = -1;
    int last_wifi_visible_tab_ = -1;
    uint32_t last_wifi_poll_ms_ = 0;
    int8_t last_warning_state_ = -1;
    int8_t last_firmware_update_state_ = -1;
};
