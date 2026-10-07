#pragma once

#include <Preferences.h>
#include <algorithm>
#include <cstdint>

#include "../config/constants.h"

// Settings for the screen shown after a grind finishes, stored in the main
// preferences. Grind Settings edits them and the grinding controller applies
// them, so both share these keys, choices and defaults.
namespace result_screen_settings {

constexpr char kHoldFinalWeightKey[] = "result_hold";
constexpr char kSecondsKey[] = "result_time_s";

// Durations offered by the Grind Settings slider, shortest first.
constexpr uint16_t kSecondsChoices[] = {15, 30, 60, 120, 300};
constexpr int kChoiceCount = sizeof(kSecondsChoices) / sizeof(kSecondsChoices[0]);

// Slider position for a duration: the first choice at least that long.
inline int choice_for_seconds(uint32_t seconds) {
    for (int i = 0; i < kChoiceCount; ++i) {
        if (seconds <= kSecondsChoices[i]) return i;
    }
    return kChoiceCount - 1;
}

inline uint32_t seconds_for_choice(int choice) {
    return kSecondsChoices[std::clamp(choice, 0, kChoiceCount - 1)];
}

inline bool load_hold_final_weight(Preferences& prefs) {
    return prefs.getBool(kHoldFinalWeightKey, USER_RESULT_HOLD_FINAL_WEIGHT_DEFAULT);
}

// Always one of kSecondsChoices, whatever was stored.
inline uint32_t load_seconds(Preferences& prefs) {
    const uint32_t stored = prefs.getUInt(kSecondsKey, USER_RESULT_SCREEN_SECONDS_DEFAULT);
    return kSecondsChoices[choice_for_seconds(stored)];
}

}  // namespace result_screen_settings
