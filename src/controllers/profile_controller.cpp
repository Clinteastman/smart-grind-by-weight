#include "profile_controller.h"
#include <Arduino.h>
#include <string.h>
#include <Preferences.h>

ProfileController::Snapshot ProfileController::snapshot() const {
    const auto lock = lock_profiles();
    Snapshot copy{};
    for (int i = 0; i < USER_PROFILE_COUNT; ++i) copy.profiles[i] = profiles[i];
    copy.current_profile = current_profile;
    copy.mode = current_grind_mode;
    return copy;
}

void ProfileController::init(Preferences* prefs) {
    const auto lock = lock_profiles();
    preferences = prefs;
    
    // Initialize default profiles
    strcpy(profiles[0].name, "SINGLE");
    profiles[0].weight = USER_SINGLE_ESPRESSO_WEIGHT_G;
    profiles[0].time_seconds = USER_SINGLE_ESPRESSO_TIME_S;
    
    strcpy(profiles[1].name, "DOUBLE");
    profiles[1].weight = USER_DOUBLE_ESPRESSO_WEIGHT_G;
    profiles[1].time_seconds = USER_DOUBLE_ESPRESSO_TIME_S;
    
    strcpy(profiles[2].name, "CUSTOM");
    profiles[2].weight = USER_CUSTOM_PROFILE_WEIGHT_G;
    profiles[2].time_seconds = USER_CUSTOM_PROFILE_TIME_S;
    
    // Initialize default grind mode
    current_grind_mode = GrindMode::WEIGHT;
    
    load_profiles();
}

void ProfileController::load_profiles() {
    const auto lock = lock_profiles();
    current_profile = preferences->getInt("profile", 1);
    
    profiles[0].weight = preferences->getFloat("weight0", USER_SINGLE_ESPRESSO_WEIGHT_G);
    profiles[1].weight = preferences->getFloat("weight1", USER_DOUBLE_ESPRESSO_WEIGHT_G);
    profiles[2].weight = preferences->getFloat("weight2", USER_CUSTOM_PROFILE_WEIGHT_G);

    profiles[0].time_seconds = preferences->getFloat("time0", USER_SINGLE_ESPRESSO_TIME_S);
    profiles[1].time_seconds = preferences->getFloat("time1", USER_DOUBLE_ESPRESSO_TIME_S);
    profiles[2].time_seconds = preferences->getFloat("time2", USER_CUSTOM_PROFILE_TIME_S);
    
    // Load grind mode (default to WEIGHT if not set)
    int stored_mode = preferences->getInt("grind_mode", static_cast<int>(GrindMode::WEIGHT));
    current_grind_mode = static_cast<GrindMode>(stored_mode);
    
    if (current_profile < 0 || current_profile >= USER_PROFILE_COUNT) {
        current_profile = 1;
    }
}

void ProfileController::save_profiles() {
    const auto lock = lock_profiles();
    preferences->putFloat("weight0", profiles[0].weight);
    preferences->putFloat("weight1", profiles[1].weight);
    preferences->putFloat("weight2", profiles[2].weight);

    preferences->putFloat("time0", profiles[0].time_seconds);
    preferences->putFloat("time1", profiles[1].time_seconds);
    preferences->putFloat("time2", profiles[2].time_seconds);
}

void ProfileController::save_current_profile() {
    const auto lock = lock_profiles();
    preferences->putInt("profile", current_profile);
    save_profiles();
}

void ProfileController::set_current_profile(int index) {
    const auto lock = lock_profiles();
    if (index >= 0 && index < USER_PROFILE_COUNT) {
        current_profile = index;
        save_current_profile();
    }
}

void ProfileController::set_profile_weight(int index, float weight) {
    const auto lock = lock_profiles();
    if (index >= 0 && index < USER_PROFILE_COUNT && is_weight_valid(weight)) {
        profiles[index].weight = weight;
        save_profiles();
    }
}

float ProfileController::get_profile_weight(int index) const {
    const auto lock = lock_profiles();
    if (index >= 0 && index < USER_PROFILE_COUNT) {
        return profiles[index].weight;
    }
    return 0.0f;
}

const char* ProfileController::get_profile_name(int index) const {
    const auto lock = lock_profiles();
    if (index >= 0 && index < USER_PROFILE_COUNT) {
        return profiles[index].name;
    }
    return "UNKNOWN";
}

void ProfileController::set_profile_time(int index, float seconds) {
    const auto lock = lock_profiles();
    if (index >= 0 && index < USER_PROFILE_COUNT && is_time_valid(seconds)) {
        profiles[index].time_seconds = seconds;
        save_profiles();
    }
}

float ProfileController::get_profile_time(int index) const {
    const auto lock = lock_profiles();
    if (index >= 0 && index < USER_PROFILE_COUNT) {
        return profiles[index].time_seconds;
    }
    return 0.0f;
}

bool ProfileController::apply_web_settings(int current_profile_index, GrindMode mode,
                                          const float* weights, const float* times) {
    const auto lock = lock_profiles();
    if (!preferences || !weights || !times || current_profile_index < 0 ||
        current_profile_index >= USER_PROFILE_COUNT ||
        (mode != GrindMode::WEIGHT && mode != GrindMode::TIME)) {
        return false;
    }
    for (int i = 0; i < USER_PROFILE_COUNT; ++i) {
        if (!is_weight_valid(weights[i]) || !is_time_valid(times[i])) return false;
    }

    current_profile = current_profile_index;
    current_grind_mode = mode;
    for (int i = 0; i < USER_PROFILE_COUNT; ++i) {
        profiles[i].weight = weights[i];
        profiles[i].time_seconds = times[i];
    }
    preferences->putInt("profile", current_profile);
    preferences->putInt("grind_mode", static_cast<int>(current_grind_mode));
    save_profiles();
    return true;
}

bool ProfileController::is_weight_valid(float weight) const {
    return weight >= USER_MIN_TARGET_WEIGHT_G && weight <= USER_MAX_TARGET_WEIGHT_G;
}

float ProfileController::clamp_weight(float weight) const {
    if (weight < USER_MIN_TARGET_WEIGHT_G) return USER_MIN_TARGET_WEIGHT_G;
    if (weight > USER_MAX_TARGET_WEIGHT_G) return USER_MAX_TARGET_WEIGHT_G;
    return weight;
}

void ProfileController::update_current_weight(float weight) {
    const auto lock = lock_profiles();
    if (is_weight_valid(weight)) {
        profiles[current_profile].weight = weight;
    }
}

void ProfileController::update_current_time(float seconds) {
    const auto lock = lock_profiles();
    if (is_time_valid(seconds)) {
        profiles[current_profile].time_seconds = seconds;
    }
}

bool ProfileController::is_time_valid(float seconds) const {
    return seconds >= USER_MIN_TARGET_TIME_S && seconds <= USER_MAX_TARGET_TIME_S;
}

float ProfileController::clamp_time(float seconds) const {
    if (seconds < USER_MIN_TARGET_TIME_S) return USER_MIN_TARGET_TIME_S;
    if (seconds > USER_MAX_TARGET_TIME_S) return USER_MAX_TARGET_TIME_S;
    return seconds;
}

void ProfileController::set_grind_mode(GrindMode mode) {
    const auto lock = lock_profiles();
    current_grind_mode = mode;
    save_grind_mode();
}

void ProfileController::save_grind_mode() {
    const auto lock = lock_profiles();
    preferences->putInt("grind_mode", static_cast<int>(current_grind_mode));
}
