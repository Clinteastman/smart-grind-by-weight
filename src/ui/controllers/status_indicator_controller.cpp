#include "status_indicator_controller.h"

#include "../../config/constants.h"
#include "../../controllers/grind_mode.h"
#include "../../network/network_manager.h"
#include "../../network/device_web_server.h"
#include "../../system/diagnostics_controller.h"
#include "../ui_manager.h"

StatusIndicatorController::StatusIndicatorController(UIManager* manager)
    : ui_manager_(manager) {}

void StatusIndicatorController::build() {
    if (!ui_manager_) {
        return;
    }

    if (ble_status_icon_) {
        return;
    }

    // Create BLE status icon (rightmost)
    ble_status_icon_ = lv_label_create(lv_scr_act());
    lv_label_set_text(ble_status_icon_, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_font(ble_status_icon_, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(ble_status_icon_, lv_color_hex(THEME_COLOR_ACCENT), 0);
    lv_obj_align(ble_status_icon_, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_obj_add_flag(ble_status_icon_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ble_status_icon_, LV_OBJ_FLAG_CLICKABLE);

    // Wi-Fi is grey while enabled but offline/setup, white when connected.
    wifi_status_icon_ = lv_label_create(lv_scr_act());
    lv_label_set_text(wifi_status_icon_, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(wifi_status_icon_, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(wifi_status_icon_, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);
    lv_obj_align(wifi_status_icon_, LV_ALIGN_TOP_RIGHT, -45, 10);
    lv_obj_add_flag(wifi_status_icon_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(wifi_status_icon_, LV_OBJ_FLAG_CLICKABLE);

    // Create warning icon (left of the connection icons)
    warning_icon_ = lv_label_create(lv_scr_act());
    lv_label_set_text(warning_icon_, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_font(warning_icon_, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(warning_icon_, lv_color_hex(THEME_COLOR_WARNING), 0);
    lv_obj_align(warning_icon_, LV_ALIGN_TOP_RIGHT, -80, 10);
    lv_obj_add_flag(warning_icon_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(warning_icon_, LV_OBJ_FLAG_CLICKABLE);

    // A refresh glyph appears only when the background check has found a
    // newer stable release for this exact hardware revision. It is also a
    // generous touch target for starting the update from the grinder.
    firmware_update_icon_ = lv_label_create(lv_scr_act());
    lv_label_set_text(firmware_update_icon_, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_font(firmware_update_icon_, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(firmware_update_icon_, lv_color_hex(THEME_COLOR_SUCCESS), 0);
    lv_obj_align(firmware_update_icon_, LV_ALIGN_TOP_RIGHT, -115, 10);
    lv_obj_add_flag(firmware_update_icon_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(firmware_update_icon_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(firmware_update_icon_, 12);
    lv_obj_add_event_cb(
        firmware_update_icon_,
        [](lv_event_t* event) {
            if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
            auto* controller = static_cast<StatusIndicatorController*>(lv_event_get_user_data(event));
            if (controller) controller->prompt_firmware_update();
        },
        LV_EVENT_CLICKED, this);

    update_ble_status_icon();
    update_wifi_status_icon();
    update_warning_icon();
    update_firmware_update_icon();
}

void StatusIndicatorController::update() {
    update_ble_status_icon();
    update_wifi_status_icon();
    update_warning_icon();
    update_firmware_update_icon();
}

void StatusIndicatorController::update_firmware_update_icon() {
    if (!ui_manager_ || !firmware_update_icon_) return;
    const bool grinder_idle = !ui_manager_->grind_controller ||
                              !ui_manager_->grind_controller->is_active();
    const int8_t state = device_web_server.firmware_update_available() && grinder_idle ? 1 : 0;
    if (state == last_firmware_update_state_) return;
    last_firmware_update_state_ = state;
    if (state != 0) {
        lv_obj_clear_flag(firmware_update_icon_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(firmware_update_icon_, LV_OBJ_FLAG_HIDDEN);
    }
}

void StatusIndicatorController::prompt_firmware_update() {
    if (!ui_manager_ || !device_web_server.firmware_update_available()) return;
    const String tag = device_web_server.latest_release_tag();
    if (tag.isEmpty()) return;
    const String message = "Install " + tag + " now?\n\nThe grinder will restart.\nDo not remove power.";
    ui_manager_->show_confirmation(
        "UPDATE READY", message.c_str(), "INSTALL", lv_color_hex(THEME_COLOR_SUCCESS),
        [this]() {
            if (!device_web_server.install_available_update()) return;
            ui_manager_->ota_screen.show_ota_mode();
            ui_manager_->ota_screen.update_status("Preparing update...");
            ui_manager_->switch_to_state(UIState::OTA_UPDATE);
        },
        "LATER");
}

void StatusIndicatorController::update_ble_status_icon() {
    if (!ui_manager_ || !ble_status_icon_) {
        return;
    }

    auto* bluetooth = ui_manager_->bluetooth_manager;
    const int8_t state = !bluetooth || !bluetooth->is_enabled()
                             ? 0
                             : bluetooth->is_connected() ? 2 : 1;
    if (state == last_ble_state_) return;
    last_ble_state_ = state;
    if (state != 0) {
        lv_obj_clear_flag(ble_status_icon_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_color(ble_status_icon_,
                                    state == 2 ? lv_color_hex(THEME_COLOR_SUCCESS)
                                               : lv_color_hex(THEME_COLOR_ACCENT),
                                    0);
    } else {
        lv_obj_add_flag(ble_status_icon_, LV_OBJ_FLAG_HIDDEN);
    }
}

void StatusIndicatorController::update_wifi_status_icon() {
    if (!wifi_status_icon_) return;

    const int8_t state = !network_manager.is_enabled()
                             ? 0
                             : network_manager.is_connected() ? 2 : 1;
    if (state == last_wifi_state_) return;
    last_wifi_state_ = state;
    if (state == 0) {
        lv_obj_add_flag(wifi_status_icon_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_clear_flag(wifi_status_icon_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_color(wifi_status_icon_,
                                state == 2 ? lv_color_hex(THEME_COLOR_TEXT_PRIMARY)
                                           : lv_color_hex(THEME_COLOR_TEXT_SECONDARY),
                                0);
}

void StatusIndicatorController::update_warning_icon() {
    if (!ui_manager_ || !warning_icon_) {
        return;
    }

    // Check if there are any diagnostic warnings
    if (ui_manager_->diagnostics_controller_) {
        // All load-cell diagnostics are irrelevant in sensor-free TIME mode,
        // but non-sensor warnings (for example mechanical instability) remain.
        const bool include_load_cell = ui_manager_->current_mode == GrindMode::WEIGHT;
        DiagnosticCode diagnostic =
            ui_manager_->diagnostics_controller_->get_highest_priority_warning(include_load_cell);

        const int8_t state = diagnostic != DiagnosticCode::NONE ? 1 : 0;
        if (state == last_warning_state_) return;
        last_warning_state_ = state;
        if (state != 0) {
            lv_obj_clear_flag(warning_icon_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(warning_icon_, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        if (last_warning_state_ == 0) return;
        last_warning_state_ = 0;
        lv_obj_add_flag(warning_icon_, LV_OBJ_FLAG_HIDDEN);
    }
}
