#pragma once

#include <cstdint>
#include <cmath>

enum class DisplayIdleState : uint8_t {
    ACTIVE,
    SCREENSAVER,
    PANEL_OFF,
};

constexpr DisplayIdleState display_idle_state(uint32_t inactive_ms,
                                               uint32_t screensaver_timeout_ms,
                                               bool panel_off_enabled,
                                               uint32_t panel_off_delay_ms) {
    if (inactive_ms < screensaver_timeout_ms) {
        return DisplayIdleState::ACTIVE;
    }
    if (panel_off_enabled &&
        inactive_ms >= screensaver_timeout_ms + panel_off_delay_ms) {
        return DisplayIdleState::PANEL_OFF;
    }
    return DisplayIdleState::SCREENSAVER;
}

inline bool display_weight_activity_detected(float delta_g, float threshold_g) {
    return std::isfinite(delta_g) && std::isfinite(threshold_g) &&
           threshold_g > 0.0f && std::fabs(delta_g) >= threshold_g;
}
