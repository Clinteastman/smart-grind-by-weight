#pragma once
#include <lvgl.h>
#ifdef SMART_GRIND_SIM
#include <string>
using ReadyScreenText = std::string;
#else
#include <Arduino.h>
using ReadyScreenText = String;
#endif
#include "../../config/constants.h"
#include "../../controllers/grind_mode.h"
#include "../assets/mode_icons.h"

class ReadyScreen {
private:
    lv_obj_t* screen = nullptr;
    lv_obj_t* tabview = nullptr;
    lv_obj_t* manual_tab = nullptr;
    lv_obj_t* manual_title_label = nullptr;
    lv_obj_t* manual_icon = nullptr;
    lv_obj_t* manual_weight_label = nullptr;
    lv_obj_t* manual_scale_status_label = nullptr;
    lv_obj_t* manual_tare_button = nullptr;
    lv_obj_t* manual_safety_label = nullptr;
    lv_obj_t* profile_tabs[3]{};
    lv_obj_t* profile_name_labels[3]{};
    lv_obj_t* profile_icons[3]{};
    lv_obj_t* weight_labels[3]{};
    lv_obj_t* wifi_tab = nullptr;
    lv_obj_t* wifi_status_label = nullptr;
    lv_obj_t* wifi_detail_label = nullptr;
    lv_obj_t* wifi_qr = nullptr;
    lv_obj_t* menu_tab = nullptr;
    ReadyScreenText wifi_status_text;
    ReadyScreenText wifi_detail_text;
    ReadyScreenText wifi_qr_payload;
    bool visible = false;
    bool has_wifi_tab_ = false;

    float cached_profile_values_[3] = {USER_SINGLE_ESPRESSO_WEIGHT_G, USER_DOUBLE_ESPRESSO_WEIGHT_G, USER_CUSTOM_PROFILE_WEIGHT_G};
    GrindMode cached_grind_mode_ = GrindMode::WEIGHT;
    float cached_manual_weight_ = 0.0f;
    bool cached_manual_available_ = true;
    bool cached_manual_taring_ = false;
    uint32_t cached_single_dose_timeout_s_ = 45;
    lv_event_cb_t profile_long_press_handler_ = nullptr;
    lv_event_cb_t manual_tare_handler_ = nullptr;
    bool show_mode_icons_ = true;

    bool tab_manual_enabled_ = true;
    bool profile_tab_enabled_[3] = {true, true, true};
    int manual_tab_index_ = 0;
    int profile_tab_indices_[3] = {1, 2, 3};
    int wifi_tab_index_ = 4;
    int menu_tab_index_ = 5;
    int total_tab_count_ = 6;

public:
    static constexpr int MANUAL_TAB_INDEX = 0;
    static constexpr int PROFILE_TAB_START_INDEX = 1;
    static constexpr int PROFILE_TAB_COUNT = 3;

    bool is_manual_enabled() const { return tab_manual_enabled_; }
    bool is_profile_enabled(int profile) const {
        return profile >= 0 && profile < 3 ? profile_tab_enabled_[profile] : false;
    }
    void recalculate_indices();
    void set_tab_config(bool manual, bool single, bool double_dose, bool custom);
#ifndef SMART_GRIND_SIM
    void load_tab_config(class Preferences* prefs);
#endif

    bool is_manual_tab(int tab) const { return tab_manual_enabled_ && manual_tab_index_ >= 0 && tab == manual_tab_index_; }
    bool is_profile_tab(int tab) const {
        for (int i = 0; i < 3; ++i) {
            if (profile_tab_enabled_[i] && profile_tab_indices_[i] >= 0 && tab == profile_tab_indices_[i]) return true;
        }
        return false;
    }
    int profile_index_for_tab(int tab) const {
        for (int i = 0; i < 3; ++i) {
            if (profile_tab_enabled_[i] && profile_tab_indices_[i] >= 0 && tab == profile_tab_indices_[i]) return i;
        }
        return -1;
    }
    int tab_for_profile_index(int profile) const {
        if (profile >= 0 && profile < 3 && profile_tab_enabled_[profile] && profile_tab_indices_[profile] >= 0) {
            return profile_tab_indices_[profile];
        }
        for (int i = 0; i < 3; ++i) {
            if (profile_tab_enabled_[i] && profile_tab_indices_[i] >= 0) return profile_tab_indices_[i];
        }
        return (tab_manual_enabled_ && manual_tab_index_ >= 0) ? manual_tab_index_ : 0;
    }

    int manual_tab_index() const { return manual_tab_index_; }
    int wifi_tab_index() const { return wifi_tab_index_; }
    int menu_tab_index() const { return menu_tab_index_; }
    int tab_count() const { return total_tab_count_; }

    bool has_wifi_tab() const { return has_wifi_tab_; }
    bool is_wifi_tab(int tab) const { return has_wifi_tab_ && wifi_tab_index_ >= 0 && tab == wifi_tab_index_; }
    bool is_menu_tab(int tab) const { return menu_tab_index_ >= 0 && tab == menu_tab_index_; }

    void create();
    void build_tabs();
    void rebuild_tabs();
    void show();
    void hide();
    void update_profile_values(const float values[3], GrindMode mode);
    void update_manual_scale(float weight, bool available, bool taring);
    void update_single_dose_timeout(uint32_t seconds);
    void update_network_status();
    void set_active_tab(int tab);
    void set_profile_long_press_handler(lv_event_cb_t handler);
    void set_manual_tare_handler(lv_event_cb_t handler);
    void set_show_mode_icons(bool enabled);
    bool is_show_mode_icons() const { return show_mode_icons_; }
    
    bool is_visible() const { return visible; }
    lv_obj_t* get_screen() const { return screen; }
    lv_obj_t* get_tabview() const { return tabview; }
    lv_obj_t* get_menu_tab() const { return menu_tab; }
    
private:
    void create_profile_page(lv_obj_t* parent, int profile_index, const char* profile_name, float weight);
    void create_manual_page(lv_obj_t* parent);
    void create_wifi_page(lv_obj_t* parent);
    void create_menu_page(lv_obj_t* parent);
};
