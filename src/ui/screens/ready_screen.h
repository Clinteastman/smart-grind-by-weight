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

class ReadyScreen {
private:
    lv_obj_t* screen;
    lv_obj_t* tabview;
    lv_obj_t* manual_tab;
    lv_obj_t* profile_tabs[3];
    lv_obj_t* weight_labels[3];
    lv_obj_t* wifi_tab;
    lv_obj_t* wifi_status_label;
    lv_obj_t* wifi_detail_label;
    lv_obj_t* wifi_qr;
    lv_obj_t* menu_tab;
    ReadyScreenText wifi_status_text;
    ReadyScreenText wifi_detail_text;
    ReadyScreenText wifi_qr_payload;
    bool visible;

public:
    static constexpr int MANUAL_TAB_INDEX = 0;
    static constexpr int PROFILE_TAB_START_INDEX = 1;
    static constexpr int PROFILE_TAB_COUNT = 3;
    static constexpr int WIFI_TAB_INDEX = 4;
    static constexpr int MENU_TAB_INDEX = 5;
    static constexpr int TAB_COUNT = 6;

    static bool is_profile_tab(int tab) {
        return tab >= PROFILE_TAB_START_INDEX &&
               tab < PROFILE_TAB_START_INDEX + PROFILE_TAB_COUNT;
    }
    static int profile_index_for_tab(int tab) { return tab - PROFILE_TAB_START_INDEX; }
    static int tab_for_profile_index(int profile) { return profile + PROFILE_TAB_START_INDEX; }

    void create();
    void show();
    void hide();
    void update_profile_values(const float values[3], GrindMode mode);
    void update_network_status();
    void set_active_tab(int tab);
    void set_profile_long_press_handler(lv_event_cb_t handler);
    
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
