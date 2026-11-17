#include <cassert>
#include <cstdint>
#include <cstring>

#include "network/wifi_join_failure.h"

namespace {

bool is_printable_ascii(const char* text) {
    for (; *text; ++text) {
        const auto value = static_cast<unsigned char>(*text);
        if (value < 0x20 || value > 0x7E) return false;
    }
    return true;
}

}  // namespace

int main() {
    using namespace wifi_disconnect_reason;

    static_assert(classify_wifi_disconnect(NONE) == WifiJoinFailure::NO_RESPONSE);
    static_assert(classify_wifi_disconnect(NO_AP_FOUND) == WifiJoinFailure::NETWORK_NOT_FOUND);
    static_assert(classify_wifi_disconnect(FOUR_WAY_HANDSHAKE_TIMEOUT) ==
                  WifiJoinFailure::PASSWORD_REJECTED);
    static_assert(classify_wifi_disconnect(AUTH_FAIL) == WifiJoinFailure::PASSWORD_REJECTED);
    static_assert(classify_wifi_disconnect(NO_AP_FOUND_IN_RSSI_THRESHOLD) ==
                  WifiJoinFailure::WEAK_SIGNAL);
    static_assert(classify_wifi_disconnect(CONNECTION_FAIL) ==
                  WifiJoinFailure::CONNECTION_REFUSED);
    static_assert(classify_wifi_disconnect(NO_AP_FOUND_W_COMPATIBLE_SECURITY) ==
                  WifiJoinFailure::UNSUPPORTED_SECURITY);
    static_assert(classify_wifi_disconnect(1) == WifiJoinFailure::OTHER);

    // A successful join has nothing to explain.
    assert(std::strlen(wifi_join_failure_summary(WifiJoinFailure::NONE)) == 0);
    assert(std::strlen(wifi_join_failure_advice(WifiJoinFailure::NONE)) == 0);

    // Every failure has text that the grinder's fonts can display.
    for (uint8_t value = static_cast<uint8_t>(WifiJoinFailure::NETWORK_NOT_FOUND);
         value <= static_cast<uint8_t>(WifiJoinFailure::OTHER); ++value) {
        const auto failure = static_cast<WifiJoinFailure>(value);
        assert(std::strlen(wifi_join_failure_summary(failure)) > 0);
        assert(std::strlen(wifi_join_failure_advice(failure)) > 0);
        assert(is_printable_ascii(wifi_join_failure_summary(failure)));
        assert(is_printable_ascii(wifi_join_failure_advice(failure)));
    }
    return 0;
}
