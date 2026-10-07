#pragma once
#include <lvgl.h>
#include "../../config/constants.h"
#include "grinding_screen_base.h"

// Mahlkonig E64 style: a light circle around the weight grows with grind
// progress until it covers the display, which then turns green when the
// grind completes (amber if it fails).
class GrindingScreenCircle : public IGrindingScreen {
private:
    lv_obj_t* screen;
    lv_obj_t* circle;
    lv_obj_t* profile_label;
    lv_obj_t* weight_label;
    lv_obj_t* target_label;
    bool visible;
    bool time_mode;
    float target_time_seconds_;
    int displayed_progress;
    GrindScreenOutcome outcome;
    lv_color_t corner_color;  // Display background shown outside the circle
    char displayed_weight_text[16];
    int16_t diameter_by_percent[101];

    void build_diameter_table();
    int32_t diameter_for_progress(int percent) const;
    lv_obj_t* create_label(const lv_font_t* font);
    void apply_colors(bool show_result);
    void show_result_colors();
    void resize_circle(int32_t diameter, bool animate);
    static void set_circle_diameter(void* circle, int32_t diameter);

public:
    void create() override;
    void show() override;
    void hide() override;
    void update_profile_name(const char* name) override;
    void update_target_weight(float weight) override;
    void update_target_weight_text(const char* text) override;
    void update_target_time(float seconds);
    void update_current_weight(float weight) override;
    void update_tare_display() override;
    void update_progress(int percent) override;
    void set_outcome(GrindScreenOutcome new_outcome) override;
    void set_time_mode(bool enabled);

    bool is_visible() const override { return visible; }
    lv_obj_t* get_screen() const override { return screen; }
    lv_obj_t* get_circle() const { return circle; }
};
