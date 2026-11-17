#pragma once

#include <cmath>
#include <cstdint>

// Start-on-cup detector.
//
// Works on an averaged weight reading, so sensor noise and brief knocks do not
// block it. The scale counts as at rest once that reading has stayed within
// `rest_tolerance_g` of where it was for `rest_hold_ms`. The last at-rest
// weight is the baseline: a new at-rest weight at least `threshold_g` above it
// starts the grind, any other at-rest weight becomes the new baseline. There is
// no deadline, so a cup that takes a while to position still starts the grind
// the moment it comes to rest, while slow drift, partial loads and removals
// only move the baseline.
struct AutoStartSettings {
    float threshold_g;        // Rise over the baseline that starts a grind
    float rest_tolerance_g;   // Averaged weight may wander this much and still count as at rest
    uint32_t rest_hold_ms;    // How long it must stay inside that tolerance
};

class AutoStartTracker {
public:
    // Forget the baseline and the rest timing. The next at-rest weight becomes
    // the baseline, so a cup already on the scale does not start a grind.
    void reset() {
        baseline_valid_ = false;
        hold_valid_ = false;
    }

    bool has_baseline() const { return baseline_valid_; }
    float baseline_g() const { return baseline_g_; }

    // Feed one averaged reading. Returns true when a grind should start.
    // While `armed` is false a detected cup is held, not forgotten, so the
    // start happens as soon as the caller is armed again.
    bool observe(float weight_g, uint32_t now_ms, bool armed, const AutoStartSettings& settings) {
        if (!std::isfinite(weight_g)) {
            hold_valid_ = false;
            return false;
        }

        if (!hold_valid_ || std::fabs(weight_g - hold_anchor_g_) > settings.rest_tolerance_g) {
            hold_anchor_g_ = weight_g;
            hold_start_ms_ = now_ms;
            hold_valid_ = true;
            return false;
        }
        if (now_ms - hold_start_ms_ < settings.rest_hold_ms) {
            return false;
        }

        if (!baseline_valid_) {
            baseline_g_ = weight_g;
            baseline_valid_ = true;
            return false;
        }

        const bool cup_added = (weight_g - baseline_g_) >= settings.threshold_g;
        if (cup_added && !armed) {
            return false;
        }
        baseline_g_ = weight_g;
        return cup_added;
    }

private:
    float baseline_g_ = 0.0f;
    bool baseline_valid_ = false;
    float hold_anchor_g_ = 0.0f;
    uint32_t hold_start_ms_ = 0;
    bool hold_valid_ = false;
};
