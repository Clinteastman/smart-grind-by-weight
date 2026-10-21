#pragma once
#include <lvgl.h>
#include "../../config/constants.h"
#include "grinding_screen_base.h"

struct DiscColorPresetDef {
    const char* id;
    const char* name;
    uint32_t color_hex;
};

static constexpr int NUM_DISC_COLOR_PRESETS = 5;
extern const DiscColorPresetDef DISC_COLOR_PRESETS[NUM_DISC_COLOR_PRESETS];

/**
 * GrindingScreenDisc - Expanding circular disc grind animation.
 * Features an expanding centered circular disc that grows with dose progress,
 * and transforms to a full solid green screen on completion.
 */
enum class DiscFinishStyle {
    ACCENT_RING,
    SOLID_GREEN
};

class GrindingScreenDisc : public IGrindingScreen {
private:
    lv_obj_t* screen;
    lv_obj_t* profile_label;
    lv_obj_t* target_label;
    lv_obj_t* disc_obj;
    lv_obj_t* accent_ring;
    lv_obj_t* weight_label;
    bool visible;
    bool time_mode;
    bool manual_mode;
    bool completed_;
    char displayed_weight_text[16];
    int displayed_progress;
    float target_time_seconds_;
    int color_preset_index_;
    uint32_t active_color_;
    DiscFinishStyle finish_style_;

public:
    GrindingScreenDisc();
    virtual ~GrindingScreenDisc();
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
    void set_completed(bool completed) override;
    void set_time_mode(bool enabled);
    void set_manual_mode(bool enabled);
    void set_color_preset(int preset_index);
    int get_color_preset() const { return color_preset_index_; }
    uint32_t get_active_color() const { return active_color_; }
    const DiscColorPresetDef& get_color_preset_def() const;

    void set_finish_style(DiscFinishStyle style) { finish_style_ = style; }
    DiscFinishStyle get_finish_style() const { return finish_style_; }

    bool is_visible() const override { return visible; }
    lv_obj_t* get_screen() const override { return screen; }
    lv_obj_t* get_disc_obj() const { return disc_obj; }
    lv_obj_t* get_weight_label() const { return weight_label; }
};
