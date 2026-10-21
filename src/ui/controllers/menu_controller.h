#pragma once
#include <lvgl.h>
#include "../../system/operation_interlock.h"

class UIManager;

// Handles the interactive menu: BLE/logging toggles, brightness sliders, maintenance actions, and stats

class MenuUIController {
public:
    explicit MenuUIController(UIManager* manager);

    void register_events();
    void update();
    void handle_calibrate();
    void handle_reset();
    void handle_purge();
    void handle_motor_test();
    void handle_scale_open();
    void handle_scale_tare();
    void handle_autotune();
    void handle_back();
    void handle_refresh_stats();
    void handle_install_update();
    void handle_diagnostics_reset();
    void handle_ble_toggle();
    void handle_ble_startup_toggle();
    void handle_ble_icon_toggle();
    void handle_wifi_toggle();
    void handle_wifi_tab_toggle();
    void handle_wifi_icon_toggle();
    void handle_wifi_power_toggle();
    void handle_logging_toggle();
    void handle_grind_mode_swipe_toggle();
    void handle_grind_mode_radio_button();
    void handle_auto_start_toggle();
    void handle_auto_start_threshold_slider();
    void handle_auto_start_threshold_slider_released();
    void handle_auto_return_toggle();
    void handle_grinder_purge_mode_radio_button();
    void handle_grinder_purge_amount_slider();
    void handle_grinder_purge_amount_slider_released();
    void handle_grind_freshness_hours_slider();
    void handle_grind_freshness_hours_slider_released();
    void handle_coast_ratio_slider();
    void handle_coast_ratio_slider_released();
    void handle_motor_latency_slider();
    void handle_motor_latency_slider_released();
    void handle_single_dose_timeout_slider();
    void handle_single_dose_timeout_slider_released();
    void handle_grind_tab_manual_toggle();
    void handle_grind_tab_single_toggle();
    void handle_grind_tab_double_toggle();
    void handle_grind_tab_custom_toggle();
    void handle_mode_icons_toggle();
    void handle_grind_layout_radio_button();
    void handle_demo_animation();
    void handle_disc_color_cycle();
    void handle_brightness_normal_slider();
    void handle_brightness_normal_slider_released();
    void handle_brightness_screensaver_slider();
    void handle_brightness_screensaver_slider_released();
    void handle_screensaver_startup_toggle();
    void handle_screensaver_sleep_toggle();
    void handle_display_off_toggle();

    float get_normal_brightness() const;
    float get_screensaver_brightness() const;
    void return_to_menu();
    void auto_return_to_ready();
    void reset_idle_timer();

private:
    UIManager* ui_manager_;
    lv_timer_t* motor_timer_{};
    OperationInterlock::Token motor_test_token_ = 0;
    uint32_t last_status_update_ms_ = 0;
    uint32_t last_activity_ms_ = 0;
    bool in_menu_ = false;
    bool diagnostics_active_tracking_ = false;
    lv_obj_t* last_active_page_ = nullptr;

    void perform_factory_reset();
    void execute_purge_operation();
    void run_motor_test();
    void stop_motor_timer();
    void motor_timer_cb(lv_timer_t* timer);
    static void static_motor_timer_cb(lv_timer_t* timer);
    void perform_diagnostics_reset();
    void handle_grind_tab_toggle(int which);
};
