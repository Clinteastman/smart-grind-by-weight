#include "network_manager.h"

#include <ESPmDNS.h>
#include <WiFi.h>
#include <time.h>

#include "../config/build_info.h"
#include "../config/constants.h"

SmartGrindNetworkManager network_manager;

void SmartGrindNetworkManager::init(Preferences* preferences) {
    preferences_ = preferences;
    if (!settings_mutex_) settings_mutex_ = xSemaphoreCreateMutex();
    const String tz = timezone();
    setenv("TZ", tz.c_str(), 1);
    tzset();
    load_settings();
    initialized_.store(true);

    if (!enabled_.load()) {
        set_state(NetworkState::WIFI_DISABLED);
        return;
    }

    if (!has_credentials()) {
        set_state(NetworkState::WIFI_NO_CREDENTIALS);
        return;
    }

    begin_connection();
}

void SmartGrindNetworkManager::update() {
    if (!initialized_.load() || !enabled_.load()) return;

    const wl_status_t wifi_status = WiFi.status();
    if (wifi_status == WL_CONNECTED) {
        if (state() != NetworkState::WIFI_CONNECTED) handle_connected();
        return;
    }

    if (state() == NetworkState::WIFI_CONNECTED) {
        if (mdns_started_) {
            MDNS.end();
            mdns_started_ = false;
        }
        LOG_BLE("[WIFI] Connection lost; retrying in %lus\n", RETRY_DELAY_MS / 1000);
        set_state(NetworkState::WIFI_RETRY_WAIT);
        return;
    }

    const uint32_t elapsed_ms = millis() - state_changed_at_ms_;
    if (state() == NetworkState::WIFI_CONNECTING && elapsed_ms >= CONNECT_TIMEOUT_MS) {
        WiFi.disconnect(false, false);
        if (ever_connected_) {
            LOG_BLE("[WIFI] Reconnection timed out; retrying in %lus\n", RETRY_DELAY_MS / 1000);
            set_state(NetworkState::WIFI_RETRY_WAIT);
        } else {
            LOG_BLE("[WIFI] Configured network unavailable; starting setup mode\n");
            set_state(NetworkState::WIFI_SETUP_REQUIRED);
        }
    } else if (state() == NetworkState::WIFI_RETRY_WAIT && elapsed_ms >= RETRY_DELAY_MS) {
        begin_connection();
    }
}

bool SmartGrindNetworkManager::set_enabled(bool enabled) {
    if (!preferences_) return false;

    enabled_.store(enabled);
    if (preferences_->putBool("wifi_on", enabled) == 0) return false;

    if (!enabled) {
        stop_network();
    } else if (!has_credentials()) {
        set_state(NetworkState::WIFI_NO_CREDENTIALS);
    } else {
        begin_connection();
    }
    return true;
}

bool SmartGrindNetworkManager::set_tab_enabled(bool enabled) {
    tab_enabled_.store(enabled);
    if (!preferences_) return false;
    return preferences_->putBool("wifi_tab", enabled) > 0;
}

bool SmartGrindNetworkManager::set_icon_menu_only(bool menu_only) {
    icon_menu_only_.store(menu_only);
    if (!preferences_) return false;
    return preferences_->putBool("wifi_icon_menu", menu_only) > 0;
}

bool SmartGrindNetworkManager::set_performance_mode(bool enabled) {
    performance_mode_.store(enabled);
    apply_power_settings();
    if (!preferences_) return false;
    return preferences_->putBool("wifi_perf", enabled) > 0;
}

void SmartGrindNetworkManager::apply_power_settings() {
    if (!enabled_.load() || state_.load() == NetworkState::WIFI_DISABLED) return;
    const bool perf = performance_mode_.load();
    WiFi.setSleep(!perf);
    WiFi.setTxPower(perf ? WIFI_POWER_19_5dBm : WIFI_POWER_15dBm);
}

bool SmartGrindNetworkManager::set_credentials(const String& ssid, const String& password) {
    if (!preferences_ || ssid.isEmpty() || ssid.length() > 32 || password.length() > 63) {
        return false;
    }
    if (!password.isEmpty() && password.length() < 8) return false;

    if (settings_mutex_) xSemaphoreTake(settings_mutex_, portMAX_DELAY);
    const String previous_password = password_;
    if (preferences_->putString("wifi_pass", password) == 0) {
        if (settings_mutex_) xSemaphoreGive(settings_mutex_);
        return false;
    }
    if (preferences_->putString("wifi_ssid", ssid) == 0) {
        preferences_->putString("wifi_pass", previous_password);
        if (settings_mutex_) xSemaphoreGive(settings_mutex_);
        return false;
    }

    ssid_ = ssid;
    password_ = password;
    if (settings_mutex_) xSemaphoreGive(settings_mutex_);
    // When credentials arrive through the setup AP, keep that link alive long
    // enough to return the success page. ProvisioningService performs a clean
    // reboot into station mode after the HTTP response has been queued.
    if (enabled_.load() && state() != NetworkState::WIFI_SETUP_AP) begin_connection();
    return true;
}

bool SmartGrindNetworkManager::connect_saved_credentials() {
    if (!initialized_.load() || !enabled_.load() || !has_credentials()) return false;
    begin_connection();
    return state() == NetworkState::WIFI_CONNECTING;
}

void SmartGrindNetworkManager::clear_credentials() {
    if (settings_mutex_) xSemaphoreTake(settings_mutex_, portMAX_DELAY);
    if (preferences_) {
        preferences_->remove("wifi_ssid");
        preferences_->remove("wifi_pass");
    }
    ssid_.clear();
    password_.clear();
    if (settings_mutex_) xSemaphoreGive(settings_mutex_);
    stop_network();
    set_state(enabled_.load() ? NetworkState::WIFI_NO_CREDENTIALS : NetworkState::WIFI_DISABLED);
}

bool SmartGrindNetworkManager::has_credentials() const {
    if (settings_mutex_) xSemaphoreTake(settings_mutex_, portMAX_DELAY);
    const bool present = !ssid_.isEmpty();
    if (settings_mutex_) xSemaphoreGive(settings_mutex_);
    return present;
}

String SmartGrindNetworkManager::hostname() const {
    if (settings_mutex_) xSemaphoreTake(settings_mutex_, portMAX_DELAY);
    const String value = hostname_;
    if (settings_mutex_) xSemaphoreGive(settings_mutex_);
    return value;
}

String SmartGrindNetworkManager::device_id() const {
    char value[13];
    snprintf(value, sizeof(value), "%012llx",
             static_cast<unsigned long long>(ESP.getEfuseMac()));
    return String(value);
}

String SmartGrindNetworkManager::network_name() const {
    if (settings_mutex_) xSemaphoreTake(settings_mutex_, portMAX_DELAY);
    const String value = ssid_;
    if (settings_mutex_) xSemaphoreGive(settings_mutex_);
    return value;
}

String SmartGrindNetworkManager::ip_address() const {
    return is_connected() ? WiFi.localIP().toString() : String();
}

int SmartGrindNetworkManager::get_rssi() const {
    if (!is_connected()) {
        cached_rssi_ = 0;
        last_rssi_poll_ms_ = 0;
        return 0;
    }
    const uint32_t now = millis();
    if (last_rssi_poll_ms_ == 0 || (now - last_rssi_poll_ms_) >= 5000) {
        cached_rssi_ = WiFi.RSSI();
        last_rssi_poll_ms_ = now;
    }
    return cached_rssi_;
}

int8_t SmartGrindNetworkManager::get_signal_level() const {
    if (!is_connected()) {
        return 0;
    }
    const int rssi = get_rssi();
    if (rssi > 0 || rssi < -110) {
        return 1;
    }
    if (rssi >= -67) return 4;
    if (rssi >= -75) return 3;
    if (rssi >= -83) return 2;
    return 1;
}

void SmartGrindNetworkManager::load_settings() {
    enabled_.store(preferences_ && preferences_->getBool("wifi_on", true));
    tab_enabled_.store(preferences_ ? preferences_->getBool("wifi_tab", true) : true);
    icon_menu_only_.store(preferences_ ? preferences_->getBool("wifi_icon_menu", false) : false);
    performance_mode_.store(preferences_ ? preferences_->getBool("wifi_perf", false) : false);
    ssid_ = preferences_ ? preferences_->getString("wifi_ssid", "") : String();
    password_ = preferences_ ? preferences_->getString("wifi_pass", "") : String();
    hostname_ = preferences_ ? sanitize_hostname(preferences_->getString("wifi_host", "")) : String();
    if (hostname_.isEmpty()) hostname_ = default_hostname();
}

void SmartGrindNetworkManager::begin_connection() {
    if (!enabled_.load()) return;

    if (settings_mutex_) xSemaphoreTake(settings_mutex_, portMAX_DELAY);
    const String ssid = ssid_;
    const String password = password_;
    const String hostname = hostname_;
    if (settings_mutex_) xSemaphoreGive(settings_mutex_);
    if (ssid.isEmpty()) return;

    if (mdns_started_) {
        MDNS.end();
        mdns_started_ = false;
    }

    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    apply_power_settings();
    WiFi.setAutoReconnect(true);
    WiFi.setHostname(hostname.c_str());
    WiFi.begin(ssid.c_str(), password.c_str());
    set_state(NetworkState::WIFI_CONNECTING);
    LOG_BLE("[WIFI] Connecting to configured network as %s.local\n", hostname.c_str());
}

bool SmartGrindNetworkManager::start_setup_access_point(const String& ssid, const String& password) {
    if (ssid.isEmpty() || (!password.isEmpty() && password.length() < 8)) return false;

    if (mdns_started_) {
        MDNS.end();
        mdns_started_ = false;
    }
    WiFi.disconnect(true, false);
    // AP+STA keeps the setup network available while the STA radio scans for
    // nearby routers for the provisioning dropdown.
    WiFi.mode(WIFI_AP_STA);
    // Some Android/Samsung captive-portal detectors do not launch their sign-in
    // UI when the AP gateway is in private address space. Use the same
    // public-looking, non-routed setup subnet as GaggiMate.
    const IPAddress setup_ip(4, 4, 4, 1);
    const IPAddress setup_mask(255, 255, 255, 0);
    if (!WiFi.softAPConfig(setup_ip, setup_ip, setup_mask)) return false;
    if (!WiFi.softAP(ssid.c_str(), password.isEmpty() ? nullptr : password.c_str())) return false;
    set_state(NetworkState::WIFI_SETUP_AP);
    LOG_BLE("[WIFI] Setup access point started: %s (%s)\n", ssid.c_str(), WiFi.softAPIP().toString().c_str());
    return true;
}

void SmartGrindNetworkManager::handle_connected() {
    ever_connected_ = true;
    set_state(NetworkState::WIFI_CONNECTED);
    const String current_hostname = hostname();
    const String current_device_id = device_id();
    mdns_started_ = MDNS.begin(current_hostname.c_str());
    if (mdns_started_) {
        MDNS.addService("http", "tcp", 80);
        MDNS.addService("smartgrind", "tcp", 80);
        MDNS.addServiceTxt("smartgrind", "tcp", "api", "v1");
        MDNS.addServiceTxt("smartgrind", "tcp", "protocol", "1");
        MDNS.addServiceTxt("smartgrind", "tcp", "version", BUILD_FIRMWARE_VERSION);
        MDNS.addServiceTxt("smartgrind", "tcp", "id", current_device_id.c_str());
    }
    const String tz = timezone();
    configTzTime(tz.c_str(), "pool.ntp.org", "time.nist.gov");
    LOG_BLE("[WIFI] Connected: %s (%s.local, mDNS=%s, TZ=%s)\n",
            WiFi.localIP().toString().c_str(), current_hostname.c_str(),
            mdns_started_ ? "OK" : "FAILED", tz.c_str());
}

void SmartGrindNetworkManager::stop_network() {
    if (mdns_started_) {
        MDNS.end();
        mdns_started_ = false;
    }
    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF);
    set_state(NetworkState::WIFI_DISABLED);
    LOG_BLE("[WIFI] Wi-Fi disabled and radio powered down\n");
}

void SmartGrindNetworkManager::set_state(NetworkState state) {
    state_.store(state);
    state_changed_at_ms_ = millis();
    if (state != NetworkState::WIFI_CONNECTED) {
        cached_rssi_ = 0;
        last_rssi_poll_ms_ = 0;
    }
}

String SmartGrindNetworkManager::default_hostname() {
    return String("smartgrind");
}

String SmartGrindNetworkManager::sanitize_hostname(const String& hostname) {
    String sanitized;
    sanitized.reserve(hostname.length());
    for (size_t i = 0; i < hostname.length() && sanitized.length() < 63; ++i) {
        char value = static_cast<char>(tolower(static_cast<unsigned char>(hostname[i])));
        if ((value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') || value == '-') {
            sanitized += value;
        }
    }
    while (sanitized.startsWith("-")) sanitized.remove(0, 1);
    while (sanitized.endsWith("-")) sanitized.remove(sanitized.length() - 1);
    return sanitized;
}

bool SmartGrindNetworkManager::is_time_synchronized() const {
    time_t now = time(nullptr);
    return now >= 1700000000;
}

void SmartGrindNetworkManager::configure_timezone(const String& tz) {
    if (tz.isEmpty()) return;
    Preferences prefs;
    if (prefs.begin("ntp", false)) {
        prefs.putString("tz", tz);
        prefs.end();
    }
    setenv("TZ", tz.c_str(), 1);
    tzset();
}

String SmartGrindNetworkManager::timezone() const {
    Preferences prefs;
    if (prefs.begin("ntp", true)) {
        String tz = prefs.getString("tz", "CET-1CEST,M3.5.0,M10.5.0/3");
        prefs.end();
        if (!tz.isEmpty()) return tz;
    }
    return "CET-1CEST,M3.5.0,M10.5.0/3";
}
