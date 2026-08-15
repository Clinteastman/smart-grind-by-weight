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

void SmartGrindNetworkManager::load_settings() {
    enabled_.store(preferences_ && preferences_->getBool("wifi_on", true));
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
    // UTC is rendered in the browser's local timezone. Synchronisation is
    // asynchronous and does not delay the control or UI tasks.
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    LOG_BLE("[WIFI] Connected: %s (%s.local, mDNS=%s)\n",
            WiFi.localIP().toString().c_str(), current_hostname.c_str(), mdns_started_ ? "OK" : "FAILED");
}

void SmartGrindNetworkManager::stop_network() {
    if (mdns_started_) {
        MDNS.end();
        mdns_started_ = false;
    }
    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF);
    set_state(NetworkState::WIFI_DISABLED);
}

void SmartGrindNetworkManager::set_state(NetworkState state) {
    state_.store(state);
    state_changed_at_ms_ = millis();
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
