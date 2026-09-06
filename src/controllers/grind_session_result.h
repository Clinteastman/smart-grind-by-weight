#pragma once

enum class GrindSessionResult {
    UNKNOWN,
    SUCCESS,
    OVERSHOOT,
    MAX_PULSES,
    TIMEOUT,
    ERROR,
    SCALE_ERROR
};

constexpr bool is_completed_grind_result(GrindSessionResult result) {
    return result == GrindSessionResult::SUCCESS ||
           result == GrindSessionResult::OVERSHOOT ||
           result == GrindSessionResult::MAX_PULSES;
}
