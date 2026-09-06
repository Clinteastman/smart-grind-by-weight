#include "screen_timeout_controller.h"
#include "screensaver_controller.h"

#include <Arduino.h>

#include "../../config/constants.h"
#include "../../config/logging.h"
#include "../../hardware/display_manager.h"
#include "../../hardware/hardware_manager.h"
#include "../../system/display_idle_policy.h"
#include "../ui_manager.h"

ScreenTimeoutController::ScreenTimeoutController(UIManager* manager)
    : ui_manager_(manager)
    , timing_settings_(ScreensaverSettings::load_timing())
    , settings_applied_at_ms_(millis())
    , last_weight_activity_ms_(settings_applied_at_ms_)
    , screen_dimmed_(false)
    , panel_off_(false) {}

void ScreenTimeoutController::register_events() {}

void ScreenTimeoutController::update() {
    if (!ui_manager_) {
        return;
    }

    auto* hardware = ui_manager_->hardware_manager;
    if (!hardware) {
        return;
    }

    auto* display = hardware->get_display();
    if (!display) {
        return;
    }

    if (is_protected_state()) {
        restore_normal_display(display);
        return;
    }

    uint32_t now = millis();
    refresh_settings_if_needed(now);

    auto* touch_driver = display->get_touch_driver();
    if (!touch_driver) {
        return;
    }

    uint32_t ms_since_touch = touch_driver->get_ms_since_last_touch();
    auto* sensor = hardware->get_weight_sensor();
    uint32_t idle_timeout_ms = ScreensaverSettings::idle_timeout_ms(timing_settings_);
    uint32_t display_off_timeout_ms =
        ScreensaverSettings::display_off_timeout_ms(timing_settings_);
    const uint32_t ms_since_settings_applied = now - settings_applied_at_ms_;
    float weight_delta_g = 0.0f;
    if (sensor && sensor->get_weight_delta(1000U, &weight_delta_g) &&
        display_weight_activity_detected(weight_delta_g, USER_WEIGHT_ACTIVITY_THRESHOLD_G)) {
        last_weight_activity_ms_ = now;
    }

    // Use the age of the most recent real activity. Comparing the ends of a
    // short weight window rejects isolated scale noise that could otherwise
    // wake the panel repeatedly from a long min/max history.
    const uint32_t ms_since_weight = now - last_weight_activity_ms_;
    const uint32_t inactive_ms = min(min(ms_since_touch, ms_since_weight),
                                     ms_since_settings_applied);
    const DisplayIdleState idle_state = display_idle_state(
        inactive_ms,
        idle_timeout_ms,
        timing_settings_.display_off_enabled,
        static_cast<uint32_t>(timing_settings_.display_off_delay_s) * 1000U);
    const bool should_dim = idle_state != DisplayIdleState::ACTIVE;
    const bool should_turn_off = idle_state == DisplayIdleState::PANEL_OFF;

    if (should_turn_off && !panel_off_) {
        if (!screen_dimmed_) {
            float dimmed = USER_SCREEN_BRIGHTNESS_DIMMED;
            if (ui_manager_->menu_controller_) {
                dimmed = ui_manager_->menu_controller_->get_screensaver_brightness();
            }
            display->set_brightness(dimmed);
            screen_dimmed_ = true;
            if (screensaver_controller_ &&
                screensaver_controller_->is_sleep_enabled() &&
                screensaver_controller_->has_image()) {
                screensaver_controller_->show();
            }
        }
        display->set_panel_power(false);
        panel_off_ = true;
        LOG_BLE("[DISPLAY] Panel off after %lus idle (%lus after screensaver)\n",
                static_cast<unsigned long>(display_off_timeout_ms / 1000U),
                static_cast<unsigned long>(timing_settings_.display_off_delay_s));
    } else if (should_dim && !screen_dimmed_) {
        float dimmed = USER_SCREEN_BRIGHTNESS_DIMMED;
        if (ui_manager_->menu_controller_) {
            dimmed = ui_manager_->menu_controller_->get_screensaver_brightness();
        }
        display->set_brightness(dimmed);
        screen_dimmed_ = true;

        // Show screensaver image if enabled
        if (screensaver_controller_ &&
            screensaver_controller_->is_sleep_enabled() &&
            screensaver_controller_->has_image()) {
            screensaver_controller_->show();
        }
        LOG_BLE("[DISPLAY] Idle timeout reached after %lus; dimmed with screensaver=%s\n",
                static_cast<unsigned long>(timing_settings_.idle_timeout_s),
                screensaver_controller_ && screensaver_controller_->is_visible() ? "ON" : "OFF");
    } else if (!should_dim && (screen_dimmed_ || panel_off_)) {
        restore_normal_display(display);
    }
}

bool ScreenTimeoutController::apply_runtime_settings(bool verify_storage) {
    ScreensaverTimingSettings loaded;
    if (verify_storage) {
        if (!ScreensaverSettings::load_timing_checked(loaded)) return false;
    } else {
        loaded = ScreensaverSettings::load_timing();
    }
    timing_settings_ = loaded;
    settings_applied_at_ms_ = millis();
    last_weight_activity_ms_ = settings_applied_at_ms_;
    last_settings_refresh_ms_ = settings_applied_at_ms_;
    LOG_BLE("[DISPLAY] Runtime settings applied; screensaver=%lus panel-off=%s delay=%lus\n",
            static_cast<unsigned long>(timing_settings_.idle_timeout_s),
            timing_settings_.display_off_enabled ? "ON" : "OFF",
            static_cast<unsigned long>(timing_settings_.display_off_delay_s));

    if (!ui_manager_ || !ui_manager_->hardware_manager) return false;
    DisplayManager* display = ui_manager_->hardware_manager->get_display();
    if (!display) return false;
    restore_normal_display(display);
    return true;
}

void ScreenTimeoutController::refresh_settings_if_needed(uint32_t now_ms) {
    constexpr uint32_t kRefreshIntervalMs = 5000;

    if (last_settings_refresh_ms_ != 0 &&
        now_ms - last_settings_refresh_ms_ < kRefreshIntervalMs) {
        return;
    }

    timing_settings_ = ScreensaverSettings::load_timing();
    last_settings_refresh_ms_ = now_ms;
}

void ScreenTimeoutController::restore_normal_display(DisplayManager* display) {
    bool screensaver_visible = screensaver_controller_ && screensaver_controller_->is_visible();
    if (screensaver_visible) {
        screensaver_controller_->hide();
    }

    if (screen_dimmed_ || panel_off_ || screensaver_visible) {
        float normal = USER_SCREEN_BRIGHTNESS_NORMAL;
        if (ui_manager_ && ui_manager_->menu_controller_) {
            normal = ui_manager_->menu_controller_->get_normal_brightness();
        }
        display->set_brightness(normal);
    }

    if (panel_off_ || !display->is_panel_powered_on()) {
        display->set_panel_power(true);
    }

    screen_dimmed_ = false;
    panel_off_ = false;
}

bool ScreenTimeoutController::is_protected_state() const {
    if (!ui_manager_) {
        return false;
    }

    bool ota_active = ui_manager_->bluetooth_manager &&
                      ui_manager_->bluetooth_manager->is_updating();
    if (ota_active) {
        return true;
    }

    if (!ui_manager_->state_machine) {
        return false;
    }

    UIState state = ui_manager_->state_machine->get_current_state();
    return state == UIState::GRINDING ||
           state == UIState::OTA_UPDATE ||
           state == UIState::OTA_UPDATE_FAILED;
}
