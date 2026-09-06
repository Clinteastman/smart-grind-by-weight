#pragma once
#include <Preferences.h>
#include "../config/constants.h"
#include "grind_mode.h"
#include <mutex>

struct Profile {
    char name[USER_PROFILE_NAME_MAX_LENGTH];
    float weight;
    float time_seconds;
};

class ProfileController {
private:
    mutable std::recursive_mutex mutex_;
    auto lock_profiles() const { return std::unique_lock<std::recursive_mutex>(mutex_); }
    Profile profiles[USER_PROFILE_COUNT];
    int current_profile;
    GrindMode current_grind_mode;
    Preferences* preferences;

public:
    struct Snapshot {
        Profile profiles[USER_PROFILE_COUNT];
        int current_profile;
        GrindMode mode;
    };
    Snapshot snapshot() const;
    void init(Preferences* prefs);
    void load_profiles();
    void save_profiles();
    void save_current_profile();
    
    void set_current_profile(int index);
    int get_current_profile() const { const auto lock = lock_profiles(); return current_profile; }
    float get_current_weight() const { const auto lock = lock_profiles(); return profiles[current_profile].weight; }
    float get_current_time() const { const auto lock = lock_profiles(); return profiles[current_profile].time_seconds; }
    // Names are fixed at initialization and never edited afterward.
    const char* get_current_name() const { const auto lock = lock_profiles(); return profiles[current_profile].name; }
    
    void set_profile_weight(int index, float weight);
    float get_profile_weight(int index) const;
    const char* get_profile_name(int index) const;
    void set_profile_time(int index, float seconds);
    float get_profile_time(int index) const;
    bool apply_web_settings(int current_profile_index, GrindMode mode,
                            const float* weights, const float* times);
    
    void update_current_weight(float weight);
    void update_current_time(float seconds);
    
    // Weight validation methods - single authority for all weight constraints
    bool is_weight_valid(float weight) const;
    float clamp_weight(float weight) const;

    bool is_time_valid(float seconds) const;
    float clamp_time(float seconds) const;
    
    // Grind mode persistence methods
    void set_grind_mode(GrindMode mode);
    GrindMode get_grind_mode() const { const auto lock = lock_profiles(); return current_grind_mode; }
    void save_grind_mode();
};
