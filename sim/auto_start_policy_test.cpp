// The simulator builds in Release mode, so make sure the checks stay compiled in.
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <cstdint>

#include "system/auto_start_policy.h"

namespace {

// A 410 g portafilter with the threshold set to 350 g, the setup that exposed
// the deadline in the previous sliding-window detector.
constexpr AutoStartSettings kSettings{350.0f, 1.0f, 1000};
constexpr uint32_t kStepMs = 100;

struct Scale {
    AutoStartTracker tracker;
    uint32_t now_ms = 0;
    bool armed = true;

    int feed(float weight_g) {
        now_ms += kStepMs;
        return tracker.observe(weight_g, now_ms, armed, kSettings) ? 1 : 0;
    }

    // Feed one reading per step for `duration_ms`; returns how many starts fired.
    int hold(float weight_g, uint32_t duration_ms) {
        int starts = 0;
        for (uint32_t t = 0; t < duration_ms; t += kStepMs) {
            starts += feed(weight_g);
        }
        return starts;
    }

    int ramp(float from_g, float to_g, uint32_t duration_ms) {
        int starts = 0;
        const uint32_t steps = duration_ms / kStepMs;
        for (uint32_t i = 1; i <= steps; ++i) {
            const float weight_g = from_g + (to_g - from_g) * static_cast<float>(i) / static_cast<float>(steps);
            starts += feed(weight_g);
        }
        return starts;
    }
};

void quick_placement_starts_once() {
    Scale scale;
    assert(scale.hold(0.0f, 3000) == 0);
    assert(scale.tracker.has_baseline());
    assert(scale.hold(410.0f, 3000) == 1);
    assert(scale.hold(410.0f, 10000) == 0);
}

void slow_placement_has_no_deadline() {
    Scale scale;
    assert(scale.hold(0.0f, 3000) == 0);
    // Twenty seconds of nudging the portafilter into place: never at rest.
    const float wobble[] = {380.0f, 395.0f, 405.0f, 390.0f, 402.0f};
    int starts = 0;
    for (int i = 0; i < 200; ++i) {
        starts += scale.feed(wobble[i % 5]);
    }
    assert(starts == 0);
    assert(scale.hold(410.0f, 3000) == 1);
}

void sensor_noise_does_not_block() {
    Scale scale;
    assert(scale.hold(0.0f, 3000) == 0);
    const float jitter[] = {0.3f, -0.2f, 0.1f, -0.3f, 0.2f, 0.0f};
    int starts = 0;
    for (int i = 0; i < 30; ++i) {
        starts += scale.feed(410.0f + jitter[i % 6]);
    }
    assert(starts == 1);
}

void knock_only_delays_the_start() {
    Scale scale;
    assert(scale.hold(0.0f, 3000) == 0);
    assert(scale.hold(410.0f, 600) == 0);
    assert(scale.feed(413.0f) == 0);      // knock during the hold
    assert(scale.hold(410.0f, 900) == 0); // hold restarted, not yet a second
    assert(scale.hold(410.0f, 200) == 1);
}

void cup_present_at_reset_is_the_baseline() {
    // OK pressed on the finished screen with the loaded portafilter still on.
    Scale scale;
    assert(scale.hold(428.0f, 3000) == 0);
    assert(scale.hold(428.0f, 5000) == 0);
    // Lift it off and set it down again.
    assert(scale.hold(0.0f, 3000) == 0);
    assert(scale.hold(410.0f, 3000) == 1);
}

void removal_never_starts() {
    Scale scale;
    assert(scale.hold(410.0f, 3000) == 0);
    assert(scale.hold(0.0f, 5000) == 0);
    assert(std::fabs(scale.tracker.baseline_g()) < 0.01f);
}

void slow_drift_moves_the_baseline() {
    Scale scale;
    assert(scale.hold(0.0f, 3000) == 0);
    assert(scale.ramp(0.0f, 50.0f, 100000) == 0);   // 0.5 g/s creep
    assert(scale.hold(50.0f, 3000) == 0);
    assert(scale.tracker.baseline_g() > 49.0f);
    assert(scale.hold(410.0f, 3000) == 1);          // 360 g over the drifted baseline
}

void detection_waits_for_rearm() {
    Scale scale;
    assert(scale.hold(0.0f, 3000) == 0);
    scale.armed = false;
    assert(scale.hold(410.0f, 3000) == 0);
    assert(std::fabs(scale.tracker.baseline_g()) < 0.01f);  // still waiting, not forgotten
    scale.armed = true;
    assert(scale.feed(410.0f) == 1);
}

void partial_load_counts_against_the_rise() {
    Scale scale;
    assert(scale.hold(0.0f, 3000) == 0);
    assert(scale.hold(100.0f, 3000) == 0);
    assert(scale.hold(460.0f, 3000) == 1);   // 360 g on top of the 100 g

    Scale other;
    assert(other.hold(0.0f, 3000) == 0);
    assert(other.hold(200.0f, 3000) == 0);
    assert(other.hold(410.0f, 3000) == 0);   // only 210 g more
}

void rise_below_threshold_moves_the_baseline() {
    Scale scale;
    assert(scale.hold(0.0f, 3000) == 0);
    assert(scale.hold(340.0f, 3000) == 0);
    assert(scale.hold(700.0f, 3000) == 1);
}

void invalid_readings_are_ignored() {
    Scale scale;
    assert(scale.hold(0.0f, 3000) == 0);
    assert(scale.feed(NAN) == 0);
    assert(scale.feed(INFINITY) == 0);
    assert(scale.hold(410.0f, 3000) == 1);
}

void reset_forgets_everything() {
    Scale scale;
    assert(scale.hold(0.0f, 3000) == 0);
    scale.tracker.reset();
    assert(!scale.tracker.has_baseline());
    assert(scale.hold(410.0f, 5000) == 0);   // becomes the baseline instead of starting
}

}  // namespace

int main() {
    quick_placement_starts_once();
    slow_placement_has_no_deadline();
    sensor_noise_does_not_block();
    knock_only_delays_the_start();
    cup_present_at_reset_is_the_baseline();
    removal_never_starts();
    slow_drift_moves_the_baseline();
    detection_waits_for_rearm();
    partial_load_counts_against_the_rise();
    rise_below_threshold_moves_the_baseline();
    invalid_readings_are_ignored();
    reset_forgets_everything();
    return 0;
}
