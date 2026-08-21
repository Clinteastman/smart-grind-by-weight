#include <cassert>
#include <cstdint>

#include "system/display_idle_policy.h"

int main() {
    constexpr uint32_t screensaver_ms = 300000;
    constexpr uint32_t off_delay_ms = 3600000;

    assert(display_idle_state(0, screensaver_ms, false, off_delay_ms) ==
           DisplayIdleState::ACTIVE);
    assert(display_idle_state(screensaver_ms, screensaver_ms, false, off_delay_ms) ==
           DisplayIdleState::SCREENSAVER);
    assert(display_idle_state(UINT32_C(24) * 60 * 60 * 1000, screensaver_ms, false,
                              off_delay_ms) == DisplayIdleState::SCREENSAVER);

    assert(display_idle_state(screensaver_ms - 1, screensaver_ms, true, off_delay_ms) ==
           DisplayIdleState::ACTIVE);
    assert(display_idle_state(screensaver_ms, screensaver_ms, true, off_delay_ms) ==
           DisplayIdleState::SCREENSAVER);
    assert(display_idle_state(screensaver_ms + off_delay_ms - 1, screensaver_ms, true,
                              off_delay_ms) == DisplayIdleState::SCREENSAVER);
    assert(display_idle_state(screensaver_ms + off_delay_ms, screensaver_ms, true,
                              off_delay_ms) == DisplayIdleState::PANEL_OFF);
    return 0;
}
