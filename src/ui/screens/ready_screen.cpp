#include "ready_screen.h"
#include <Arduino.h>
#include "../../config/constants.h"
#include "../../controllers/grind_mode_traits.h"
#ifndef SMART_GRIND_SIM
#include "../../network/network_manager.h"
#include "../../network/provisioning_service.h"
#endif
#include "../ui_helpers.h"

namespace {

bool ready_text_empty(const ReadyScreenText& value) {
#ifdef SMART_GRIND_SIM
    return value.empty();
#else
    return value.isEmpty();
#endif
}

// Long enough to show several frames on hardware while remaining much more
// responsive than LVGL's 200-400 ms default scroll timing.
constexpr uint32_t READY_TAB_SWIPE_ANIMATION_MS = 180;

void shorten_tab_scroll_animation(lv_event_t* event) {
    lv_anim_t* animation = lv_event_get_scroll_anim(event);
    if (animation) {
        lv_anim_set_duration(animation, READY_TAB_SWIPE_ANIMATION_MS);
    }
}

#ifndef SMART_GRIND_SIM
String escape_wifi_qr_value(const String& value) {
    String escaped;
    escaped.reserve(value.length() + 8);
    for (size_t i = 0; i < value.length(); ++i) {
        const char ch = value[i];
        if (ch == '\\' || ch == ';' || ch == ',' || ch == ':' || ch == '"') escaped += '\\';
        escaped += ch;
    }
    return escaped;
}
#endif

} // namespace

void ReadyScreen::create() {
    screen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(screen, LV_PCT(100), LV_PCT(80));
    lv_obj_align(screen, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_add_flag(screen, LV_OBJ_FLAG_GESTURE_BUBBLE);

    // Create tabview
    tabview = lv_tabview_create(screen);
    lv_obj_set_size(tabview, LV_PCT(100), LV_PCT(100));
    lv_obj_align(tabview, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(tabview, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
    lv_obj_add_flag(tabview, LV_OBJ_FLAG_GESTURE_BUBBLE);

    // LVGL defaults scroll snapping to 200-400 ms. On the full-width AMOLED
    // tabs that makes a completed swipe feel noticeably behind the finger.
    lv_obj_t* tab_content = lv_tabview_get_content(tabview);
    lv_obj_add_event_cb(tab_content, shorten_tab_scroll_animation, LV_EVENT_SCROLL_BEGIN, nullptr);

    // Hide tab buttons for swipe-only interface
    lv_obj_t* tab_btns = lv_tabview_get_tab_btns(tabview);
    lv_obj_add_flag(tab_btns, LV_OBJ_FLAG_HIDDEN);

    // Transparent background
    lv_obj_set_style_bg_opa(tabview, LV_OPA_TRANSP, 0);

    // Add manual and profile tabs
    manual_tab = lv_tabview_add_tab(tabview, "Manual");
    profile_tabs[0] = lv_tabview_add_tab(tabview, "Single");
    profile_tabs[1] = lv_tabview_add_tab(tabview, "Double");
    profile_tabs[2] = lv_tabview_add_tab(tabview, "Custom");
    wifi_tab = lv_tabview_add_tab(tabview, "WI-FI");
    menu_tab = lv_tabview_add_tab(tabview, "MENU");

    create_manual_page(manual_tab);

    // Default weights
    float default_weights[3] = {USER_SINGLE_ESPRESSO_WEIGHT_G, USER_DOUBLE_ESPRESSO_WEIGHT_G, USER_CUSTOM_PROFILE_WEIGHT_G};
    const char* names[3] = {"SINGLE", "DOUBLE", "CUSTOM"};
    
    for (int i = 0; i < 3; i++) {
        create_profile_page(profile_tabs[i], i, names[i], default_weights[i]);
    }

    create_wifi_page(wifi_tab);

    // Create menu tab page
    create_menu_page(menu_tab);

    update_profile_values(default_weights, GrindMode::WEIGHT);

    visible = false;
}

void ReadyScreen::create_manual_page(lv_obj_t* parent) {
    lv_obj_set_layout(parent, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(parent, 8, 0);

    lv_obj_t* title = lv_label_create(parent);
    lv_label_set_text(title, "MANUAL");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);

    lv_obj_t* detail = lv_label_create(parent);
    lv_label_set_text(detail, "START / STOP\n30s safety limit");
    lv_obj_set_style_text_font(detail, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(detail, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_text_align(detail, LV_TEXT_ALIGN_CENTER, 0);
}

void ReadyScreen::create_wifi_page(lv_obj_t* parent) {
    lv_obj_set_layout(parent, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(parent, 4, 0);
    lv_obj_set_style_pad_gap(parent, 5, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    wifi_status_label = lv_label_create(parent);
    lv_obj_set_style_text_font(wifi_status_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(wifi_status_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_style_text_align(wifi_status_label, LV_TEXT_ALIGN_CENTER, 0);

    wifi_qr = lv_qrcode_create(parent);
    lv_qrcode_set_size(wifi_qr, 170);
    lv_qrcode_set_dark_color(wifi_qr, lv_color_hex(0x000000));
    lv_qrcode_set_light_color(wifi_qr, lv_color_hex(0xFFFFFF));
    lv_qrcode_set_quiet_zone(wifi_qr, true);

    wifi_detail_label = lv_label_create(parent);
    lv_obj_set_width(wifi_detail_label, LV_PCT(94));
    lv_label_set_long_mode(wifi_detail_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(wifi_detail_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(wifi_detail_label, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_text_align(wifi_detail_label, LV_TEXT_ALIGN_CENTER, 0);

    update_network_status();
}

void ReadyScreen::update_network_status() {
    if (!wifi_status_label || !wifi_detail_label || !wifi_qr) return;

    ReadyScreenText status;
    ReadyScreenText detail;
    ReadyScreenText qr_payload;
#ifdef SMART_GRIND_SIM
    status = "WI-FI SIMULATED";
    detail = "smartgrind.local\n192.168.50.160";
#else
    switch (network_manager.state()) {
        case NetworkState::WIFI_DISABLED:
            status = "WI-FI DISABLED";
            detail = "Enable Wi-Fi in Settings.";
            break;
        case NetworkState::WIFI_NO_CREDENTIALS:
        case NetworkState::WIFI_SETUP_REQUIRED:
            status = "STARTING WI-FI SETUP";
            detail = "Preparing the setup network...";
            break;
        case NetworkState::WIFI_CONNECTING:
            status = "CONNECTING";
            detail = network_manager.network_name();
            break;
        case NetworkState::WIFI_CONNECTED:
            status = "WI-FI CONNECTED";
            detail = network_manager.network_name() + "\nhttp://" + network_manager.hostname() +
                     ".local\n" + network_manager.ip_address();
            break;
        case NetworkState::WIFI_RETRY_WAIT:
            status = "WI-FI INTERRUPTED";
            detail = "Reconnecting automatically...";
            break;
        case NetworkState::WIFI_SETUP_AP: {
            status = "CONNECT WI-FI";
            const String& ssid = provisioning_service.access_point_ssid();
            const String& password = provisioning_service.access_point_password();
            detail = "Scan with your phone, then use the sign-in page.\n" + ssid;
            qr_payload = "WIFI:T:";
            qr_payload += password.isEmpty() ? "nopass" : "WPA";
            qr_payload += ";S:" + escape_wifi_qr_value(ssid);
            if (!password.isEmpty()) qr_payload += ";P:" + escape_wifi_qr_value(password);
            qr_payload += ";;";
            break;
        }
    }
#endif

    const ReadyScreenText& rendered_status = status;
    const ReadyScreenText& rendered_detail = detail;
    const ReadyScreenText& rendered_qr = qr_payload;
    if (rendered_status != wifi_status_text) {
        lv_label_set_text(wifi_status_label, rendered_status.c_str());
        wifi_status_text = rendered_status;
    }
    if (rendered_detail != wifi_detail_text) {
        lv_label_set_text(wifi_detail_label, rendered_detail.c_str());
        wifi_detail_text = rendered_detail;
    }
    if (ready_text_empty(rendered_qr)) {
        lv_obj_add_flag(wifi_qr, LV_OBJ_FLAG_HIDDEN);
    } else {
        if (rendered_qr != wifi_qr_payload) {
            lv_qrcode_update(wifi_qr, rendered_qr.c_str(), static_cast<uint32_t>(rendered_qr.length()));
            wifi_qr_payload = rendered_qr;
        }
        lv_obj_clear_flag(wifi_qr, LV_OBJ_FLAG_HIDDEN);
    }
}

void ReadyScreen::create_profile_page(lv_obj_t* parent, int profile_index, const char* profile_name, float weight) {
    lv_obj_set_layout(parent, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(parent, 0, 0);

    lv_obj_t* name_label;
    (void)create_profile_label(parent, &name_label, &weight_labels[profile_index]);
    lv_label_set_text(name_label, profile_name);
    lv_obj_add_flag(name_label, LV_OBJ_FLAG_CLICKABLE);
    
    char weight_text[16];
    snprintf(weight_text, sizeof(weight_text), SYS_WEIGHT_DISPLAY_FORMAT, weight);
    lv_label_set_text(weight_labels[profile_index], weight_text);
    lv_obj_add_flag(weight_labels[profile_index], LV_OBJ_FLAG_CLICKABLE);
}

void ReadyScreen::create_menu_page(lv_obj_t* parent) {
    lv_obj_set_layout(parent, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(parent, 20, 0);

    // Info label
    lv_obj_t* info_label = lv_label_create(parent);
    lv_label_set_text(info_label, "MAIN\nMENU");
    lv_obj_set_style_text_font(info_label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(info_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_style_text_align(info_label, LV_TEXT_ALIGN_CENTER, 0);
}

void ReadyScreen::show() {
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_HIDDEN);
    visible = true;
}

void ReadyScreen::hide() {
    lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);
    visible = false;
}

void ReadyScreen::update_profile_values(const float values[3], GrindMode mode) {
    for (int i = 0; i < 3; i++) {
        if (weight_labels[i]) {
            char text[24];
            format_ready_value(text, sizeof(text), mode, values[i]);
            lv_label_set_text(weight_labels[i], text);
        }
    }
}

void ReadyScreen::set_active_tab(int tab) {
    if (tab >= 0 && tab < TAB_COUNT) {
        lv_tabview_set_act(tabview, tab, LV_ANIM_OFF);
    }
}

void ReadyScreen::set_profile_long_press_handler(lv_event_cb_t handler) {
    for (int i = 0; i < 3; i++) {
        if (weight_labels[i]) {
            lv_obj_add_event_cb(weight_labels[i], handler, LV_EVENT_LONG_PRESSED, NULL);
        }
    }
}
