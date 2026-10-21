#pragma once
#include "grinding_screen_base.h"
#include "grinding_screen_arc.h"
#include "grinding_screen_chart.h"
#include "grinding_screen_disc.h"
#include <Preferences.h>
#include "../../controllers/grind_mode.h"

// Unified grinding screen that wraps all screen implementations
class GrindingScreen : public IGrindingScreen {
private:
    IGrindingScreen* active_screen;
    GrindScreenLayout current_layout;
    GrindingScreenArc arc_screen;
    GrindingScreenChart chart_screen;
    GrindingScreenDisc disc_screen;
    Preferences* preferences;
    GrindMode current_mode;
    
public:
    GrindingScreen();
    void init(Preferences* prefs);
    void set_layout(GrindScreenLayout layout);
    GrindScreenLayout get_layout() const { return current_layout; }
    bool is_disc_layout() const {
        return current_layout == GrindScreenLayout::DISC_RING ||
               current_layout == GrindScreenLayout::DISC_SOLID;
    }
    
    // IGrindingScreen implementation - delegates to active screen
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
    bool is_visible() const override;
    lv_obj_t* get_screen() const override;
    void add_chart_data_point(float current_weight, float flow_rate, uint32_t current_time_ms) override;
    void reset_chart_data();
    void set_mode(GrindMode mode);
    void set_chart_time_prediction(uint32_t predicted_time_ms);

    // Public accessors for individual screen objects
    lv_obj_t* get_arc_screen_obj() const { return arc_screen.get_screen(); }
    lv_obj_t* get_chart_screen_obj() const { return chart_screen.get_screen(); }
    lv_obj_t* get_disc_screen_obj() const { return disc_screen.get_screen(); }
    void set_disc_color_preset(int preset_index);
    int get_disc_color_preset() const { return disc_screen.get_color_preset(); }
    const DiscColorPresetDef& get_disc_color_preset_def() const { return disc_screen.get_color_preset_def(); }
};
