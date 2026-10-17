#include "ready_screen.h"
#include <Arduino.h>
#include "../../config/constants.h"
#include "../../controllers/grind_mode_traits.h"
#include "../event_bridge_lvgl.h"
#ifndef SMART_GRIND_SIM
#include <Preferences.h>
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

    build_tabs();
    visible = false;
}

void ReadyScreen::recalculate_indices() {
    int next_index = 0;
    if (tab_manual_enabled_) {
        manual_tab_index_ = next_index++;
    } else {
        manual_tab_index_ = -1;
    }

    for (int i = 0; i < 3; ++i) {
        if (profile_tab_enabled_[i]) {
            profile_tab_indices_[i] = next_index++;
        } else {
            profile_tab_indices_[i] = -1;
        }
    }

#ifdef SMART_GRIND_SIM
    has_wifi_tab_ = true;
#else
    has_wifi_tab_ = network_manager.should_show_tab();
#endif

    if (has_wifi_tab_) {
        wifi_tab_index_ = next_index++;
    } else {
        wifi_tab_index_ = -1;
    }

    menu_tab_index_ = next_index++;
    total_tab_count_ = next_index;
}

void ReadyScreen::set_tab_config(bool manual, bool single, bool double_dose, bool custom) {
    if (!manual && !single && !double_dose && !custom) {
        manual = true;
        single = true;
        double_dose = true;
        custom = true;
    }
    tab_manual_enabled_ = manual;
    profile_tab_enabled_[0] = single;
    profile_tab_enabled_[1] = double_dose;
    profile_tab_enabled_[2] = custom;
    recalculate_indices();
}

#ifndef SMART_GRIND_SIM
void ReadyScreen::load_tab_config(Preferences* prefs) {
    if (!prefs) return;
    const bool manual = prefs->getBool("tab_manual", true);
    const bool single = prefs->getBool("tab_single", true);
    const bool double_dose = prefs->getBool("tab_double", true);
    const bool custom = prefs->getBool("tab_custom", true);
    set_tab_config(manual, single, double_dose, custom);
    show_mode_icons_ = prefs->getBool("mode_icons", true);
}
#endif

void ReadyScreen::build_tabs() {
    if (tabview) {
        lv_obj_del(tabview);
        tabview = nullptr;
        wifi_tab = nullptr;
        wifi_status_label = nullptr;
        wifi_detail_label = nullptr;
        wifi_qr = nullptr;
        manual_tab = nullptr;
        manual_title_label = nullptr;
        manual_icon = nullptr;
        manual_weight_label = nullptr;
        manual_scale_status_label = nullptr;
        manual_tare_button = nullptr;
        manual_safety_label = nullptr;
        for (int i = 0; i < 3; ++i) {
            profile_tabs[i] = nullptr;
            profile_name_labels[i] = nullptr;
            profile_icons[i] = nullptr;
            weight_labels[i] = nullptr;
        }
        menu_tab = nullptr;
        wifi_status_text.clear();
        wifi_detail_text.clear();
        wifi_qr_payload.clear();
    }

    recalculate_indices();

    tabview = lv_tabview_create(screen);
    lv_obj_set_size(tabview, LV_PCT(100), LV_PCT(100));
    lv_obj_align(tabview, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(tabview, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
    lv_obj_add_flag(tabview, LV_OBJ_FLAG_GESTURE_BUBBLE);

    lv_obj_t* tab_content = lv_tabview_get_content(tabview);
    lv_obj_add_event_cb(tab_content, shorten_tab_scroll_animation, LV_EVENT_SCROLL_BEGIN, nullptr);

    lv_obj_t* tab_btns = lv_tabview_get_tab_btns(tabview);
    lv_obj_add_flag(tab_btns, LV_OBJ_FLAG_HIDDEN);

    lv_obj_set_style_bg_opa(tabview, LV_OPA_TRANSP, 0);

    if (manual_tab_index_ >= 0) {
        manual_tab = lv_tabview_add_tab(tabview, "Manual");
    } else {
        manual_tab = nullptr;
    }

    const char* tab_names[3] = {"Single", "Double", "Custom"};
    const char* profile_names[3] = {"SINGLE", "DOUBLE", "CUSTOM"};
    for (int i = 0; i < 3; ++i) {
        if (profile_tab_indices_[i] >= 0) {
            profile_tabs[i] = lv_tabview_add_tab(tabview, tab_names[i]);
        } else {
            profile_tabs[i] = nullptr;
        }
    }

    if (wifi_tab_index_ >= 0) {
        wifi_tab = lv_tabview_add_tab(tabview, "WI-FI");
    } else {
        wifi_tab = nullptr;
    }

    menu_tab = lv_tabview_add_tab(tabview, "MENU");

    if (manual_tab) {
        create_manual_page(manual_tab);
    }

    for (int i = 0; i < 3; i++) {
        if (profile_tabs[i]) {
            create_profile_page(profile_tabs[i], i, profile_names[i], cached_profile_values_[i]);
        }
    }

    if (has_wifi_tab_ && wifi_tab) {
        create_wifi_page(wifi_tab);
    }

    create_menu_page(menu_tab);

    if (profile_long_press_handler_) {
        set_profile_long_press_handler(profile_long_press_handler_);
    }
    if (manual_tare_handler_) {
        set_manual_tare_handler(manual_tare_handler_);
    }

    update_profile_values(cached_profile_values_, cached_grind_mode_);
    update_manual_scale(cached_manual_weight_, cached_manual_available_, cached_manual_taring_);
    update_single_dose_timeout(cached_single_dose_timeout_s_);
}

void ReadyScreen::rebuild_tabs() {
    build_tabs();
    if (visible) {
        show();
    } else {
        hide();
    }
}

void ReadyScreen::create_manual_page(lv_obj_t* parent) {
    lv_obj_set_layout(parent, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(parent, 4, 0);
    lv_obj_set_style_pad_gap(parent, 4, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    manual_icon = lv_image_create(parent);
    lv_image_set_src(manual_icon, &icon_mode_scale);
    lv_obj_set_style_image_recolor(manual_icon, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_style_image_recolor_opa(manual_icon, LV_OPA_COVER, 0);
    lv_obj_set_style_margin_bottom(manual_icon, 4, 0);

    manual_title_label = lv_label_create(parent);
    lv_label_set_text(manual_title_label, "SINGLE DOSE");
    lv_obj_set_style_text_font(manual_title_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(manual_title_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_style_text_align(manual_title_label, LV_TEXT_ALIGN_CENTER, 0);

    if (show_mode_icons_) {
        lv_obj_add_flag(manual_title_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(manual_icon, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(manual_title_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(manual_icon, LV_OBJ_FLAG_HIDDEN);
    }

    manual_weight_label = lv_label_create(parent);
    lv_label_set_text(manual_weight_label, "---");
    lv_obj_set_style_text_font(manual_weight_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(manual_weight_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_style_text_align(manual_weight_label, LV_TEXT_ALIGN_CENTER, 0);

    manual_scale_status_label = lv_label_create(parent);
    lv_label_set_text(manual_scale_status_label, "LIVE WEIGHT");
    lv_obj_set_style_text_font(manual_scale_status_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(manual_scale_status_label, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_text_align(manual_scale_status_label, LV_TEXT_ALIGN_CENTER, 0);

    manual_tare_button = create_button(parent, "TARE", lv_color_hex(THEME_COLOR_NEUTRAL), 120, 38, &lv_font_montserrat_16);

    manual_safety_label = lv_label_create(parent);
    lv_obj_set_style_text_font(manual_safety_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(manual_safety_label, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_text_align(manual_safety_label, LV_TEXT_ALIGN_CENTER, 0);
    update_single_dose_timeout(cached_single_dose_timeout_s_);
}

void ReadyScreen::update_manual_scale(float weight, bool available, bool taring) {
    cached_manual_weight_ = weight;
    cached_manual_available_ = available;
    cached_manual_taring_ = taring;

    if (!manual_weight_label || !manual_scale_status_label || !manual_tare_button) return;

    if (!available) {
        lv_label_set_text(manual_weight_label, "---");
        lv_label_set_text(manual_scale_status_label, "SCALE UNAVAILABLE");
        lv_obj_add_state(manual_tare_button, LV_STATE_DISABLED);
        return;
    }

    if (taring) {
        lv_label_set_text(manual_weight_label, "TARING");
        lv_label_set_text(manual_scale_status_label, "KEEP SCALE CLEAR");
        lv_obj_add_state(manual_tare_button, LV_STATE_DISABLED);
        return;
    }

    char weight_text[16];
    snprintf(weight_text, sizeof(weight_text), SYS_WEIGHT_DISPLAY_FORMAT, weight);
    lv_label_set_text(manual_weight_label, weight_text);
    lv_label_set_text(manual_scale_status_label, "LIVE WEIGHT");
    lv_obj_clear_state(manual_tare_button, LV_STATE_DISABLED);
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

    lv_obj_t* label_container = create_profile_label(parent, &profile_name_labels[profile_index], &weight_labels[profile_index]);
    lv_label_set_text(profile_name_labels[profile_index], profile_name);
    lv_obj_add_flag(profile_name_labels[profile_index], LV_OBJ_FLAG_CLICKABLE);

    const lv_image_dsc_t* icon_dsc = (profile_index == 0) ? &icon_mode_single :
                                     (profile_index == 1) ? &icon_mode_double :
                                                            &icon_mode_custom;
    profile_icons[profile_index] = lv_image_create(label_container);
    lv_image_set_src(profile_icons[profile_index], icon_dsc);
    lv_obj_set_style_image_recolor(profile_icons[profile_index], lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_style_image_recolor_opa(profile_icons[profile_index], LV_OPA_COVER, 0);
    lv_obj_set_style_margin_bottom(profile_icons[profile_index], 8, 0);
    lv_obj_add_flag(profile_icons[profile_index], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_to_index(profile_icons[profile_index], 0);

    if (show_mode_icons_) {
        lv_obj_add_flag(profile_name_labels[profile_index], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(profile_icons[profile_index], LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(profile_name_labels[profile_index], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(profile_icons[profile_index], LV_OBJ_FLAG_HIDDEN);
    }
    
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
    cached_grind_mode_ = mode;
    for (int i = 0; i < 3; i++) {
        cached_profile_values_[i] = values[i];
        if (profile_tabs[i]) {
            lv_obj_invalidate(profile_tabs[i]);
        }
        if (weight_labels[i]) {
            char text[24];
            format_ready_value(text, sizeof(text), mode, values[i]);
            lv_label_set_text(weight_labels[i], text);
        }
        if (profile_tabs[i]) {
            lv_obj_invalidate(profile_tabs[i]);
        }
    }
    if (screen) {
        lv_obj_invalidate(screen);
    }
}

void ReadyScreen::update_single_dose_timeout(uint32_t seconds) {
    cached_single_dose_timeout_s_ = seconds;
    if (!manual_safety_label) return;
    char buf[32];
    snprintf(buf, sizeof(buf), "%lu SECOND LIMIT", static_cast<unsigned long>(seconds));
    lv_label_set_text(manual_safety_label, buf);
}

void ReadyScreen::set_active_tab(int tab) {
    if (tab >= 0 && tab < tab_count()) {
        lv_tabview_set_act(tabview, tab, LV_ANIM_OFF);
    }
}

void ReadyScreen::set_profile_long_press_handler(lv_event_cb_t handler) {
    profile_long_press_handler_ = handler;
    for (int i = 0; i < 3; i++) {
        if (weight_labels[i]) {
            lv_obj_add_event_cb(weight_labels[i], handler, LV_EVENT_LONG_PRESSED, NULL);
        }
        if (profile_icons[i]) {
            lv_obj_add_event_cb(profile_icons[i], handler, LV_EVENT_LONG_PRESSED, NULL);
        }
        if (profile_name_labels[i]) {
            lv_obj_add_event_cb(profile_name_labels[i], handler, LV_EVENT_LONG_PRESSED, NULL);
        }
    }
}

void ReadyScreen::set_manual_tare_handler(lv_event_cb_t handler) {
    manual_tare_handler_ = handler;
    if (manual_tare_button && handler) {
        lv_obj_add_event_cb(manual_tare_button, handler, LV_EVENT_CLICKED,
                            reinterpret_cast<void*>(static_cast<intptr_t>(
                                EventBridgeLVGL::EventType::MANUAL_TARE)));
    }
}

void ReadyScreen::set_show_mode_icons(bool enabled) {
    show_mode_icons_ = enabled;
    for (int i = 0; i < 3; ++i) {
        if (profile_icons[i] && profile_name_labels[i]) {
            if (enabled) {
                lv_obj_clear_flag(profile_icons[i], LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(profile_name_labels[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(profile_icons[i], LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_flag(profile_name_labels[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
    if (manual_icon && manual_title_label) {
        if (enabled) {
            lv_obj_clear_flag(manual_icon, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(manual_title_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(manual_icon, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(manual_title_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
}
