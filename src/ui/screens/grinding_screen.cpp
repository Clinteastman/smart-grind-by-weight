#include "grinding_screen.h"
#include <Preferences.h>

GrindingScreen::GrindingScreen() : current_layout(GrindScreenLayout::MINIMAL_ARC), preferences(nullptr), current_mode(GrindMode::WEIGHT) {
    // Layout will be loaded in init() when preferences are available
    active_screen = (IGrindingScreen*)&arc_screen; // Default to arc screen
}

void GrindingScreen::init(Preferences* prefs) {
    preferences = prefs;

    // Load saved layout preference using the provided preferences instance
    current_layout = GrindScreenLayout::MINIMAL_ARC;
    if (preferences && preferences->isKey("grind_layout")) {
        const int saved_layout = preferences->getInt("grind_layout", (int)GrindScreenLayout::MINIMAL_ARC);
        if (saved_layout >= 0 && saved_layout < (int)GrindScreenLayout::COUNT) {
            current_layout = (GrindScreenLayout)saved_layout;
        }
    }

    // Set active screen based on loaded layout
    active_screen = screen_for_layout(current_layout);
}

IGrindingScreen* GrindingScreen::screen_for_layout(GrindScreenLayout layout) {
    switch (layout) {
        case GrindScreenLayout::NERDY_CHART:
            return &chart_screen;
        case GrindScreenLayout::CIRCLE:
            return &circle_screen;
        case GrindScreenLayout::MINIMAL_ARC:
        default:
            return &arc_screen;
    }
}

void GrindingScreen::set_layout(GrindScreenLayout layout) {
    if (current_layout == layout) return;

    bool was_visible = is_visible();

    // Hide current screen
    if (active_screen) {
        active_screen->hide();
    }

    // Switch to new layout
    current_layout = layout;
    active_screen = screen_for_layout(layout);

    // Show new screen if it was visible
    if (was_visible) {
        active_screen->show();
    }

    // Save preference using the provided preferences instance
    if (preferences) {
        preferences->putInt("grind_layout", (int)layout);
    }
}

void GrindingScreen::cycle_layout() {
    const int next = ((int)current_layout + 1) % (int)GrindScreenLayout::COUNT;
    set_layout((GrindScreenLayout)next);
}

// Delegate all calls to active screen
void GrindingScreen::create() {
    arc_screen.create();
    chart_screen.create();
    circle_screen.create();
    arc_screen.set_time_mode(current_mode == GrindMode::TIME);
    chart_screen.set_time_mode(current_mode == GrindMode::TIME);
    circle_screen.set_time_mode(current_mode == GrindMode::TIME);

    // Hide the inactive screens initially
    IGrindingScreen* layouts[] = {&arc_screen, &chart_screen, &circle_screen};
    for (IGrindingScreen* layout : layouts) {
        if (layout != active_screen) layout->hide();
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
    circle_screen.update_profile_name(name);
}

void GrindingScreen::update_target_weight(float weight) {
    arc_screen.update_target_weight(weight);
    chart_screen.update_target_weight(weight);
    circle_screen.update_target_weight(weight);
}

void GrindingScreen::update_target_weight_text(const char* text) {
    arc_screen.update_target_weight_text(text);
    chart_screen.update_target_weight_text(text);
    circle_screen.update_target_weight_text(text);
}

void GrindingScreen::update_target_time(float seconds) {
    arc_screen.update_target_time(seconds);
    chart_screen.update_target_time(seconds);
    circle_screen.update_target_time(seconds);
}

void GrindingScreen::update_current_weight(float weight) {
    arc_screen.update_current_weight(weight);
    chart_screen.update_current_weight(weight);
    circle_screen.update_current_weight(weight);
}

void GrindingScreen::update_tare_display() {
    arc_screen.update_tare_display();
    chart_screen.update_tare_display();
    circle_screen.update_tare_display();
}

void GrindingScreen::update_progress(int percent) {
    arc_screen.update_progress(percent);
    chart_screen.update_progress(percent);
    circle_screen.update_progress(percent);
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

void GrindingScreen::set_outcome(GrindScreenOutcome outcome) {
    // Only the circle colours the result; keep it current while hidden too.
    circle_screen.set_outcome(outcome);
}

void GrindingScreen::reset_chart_data() {
    chart_screen.reset_chart_data();
}

void GrindingScreen::set_mode(GrindMode mode) {
    current_mode = mode;
    bool time_enabled = (mode == GrindMode::TIME);
    arc_screen.set_time_mode(time_enabled);
    chart_screen.set_time_mode(time_enabled);
    circle_screen.set_time_mode(time_enabled);
}

void GrindingScreen::set_chart_time_prediction(uint32_t predicted_time_ms) {
    chart_screen.set_chart_time_prediction(predicted_time_ms);
}
