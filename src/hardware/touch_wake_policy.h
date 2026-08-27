#pragma once

#include <cstdint>

constexpr uint32_t kWakeTouchGuardTimeoutMs = 1000U;

constexpr bool wake_touch_guard_expired(uint32_t elapsed_ms) {
    return elapsed_ms >= kWakeTouchGuardTimeoutMs;
}
