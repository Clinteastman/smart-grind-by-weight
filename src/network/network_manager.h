#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

enum class NetworkState : uint8_t {
    WIFI_DISABLED,
    WIFI_NO_CREDENTIALS,
    WIFI_CONNECTING,
    WIFI_CONNECTED,
    WIFI_RETRY_WAIT,
    WIFI_SETUP_REQUIRED,
    WIFI_SETUP_AP
};

class SmartGrindNetworkManager {
public:
    void init(Preferences* preferences);
    void update();

    bool set_enabled(bool enabled);
    bool set_credentials(const String& ssid, const String& password);
    bool connect_saved_credentials();
    void clear_credentials();
    bool start_setup_access_point(const String& ssid, const String& password);

    bool is_enabled() const { return enabled_.load(); }
    bool is_connected() const { return state() == NetworkState::WIFI_CONNECTED; }
    bool has_credentials() const;
    bool is_tab_enabled() const { return tab_enabled_.load(); }
    bool set_tab_enabled(bool enabled);
    bool should_show_tab() const { return is_enabled() && is_tab_enabled(); }
    bool is_icon_menu_only() const { return icon_menu_only_.load(); }
    bool set_icon_menu_only(bool menu_only);
    NetworkState state() const { return state_.load(); }
    String hostname() const;
    String device_id() const;
    String network_name() const;
    String ip_address() const;
    int get_rssi() const;
    int8_t get_signal_level() const;
    bool is_performance_mode() const { return performance_mode_.load(); }
    bool set_performance_mode(bool enabled);
    void apply_power_settings();
    bool is_time_synchronized() const;
    void configure_timezone(const String& tz);
    String timezone() const;

private:
    static constexpr uint32_t CONNECT_TIMEOUT_MS = 15000;
    static constexpr uint32_t RETRY_DELAY_MS = 30000;

    Preferences* preferences_ = nullptr;
    std::atomic<NetworkState> state_{NetworkState::WIFI_DISABLED};
    std::atomic<bool> initialized_{false};
    std::atomic<bool> enabled_{false};
    std::atomic<bool> tab_enabled_{true};
    std::atomic<bool> icon_menu_only_{false};
    std::atomic<bool> performance_mode_{false};
    bool ever_connected_ = false;
    bool mdns_started_ = false;
    uint32_t state_changed_at_ms_ = 0;
    String ssid_;
    String password_;
    String hostname_;
    mutable SemaphoreHandle_t settings_mutex_ = nullptr;

    void load_settings();
    void begin_connection();
    void handle_connected();
    void stop_network();
    void set_state(NetworkState state);
    static String default_hostname();
    static String sanitize_hostname(const String& hostname);
};

extern SmartGrindNetworkManager network_manager;
