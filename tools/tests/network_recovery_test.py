"""Run the production Wi-Fi state machine, setup-mode retry and driver setup against ESP-IDF fakes."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from controller_serialization_test import function

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "src/network/network_manager.cpp").read_text()

HARNESS = r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0, ESP_FAIL = -1;
#define LOG_BLE(...) std::printf(__VA_ARGS__)
const char* esp_err_to_name(esp_err_t) { return "error"; }
uint32_t now_ms = 1000;
uint32_t millis() { return now_ms; }

// ---- esp_netif / esp_wifi / esp_event ----
struct esp_netif_t {} station, access_point;
int station_creates = 0, access_point_creates = 0;
esp_netif_t* esp_netif_create_default_wifi_sta() { ++station_creates; return &station; }
esp_netif_t* esp_netif_create_default_wifi_ap() { ++access_point_creates; return &access_point; }
struct wifi_init_config_t {};
#define WIFI_INIT_CONFIG_DEFAULT() wifi_init_config_t{}
int driver_inits = 0, driver_init_failures = 0;
esp_err_t esp_wifi_init(const wifi_init_config_t*) {
    ++driver_inits;
    return driver_init_failures-- > 0 ? ESP_FAIL : ESP_OK;
}
enum wifi_storage_t { WIFI_STORAGE_RAM };
esp_err_t esp_wifi_set_storage(wifi_storage_t) { return ESP_OK; }
int disconnects = 0, connects = 0;
esp_err_t esp_wifi_connect() { ++connects; return ESP_OK; }
enum wifi_mode_t { WIFI_MODE_STA = 1, WIFI_MODE_APSTA = 3 };
wifi_mode_t mode = WIFI_MODE_APSTA;
esp_err_t esp_wifi_set_mode(wifi_mode_t next) { mode = next; return ESP_OK; }
// Defined after the class: the driver's DISCONNECTED event is delivered to
// the production handler from inside the disconnect call, as the event task
// on the other core can do before the caller runs its next statement.
esp_err_t esp_wifi_disconnect();
constexpr int32_t WIFI_EVENT_STA_START = 2, WIFI_EVENT_STA_DISCONNECTED = 5,
                  WIFI_EVENT_AP_STACONNECTED = 14, WIFI_EVENT_AP_STADISCONNECTED = 15;
struct wifi_event_sta_disconnected_t {
    uint8_t ssid[32]; uint8_t ssid_len; uint8_t bssid[6]; uint8_t reason; int8_t rssi;
};
constexpr uint8_t WIFI_REASON_AUTH_EXPIRE = 2, WIFI_REASON_DISASSOC_DUE_TO_INACTIVITY = 4,
                  WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT = 15, WIFI_REASON_BEACON_TIMEOUT = 200,
                  WIFI_REASON_NO_AP_FOUND = 201, WIFI_REASON_AUTH_FAIL = 202,
                  WIFI_REASON_ASSOC_FAIL = 203, WIFI_REASON_HANDSHAKE_TIMEOUT = 204,
                  WIFI_REASON_CONNECTION_FAIL = 205,
                  WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY = 210,
                  WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD = 211;
using esp_event_base_t = const char*;
using esp_event_handler_t = void (*)(void*, esp_event_base_t, int32_t, void*);
using esp_event_handler_instance_t = void*;
const char* WIFI_EVENT = "WIFI_EVENT";
const char* IP_EVENT = "IP_EVENT";
constexpr int32_t ESP_EVENT_ANY_ID = -1, IP_EVENT_STA_GOT_IP = 0;
int wifi_registrations = 0, ip_registrations = 0, ip_registration_failures = 0;
int instance_storage = 0;
esp_err_t esp_event_handler_instance_register(esp_event_base_t base, int32_t, esp_event_handler_t,
                                              void*, esp_event_handler_instance_t* instance) {
    if (base == IP_EVENT) {
        ++ip_registrations;
        if (ip_registration_failures-- > 0) return ESP_FAIL;
    } else {
        ++wifi_registrations;
    }
    *instance = &instance_storage;
    return ESP_OK;
}

// ---- the production disconnect-reason wording ----
''' + re.search(r"constexpr int8_t kNoRssi = [^;]+;", SOURCE).group(0) + "\n" \
    + function(SOURCE, "const char* disconnect_reason_text(") + r'''

// ---- SmartGrindNetworkManager members the production code touches ----
enum class NetworkState : uint8_t {
    WIFI_DISABLED, WIFI_NO_CREDENTIALS, WIFI_CONNECTING, WIFI_CONNECTED,
    WIFI_RETRY_WAIT, WIFI_SETUP_REQUIRED, WIFI_SETUP_AP,
};
class SmartGrindNetworkManager {
public:
    static constexpr uint32_t CONNECT_TIMEOUT_MS = 30000;
    static constexpr uint32_t RETRY_DELAY_MS = 30000;
    static constexpr uint32_t SETUP_RETRY_INTERVAL_MS = 60000;
    std::atomic<NetworkState> state_{NetworkState::WIFI_DISABLED};
    std::atomic<bool> setup_attempt_active_{false};
    std::atomic<int> setup_clients_{0};
    uint32_t setup_attempt_at_ms_ = 0;
    std::atomic<uint8_t> last_disconnect_reason_{0};
    std::atomic<int8_t> last_disconnect_rssi_{0};
    bool credentials = true;
    std::atomic<bool> initialized_{true};
    std::atomic<bool> enabled_{true};
    std::atomic<bool> station_got_ip_{false};
    bool ever_connected_ = false;
    uint32_t state_changed_at_ms_ = 0;
    esp_netif_t* station_netif_ = nullptr;
    esp_netif_t* access_point_netif_ = nullptr;
    bool wifi_driver_initialized_ = false;
    esp_event_handler_instance_t wifi_event_instance_ = nullptr;
    esp_event_handler_instance_t ip_event_instance_ = nullptr;
    int connection_attempts = 0, mdns_stops = 0;
    bool connection_starts = true;

    NetworkState state() const { return state_.load(); }
    bool has_credentials() const { return credentials; }
    // Stand-in for start_setup_access_point's bookkeeping.
    void enter_setup_mode() {
        setup_clients_ = 0; setup_attempt_active_ = false;
        setup_attempt_at_ms_ = millis() + SETUP_RETRY_INTERVAL_MS;
        mode = WIFI_MODE_APSTA; set_state(NetworkState::WIFI_SETUP_AP);
    }
    void set_state(NetworkState state) { state_.store(state); state_changed_at_ms_ = millis(); }
    void stop_mdns() { ++mdns_stops; }
    void handle_connected() { ever_connected_ = true; set_state(NetworkState::WIFI_CONNECTED); }
    // Stand-in for the production begin_connection: it ends in CONNECTING once
    // the driver accepts the attempt, and returns early otherwise.
    void begin_connection() {
        ++connection_attempts;
        if (connection_starts) set_state(NetworkState::WIFI_CONNECTING);
    }
    bool ensure_wifi_initialized();
    void update();
    void update_setup_retry();
    void close_setup_access_point();
    std::string last_failure() const;
    static void wifi_event_handler(void* context, esp_event_base_t base, int32_t id, void* data);
    static void ip_event_handler(void*, esp_event_base_t, int32_t, void*) {}
};
SmartGrindNetworkManager* radio_owner = nullptr;
esp_err_t esp_wifi_disconnect() {
    ++disconnects;
    SmartGrindNetworkManager::wifi_event_handler(radio_owner, WIFI_EVENT,
                                                 WIFI_EVENT_STA_DISCONNECTED, nullptr);
    return ESP_OK;
}
''' + function(SOURCE, "bool SmartGrindNetworkManager::ensure_wifi_initialized()") \
    + function(SOURCE, "void SmartGrindNetworkManager::update()") \
    + function(SOURCE, "void SmartGrindNetworkManager::update_setup_retry()") \
    + function(SOURCE, "void SmartGrindNetworkManager::close_setup_access_point()") \
    + function(SOURCE, "void SmartGrindNetworkManager::wifi_event_handler(") \
    + function(SOURCE, "std::string SmartGrindNetworkManager::last_failure() const") + r'''

void connected(SmartGrindNetworkManager& wifi) {
    wifi.station_got_ip_ = true; wifi.update();
    assert(wifi.state() == NetworkState::WIFI_CONNECTED);
}

int main() {
    // A dropped connection reconnects on the next service-loop pass instead of
    // waiting out the retry delay.
    SmartGrindNetworkManager wifi;
    radio_owner = &wifi;
    connected(wifi);
    wifi.station_got_ip_ = false; wifi.update();
    assert(wifi.connection_attempts == 1 && wifi.mdns_stops == 1);
    assert(wifi.state() == NetworkState::WIFI_CONNECTING);
    connected(wifi);

    // An attempt that cannot start waits out the retry delay.
    wifi.connection_starts = false;
    wifi.station_got_ip_ = false; wifi.update();
    assert(wifi.connection_attempts == 2 && wifi.state() == NetworkState::WIFI_RETRY_WAIT);
    now_ms += SmartGrindNetworkManager::RETRY_DELAY_MS - 1; wifi.update();
    assert(wifi.connection_attempts == 2);
    now_ms += 1; wifi.connection_starts = true; wifi.update();
    assert(wifi.connection_attempts == 3 && wifi.state() == NetworkState::WIFI_CONNECTING);

    // A reconnection that times out backs off rather than retrying at once.
    // The DISCONNECTED event the disconnect raises must find the new state,
    // not WIFI_CONNECTING, or the handler reconnects underneath the back-off.
    now_ms += SmartGrindNetworkManager::CONNECT_TIMEOUT_MS; wifi.update();
    assert(disconnects == 1 && connects == 0 && wifi.state() == NetworkState::WIFI_RETRY_WAIT);
    assert(wifi.connection_attempts == 3);

    // While an attempt is in progress, a drop is retried within the attempt.
    wifi.set_state(NetworkState::WIFI_CONNECTING);
    SmartGrindNetworkManager::wifi_event_handler(&wifi, WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, nullptr);
    assert(connects == 1 && wifi.state() == NetworkState::WIFI_CONNECTING);

    // Behind the setup access point the saved network is tried again once a
    // minute; an attempt that times out backs off, and the DISCONNECTED event
    // its disconnect raises does not start another.
    SmartGrindNetworkManager setup;
    radio_owner = &setup;
    setup.enter_setup_mode();
    const int connects_before = connects, disconnects_before = disconnects;
    now_ms += SmartGrindNetworkManager::SETUP_RETRY_INTERVAL_MS - 1; setup.update();
    assert(connects == connects_before && !setup.setup_attempt_active_);
    // A new attempt does not inherit the previous one's failure reason.
    setup.last_disconnect_reason_ = WIFI_REASON_NO_AP_FOUND;
    now_ms += 1; setup.update();
    assert(connects == connects_before + 1 && setup.setup_attempt_active_);
    assert(setup.last_disconnect_reason_ == 0);
    assert(setup.state() == NetworkState::WIFI_SETUP_AP && mode == WIFI_MODE_APSTA);
    // A drop within the attempt is retried within it.
    SmartGrindNetworkManager::wifi_event_handler(&setup, WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, nullptr);
    assert(connects == connects_before + 2);
    now_ms += SmartGrindNetworkManager::CONNECT_TIMEOUT_MS; setup.update();
    assert(!setup.setup_attempt_active_ && disconnects == disconnects_before + 1);
    assert(connects == connects_before + 2 && setup.state() == NetworkState::WIFI_SETUP_AP);

    // Nothing is tried while a phone is on the access point, and an attempt in
    // progress when one joins is abandoned.
    SmartGrindNetworkManager::wifi_event_handler(&setup, WIFI_EVENT, WIFI_EVENT_AP_STACONNECTED, nullptr);
    now_ms += 3 * SmartGrindNetworkManager::SETUP_RETRY_INTERVAL_MS; setup.update();
    assert(connects == connects_before + 2 && !setup.setup_attempt_active_);
    SmartGrindNetworkManager::wifi_event_handler(&setup, WIFI_EVENT, WIFI_EVENT_AP_STADISCONNECTED, nullptr);
    SmartGrindNetworkManager::wifi_event_handler(&setup, WIFI_EVENT, WIFI_EVENT_AP_STADISCONNECTED, nullptr);
    assert(setup.setup_clients_ == 0);  // a stray event never makes the count negative
    setup.update();
    assert(connects == connects_before + 3 && setup.setup_attempt_active_);
    SmartGrindNetworkManager::wifi_event_handler(&setup, WIFI_EVENT, WIFI_EVENT_AP_STACONNECTED, nullptr);
    now_ms += 1; setup.update();
    assert(!setup.setup_attempt_active_ && disconnects == disconnects_before + 2);
    SmartGrindNetworkManager::wifi_event_handler(&setup, WIFI_EVENT, WIFI_EVENT_AP_STADISCONNECTED, nullptr);

    // Reaching the network closes the setup access point and connects normally.
    now_ms += SmartGrindNetworkManager::SETUP_RETRY_INTERVAL_MS; setup.update();
    assert(setup.setup_attempt_active_);
    setup.station_got_ip_ = true; setup.update();
    assert(setup.state() == NetworkState::WIFI_CONNECTED && mode == WIFI_MODE_STA);
    assert(!setup.setup_attempt_active_ && setup.ever_connected_);

    // Without saved credentials there is nothing to try.
    SmartGrindNetworkManager unconfigured;
    radio_owner = &unconfigured;
    unconfigured.credentials = false; unconfigured.enter_setup_mode();
    const int connects_unconfigured = connects;
    now_ms += 5 * SmartGrindNetworkManager::SETUP_RETRY_INTERVAL_MS; unconfigured.update();
    assert(connects == connects_unconfigured && unconfigured.state() == NetworkState::WIFI_SETUP_AP);

    // Driver setup retries the step that failed and repeats none that worked.
    SmartGrindNetworkManager fresh;
    driver_init_failures = 1; ip_registration_failures = 1;
    assert(!fresh.ensure_wifi_initialized());  // driver init fails
    assert(!fresh.ensure_wifi_initialized());  // IP handler registration fails
    assert(fresh.ensure_wifi_initialized());
    assert(fresh.ensure_wifi_initialized());
    assert(station_creates == 1 && access_point_creates == 1);
    assert(driver_inits == 2 && wifi_registrations == 1 && ip_registrations == 2);

    // A dropped attempt records why, so giving up can say what went wrong.
    SmartGrindNetworkManager reasons;
    radio_owner = &reasons;
    assert(reasons.last_failure() == "no reply from the driver");
    reasons.set_state(NetworkState::WIFI_CONNECTING);
    wifi_event_sta_disconnected_t dropped = {};
    dropped.reason = WIFI_REASON_NO_AP_FOUND; dropped.rssi = -128;
    SmartGrindNetworkManager::wifi_event_handler(&reasons, WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &dropped);
    assert(reasons.last_failure() == "network not found");
    dropped.reason = WIFI_REASON_DISASSOC_DUE_TO_INACTIVITY; dropped.rssi = -77;
    SmartGrindNetworkManager::wifi_event_handler(&reasons, WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &dropped);
    assert(reasons.last_failure() == "association timed out, -77 dBm");
    dropped.reason = 39; dropped.rssi = -70;
    SmartGrindNetworkManager::wifi_event_handler(&reasons, WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &dropped);
    assert(reasons.last_failure() == "reason 39, -70 dBm");
}
'''


class NetworkRecoveryTest(unittest.TestCase):
    def test_reconnects_retries_behind_setup_and_retries_failed_setup(self):
        with tempfile.TemporaryDirectory() as folder:
            cpp, binary = Path(folder) / "network.cpp", Path(folder) / "network"
            cpp.write_text(HARNESS)
            # Production log formats use %lu for uint32_t, which is unsigned
            # long on the ESP32 but not on the host.
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-format",
                            str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10, stdout=subprocess.DEVNULL)


if __name__ == "__main__":
    unittest.main()
