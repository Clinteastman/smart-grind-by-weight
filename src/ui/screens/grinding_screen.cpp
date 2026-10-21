#include "grinding_screen.h"
#include <Preferences.h>

GrindingScreen::GrindingScreen() : current_layout(GrindScreenLayout::MINIMAL_ARC), preferences(nullptr), current_mode(GrindMode::WEIGHT) {
    // Layout will be loaded in init() when preferences are available
    active_screen = (IGrindingScreen*)&arc_screen; // Default to arc screen
}

void GrindingScreen::init(Preferences* prefs) {
    preferences = prefs;
    
    // Load saved layout preference using the provided preferences instance
    if (preferences && preferences->isKey("grind_layout")) {
        int saved_layout = preferences->getInt("grind_layout", (int)GrindScreenLayout::MINIMAL_ARC);
        if (saved_layout < 0 || saved_layout > (int)GrindScreenLayout::NERDY_CHART) {
            saved_layout = (int)GrindScreenLayout::MINIMAL_ARC;
        }
        current_layout = (GrindScreenLayout)saved_layout;
    } else {
        current_layout = GrindScreenLayout::MINIMAL_ARC;
    }
    
    if (current_layout == GrindScreenLayout::DISC_RING) {
        disc_screen.set_finish_style(DiscFinishStyle::ACCENT_RING);
        active_screen = (IGrindingScreen*)&disc_screen;
    } else if (current_layout == GrindScreenLayout::DISC_SOLID) {
        disc_screen.set_finish_style(DiscFinishStyle::SOLID_GREEN);
        active_screen = (IGrindingScreen*)&disc_screen;
    } else if (current_layout == GrindScreenLayout::NERDY_CHART) {
        active_screen = (IGrindingScreen*)&chart_screen;
    } else {
        active_screen = (IGrindingScreen*)&arc_screen;
    }

    if (preferences && preferences->isKey("disc_color")) {
        int saved_color = preferences->getInt("disc_color", 0);
        if (saved_color >= 0 && saved_color < NUM_DISC_COLOR_PRESETS) {
            disc_screen.set_color_preset(saved_color);
        }
    }
}

void GrindingScreen::set_layout(GrindScreenLayout layout) {
    if (current_layout == layout && active_screen) return;
    
    bool was_visible = is_visible();
    
    // Hide current screen
    if (active_screen) {
        active_screen->hide();
    }
    
    // Switch to new layout
    current_layout = layout;
    if (layout == GrindScreenLayout::DISC_RING) {
        disc_screen.set_finish_style(DiscFinishStyle::ACCENT_RING);
        active_screen = (IGrindingScreen*)&disc_screen;
    } else if (layout == GrindScreenLayout::DISC_SOLID) {
        disc_screen.set_finish_style(DiscFinishStyle::SOLID_GREEN);
        active_screen = (IGrindingScreen*)&disc_screen;
    } else if (layout == GrindScreenLayout::NERDY_CHART) {
        active_screen = (IGrindingScreen*)&chart_screen;
    } else {
        active_screen = (IGrindingScreen*)&arc_screen;
    }
    
    // Show new screen if it was visible
    if (was_visible) {
        active_screen->show();
    }
    
    // Save preference using the provided preferences instance
    if (preferences) {
        preferences->putInt("grind_layout", (int)layout);
    }
}

// Delegate all calls to active screen
void GrindingScreen::create() {
    arc_screen.create();
    chart_screen.create();
    disc_screen.create();
    arc_screen.set_time_mode(current_mode == GrindMode::TIME);
    chart_screen.set_time_mode(current_mode == GrindMode::TIME);
    disc_screen.set_time_mode(current_mode == GrindMode::TIME);
    
    arc_screen.hide();
    chart_screen.hide();
    disc_screen.hide();
    if (active_screen) {
        active_screen->show();
    }
}

void GrindingScreen::show() {
    if (active_screen) active_screen->show();
}

void GrindingScreen::hide() {
    if (active_screen) active_screen->hide();
}

void GrindingScreen::update_profile_name(const char* name) {
    arc_screen.update_profile_name(name);
    chart_screen.update_profile_name(name);
    disc_screen.update_profile_name(name);
}

void GrindingScreen::update_target_weight(float weight) {
    arc_screen.update_target_weight(weight);
    chart_screen.update_target_weight(weight);
    disc_screen.update_target_weight(weight);
}

void GrindingScreen::update_target_weight_text(const char* text) {
    arc_screen.update_target_weight_text(text);
    chart_screen.update_target_weight_text(text);
    disc_screen.update_target_weight_text(text);
}

void GrindingScreen::update_target_time(float seconds) {
    arc_screen.update_target_time(seconds);
    chart_screen.update_target_time(seconds);
    disc_screen.update_target_time(seconds);
}

void GrindingScreen::update_current_weight(float weight) {
    arc_screen.update_current_weight(weight);
    chart_screen.update_current_weight(weight);
    disc_screen.update_current_weight(weight);
}

void GrindingScreen::update_tare_display() {
    arc_screen.update_tare_display();
    chart_screen.update_tare_display();
    disc_screen.update_tare_display();
}

void GrindingScreen::update_progress(int percent) {
    arc_screen.update_progress(percent);
    chart_screen.update_progress(percent);
    disc_screen.update_progress(percent);
}

void GrindingScreen::set_completed(bool completed) {
    arc_screen.set_completed(completed);
    chart_screen.set_completed(completed);
    disc_screen.set_completed(completed);
}

bool GrindingScreen::is_visible() const {
    return active_screen ? active_screen->is_visible() : false;
}

lv_obj_t* GrindingScreen::get_screen() const {
    return active_screen ? active_screen->get_screen() : nullptr;
}

void GrindingScreen::add_chart_data_point(float current_weight, float flow_rate, uint32_t current_time_ms) {
    // Always send data points to the chart screen instance,
    // even when it is not the active_screen. This ensures data is
    // collected in the background.
    chart_screen.add_chart_data_point(current_weight, flow_rate, current_time_ms);
}

void GrindingScreen::reset_chart_data() {
    chart_screen.reset_chart_data();
}

void GrindingScreen::set_mode(GrindMode mode) {
    current_mode = mode;
    bool time_enabled = (mode == GrindMode::TIME);
    bool manual_enabled = (mode == GrindMode::MANUAL);
    arc_screen.set_time_mode(time_enabled);
    chart_screen.set_time_mode(time_enabled);
    disc_screen.set_time_mode(time_enabled);
    arc_screen.set_manual_mode(manual_enabled);
    disc_screen.set_manual_mode(manual_enabled);
}

void GrindingScreen::set_chart_time_prediction(uint32_t predicted_time_ms) {
    chart_screen.set_chart_time_prediction(predicted_time_ms);
}

void GrindingScreen::set_disc_color_preset(int preset_index) {
    disc_screen.set_color_preset(preset_index);
    if (preferences) {
        preferences->putInt("disc_color", preset_index);
    }
}
