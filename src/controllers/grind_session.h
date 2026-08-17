#pragma once

#include "grind_mode.h"
#include <cstdint>

struct GrindSessionDescriptor {
    GrindMode mode = GrindMode::WEIGHT;
    uint8_t finish_mode = 0;         // GrindFinishMode value for this session
    float target_weight = 0.0f;      // grams
    uint32_t target_time_ms = 0;     // milliseconds
    float tolerance = 0.0f;          // grams
    uint8_t profile_id = 0;          // active profile index
};
