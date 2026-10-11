#pragma once
#include <lvgl.h>
#include <cstdint>

class UIManager;

// Handles the calibration workflow (tare, weight setting, completion)

class CalibrationUIController {
public:
    explicit CalibrationUIController(UIManager* manager);

    void register_events();
    void update();

    void handle_ok();
    void handle_cancel();
    void handle_plus(lv_event_code_t code);
    void handle_minus(lv_event_code_t code);

private:
    UIManager* ui_manager_;
    unsigned long noise_step_enter_ms_ = 0;
    unsigned long noise_ok_since_ms_ = 0;
    bool noise_check_active_ = false;
    bool noise_check_passed_ = false;
    bool noise_check_forced_pass_ = false;
    int32_t baseline_adc_value_ = 0;

    void start_noise_check();
    void reset_noise_check_state();
    void update_noise_check();
    void complete_calibration();
};
