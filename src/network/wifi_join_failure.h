#pragma once

#include <cstdint>

// Why the grinder could not join the configured Wi-Fi network, grouped by what
// the user can do about it. Free of Arduino/ESP-IDF headers so the mapping can
// be tested on a desktop.
enum class WifiJoinFailure : uint8_t {
    NONE,
    NETWORK_NOT_FOUND,
    PASSWORD_REJECTED,
    WEAK_SIGNAL,
    CONNECTION_REFUSED,
    UNSUPPORTED_SECURITY,
    NO_RESPONSE,
    OTHER,
};

// The ESP-IDF station disconnect reasons (wifi_err_reason_t) used below.
// network_manager.cpp checks them against the ESP-IDF definitions.
namespace wifi_disconnect_reason {
constexpr uint16_t NONE = 0;
constexpr uint16_t AUTH_EXPIRE = 2;
constexpr uint16_t ASSOC_LEAVE = 8;
constexpr uint16_t MIC_FAILURE = 14;
constexpr uint16_t FOUR_WAY_HANDSHAKE_TIMEOUT = 15;
constexpr uint16_t BEACON_TIMEOUT = 200;
constexpr uint16_t NO_AP_FOUND = 201;
constexpr uint16_t AUTH_FAIL = 202;
constexpr uint16_t ASSOC_FAIL = 203;
constexpr uint16_t HANDSHAKE_TIMEOUT = 204;
constexpr uint16_t CONNECTION_FAIL = 205;
constexpr uint16_t NO_AP_FOUND_W_COMPATIBLE_SECURITY = 210;
constexpr uint16_t NO_AP_FOUND_IN_AUTHMODE_THRESHOLD = 211;
constexpr uint16_t NO_AP_FOUND_IN_RSSI_THRESHOLD = 212;
}  // namespace wifi_disconnect_reason

// NONE means no disconnect was reported before the attempt timed out.
constexpr WifiJoinFailure classify_wifi_disconnect(uint16_t reason) {
    using namespace wifi_disconnect_reason;
    switch (reason) {
        case NONE:
            return WifiJoinFailure::NO_RESPONSE;
        case NO_AP_FOUND:
            return WifiJoinFailure::NETWORK_NOT_FOUND;
        case MIC_FAILURE:
        case FOUR_WAY_HANDSHAKE_TIMEOUT:
        case AUTH_FAIL:
        case HANDSHAKE_TIMEOUT:
            return WifiJoinFailure::PASSWORD_REJECTED;
        case BEACON_TIMEOUT:
        case NO_AP_FOUND_IN_RSSI_THRESHOLD:
            return WifiJoinFailure::WEAK_SIGNAL;
        case AUTH_EXPIRE:
        case ASSOC_FAIL:
        case CONNECTION_FAIL:
            return WifiJoinFailure::CONNECTION_REFUSED;
        case NO_AP_FOUND_W_COMPATIBLE_SECURITY:
        case NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
            return WifiJoinFailure::UNSUPPORTED_SECURITY;
        default:
            return WifiJoinFailure::OTHER;
    }
}

// Text is plain ASCII because the grinder's fonts only contain basic Latin.
constexpr const char* wifi_join_failure_summary(WifiJoinFailure failure) {
    switch (failure) {
        case WifiJoinFailure::NONE:
            return "";
        case WifiJoinFailure::NETWORK_NOT_FOUND:
            return "network not found";
        case WifiJoinFailure::PASSWORD_REJECTED:
            return "password rejected";
        case WifiJoinFailure::WEAK_SIGNAL:
            return "signal too weak";
        case WifiJoinFailure::CONNECTION_REFUSED:
            return "connection refused";
        case WifiJoinFailure::UNSUPPORTED_SECURITY:
            return "security type not supported";
        case WifiJoinFailure::NO_RESPONSE:
            return "no response";
        case WifiJoinFailure::OTHER:
            break;
    }
    return "connection failed";
}

constexpr const char* wifi_join_failure_advice(WifiJoinFailure failure) {
    switch (failure) {
        case WifiJoinFailure::NONE:
            return "";
        case WifiJoinFailure::NETWORK_NOT_FOUND:
            return "Check the name (capitals matter), that the network has 2.4 GHz "
                   "switched on, and that the grinder is in range.";
        case WifiJoinFailure::PASSWORD_REJECTED:
            return "Check the password and try again.";
        case WifiJoinFailure::WEAK_SIGNAL:
            return "Move the grinder closer to the access point.";
        case WifiJoinFailure::CONNECTION_REFUSED:
            return "The router did not accept the grinder. Its signal may be too weak, "
                   "or the router may block new devices.";
        case WifiJoinFailure::UNSUPPORTED_SECURITY:
            return "Use WPA2 or WPA3 Personal security for this network.";
        case WifiJoinFailure::NO_RESPONSE:
            return "Check that the network uses 2.4 GHz and that the grinder is in range.";
        case WifiJoinFailure::OTHER:
            break;
    }
    return "Check the network name and password, then try again.";
}
