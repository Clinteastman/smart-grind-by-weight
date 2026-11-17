#pragma once
#include <lvgl.h>
#include <cstdint>

// Abstract base class for grinding screen implementations
class IGrindingScreen {
public:
    virtual ~IGrindingScreen() = default;
    
    virtual void create() = 0;
    virtual void show() = 0;
    virtual void hide() = 0;
    virtual void update_profile_name(const char* name) = 0;
    virtual void update_target_weight(float weight) = 0;
    virtual void update_target_weight_text(const char* text) = 0;
    virtual void update_current_weight(float weight) = 0;
    virtual void update_tare_display() = 0;
    virtual void update_progress(int percent) = 0;
    virtual bool is_visible() const = 0;
    virtual lv_obj_t* get_screen() const = 0;
    
    // Chart-specific method (only implemented by chart screen)
    virtual void add_chart_data_point(float current_weight, float flow_rate, uint32_t current_time_ms) {}
};

enum class GrindScreenLayout {
    MINIMAL_ARC,   // Original arc-based screen
    NERDY_CHART    // Chart-based screen
};