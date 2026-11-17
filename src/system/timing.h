#pragma once

#include <stdint.h>

#ifdef SMART_GRIND_SIM
#include <chrono>
#else
#include <esp_timer.h>
#endif

//==============================================================================
// MONOTONIC CLOCK HELPERS
//==============================================================================
// The grind controller, load-cell sampler and UI all schedule themselves against
// a monotonic millisecond counter and rely on unsigned wraparound arithmetic
// (`now - started >= interval`). These wrappers keep that idiom in one place.
//
// On device both counters come from esp_timer, which runs off a 64-bit hardware
// timer and is unaffected by the CPU frequency scaling the BLE OTA path uses.
// The desktop simulator maps them onto std::chrono::steady_clock.

namespace timing_detail {

inline int64_t microseconds_since_boot() {
#ifdef SMART_GRIND_SIM
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
#else
    return esp_timer_get_time();
#endif
}

}  // namespace timing_detail

/**
 * Milliseconds since boot, truncated to 32 bits.
 *
 * Wraps every ~49.7 days. Always compare elapsed intervals as
 * `now - previous >= interval` so a wrap cannot produce a negative duration.
 */
inline uint32_t millis() {
    return static_cast<uint32_t>(timing_detail::microseconds_since_boot() / 1000);
}

/**
 * Microseconds since boot, truncated to 32 bits.
 *
 * Wraps every ~71.6 minutes; only use it for short interval measurements such as
 * the display flush and render timings.
 */
inline uint32_t micros() {
    return static_cast<uint32_t>(timing_detail::microseconds_since_boot());
}
