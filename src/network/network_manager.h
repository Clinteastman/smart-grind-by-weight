#pragma once

#include <esp_event.h>
#include <esp_netif.h>
#include <esp_wifi_types.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "../storage/preferences.h"

enum class NetworkState : uint8_t {
    WIFI_DISABLED,
    WIFI_NO_CREDENTIALS,
    WIFI_CONNECTING,
    WIFI_CONNECTED,
    WIFI_RETRY_WAIT,
    WIFI_SETUP_REQUIRED,
    WIFI_SETUP_AP
};

/** One entry of a station-mode scan, for the provisioning network picker. */
struct WifiScanResult {
    std::string ssid;
    int8_t rssi = 0;
    bool secured = false;
};

class SmartGrindNetworkManager {
public:
    // How long one attempt on the saved network may take before it gives up. A
    // router at the edge of range can need several scans and association tries.
    static constexpr uint32_t CONNECT_TIMEOUT_MS = 30000;

    void init(Preferences* preferences);
    void update();

    bool set_enabled(bool enabled);
    bool set_credentials(const std::string& ssid, const std::string& password);
    bool connect_saved_credentials();
    void clear_credentials();
    bool start_setup_access_point(const std::string& ssid, const std::string& password);

    bool is_enabled() const { return enabled_.load(); }
    bool is_connected() const { return state() == NetworkState::WIFI_CONNECTED; }
    bool has_credentials() const;
    NetworkState state() const { return state_.load(); }
    std::string hostname() const;
    std::string device_id() const;
    std::string network_name() const;
    std::string ip_address() const;

    /** IPv4 address of the setup access point, empty when it is not running. */
    std::string access_point_ip() const;

    /**
     * Same address in host byte order, for callers that need it numerically.
     * Returns 0 when the access point is not running.
     */
    uint32_t access_point_ip_v4() const;

    /** True once the Wi-Fi driver is running in any mode. */
    bool is_radio_started() const { return wifi_started_; }

    /**
     * Scan for nearby networks. This blocks for the duration of the scan, so
     * only call it from the service loop or an HTTP handler.
     */
    std::vector<WifiScanResult> scan_networks();

private:
    static constexpr uint32_t RETRY_DELAY_MS = 30000;
    // How often the saved network is tried again behind the setup access point.
    static constexpr uint32_t SETUP_RETRY_INTERVAL_MS = 60000;

    Preferences* preferences_ = nullptr;
    std::atomic<NetworkState> state_{NetworkState::WIFI_DISABLED};
    std::atomic<bool> initialized_{false};
    std::atomic<bool> enabled_{false};
    std::atomic<bool> station_got_ip_{false};
    // A background attempt on the saved network while the setup AP is up.
    std::atomic<bool> setup_attempt_active_{false};
    // Phones and laptops connected to the setup access point.
    std::atomic<int> setup_clients_{0};
    uint32_t setup_attempt_at_ms_ = 0;  // when the active attempt began, or the next one is due
    // Why the driver last dropped the current attempt (0 = nothing reported yet),
    // and the signal strength it reported then.
    std::atomic<uint8_t> last_disconnect_reason_{0};
    std::atomic<int8_t> last_disconnect_rssi_{0};
    bool ever_connected_ = false;
    bool mdns_started_ = false;
    bool wifi_started_ = false;
    bool sntp_started_ = false;
    uint32_t state_changed_at_ms_ = 0;
    std::string ssid_;
    std::string password_;
    std::string hostname_;
    esp_netif_t* station_netif_ = nullptr;
    esp_netif_t* access_point_netif_ = nullptr;
    bool wifi_driver_initialized_ = false;
    esp_event_handler_instance_t wifi_event_instance_ = nullptr;
    esp_event_handler_instance_t ip_event_instance_ = nullptr;
    mutable SemaphoreHandle_t settings_mutex_ = nullptr;

    void load_settings();
    void begin_connection();
    /**
     * Try the saved network again while the setup access point is up, in case it
     * was only out of reach at start-up. Waits while anyone is connected to the
     * access point: an attempt moves the radio to the router's channel and
     * blocks the setup page's network scan.
     */
    void update_setup_retry();
    /** Close the setup access point once the station has joined the network. */
    void close_setup_access_point();
    void handle_connected();
    /** Why the last attempt failed, e.g. "network not found" or "association timed out, -77 dBm". */
    std::string last_failure() const;
    void stop_network();
    void set_state(NetworkState state);
    void stop_mdns();

    /**
     * Create both netifs, the Wi-Fi driver and the event handlers on first use.
     * Each step runs once; a step that fails is retried on the next call.
     */
    bool ensure_wifi_initialized();
    /** Start the radio if it is not running yet. Configure interfaces first. */
    bool start_radio();

    static void wifi_event_handler(void* context, esp_event_base_t base, int32_t id, void* data);
    static void ip_event_handler(void* context, esp_event_base_t base, int32_t id, void* data);

    static std::string sanitize_hostname(const std::string& hostname);
};

extern SmartGrindNetworkManager network_manager;
