#include "network_manager.h"

#include <esp_err.h>
#include <esp_netif_sntp.h>
#include <esp_wifi.h>
#include <lwip/ip4_addr.h>
#include <mdns.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "../config/build_info.h"
#include "../config/constants.h"
#include "../system/device_info.h"
#include "../system/timing.h"

SmartGrindNetworkManager network_manager;

namespace {

// Some Android and Samsung captive-portal detectors do not launch their sign-in
// UI when the access-point gateway is in private address space, so the setup
// network uses the same public-looking, non-routed subnet as GaggiMate.
constexpr uint8_t kSetupIp[4] = {4, 4, 4, 1};
constexpr uint8_t kSetupNetmask[4] = {255, 255, 255, 0};

constexpr uint16_t kMaxScanResults = 20;
constexpr char kDefaultHostname[] = "smartgrind";

std::string ip4_to_string(const esp_ip4_addr_t& address) {
    char text[16] = {};
    snprintf(text, sizeof(text), IPSTR, IP2STR(&address));
    return text;
}

// The disconnect reasons a station attempt commonly ends with, in words a user
// can act on. Anything else is logged by number.
const char* disconnect_reason_text(uint8_t reason) {
    switch (reason) {
        case 0: return "no reply from the driver";
        case WIFI_REASON_AUTH_EXPIRE: return "authentication timed out";
        case WIFI_REASON_DISASSOC_DUE_TO_INACTIVITY: return "association timed out";
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_HANDSHAKE_TIMEOUT: return "handshake timed out (wrong password or weak signal)";
        case WIFI_REASON_BEACON_TIMEOUT: return "signal lost";
        case WIFI_REASON_NO_AP_FOUND: return "network not found";
        case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
        case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD: return "network security not supported";
        case WIFI_REASON_AUTH_FAIL: return "authentication failed";
        case WIFI_REASON_ASSOC_FAIL: return "association failed";
        case WIFI_REASON_CONNECTION_FAIL: return "connection failed";
        default: return nullptr;
    }
}

// The driver reports this when it has no signal reading, e.g. for "no AP found".
constexpr int8_t kNoRssi = -128;

}  // namespace

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

bool SmartGrindNetworkManager::ensure_wifi_initialized() {
    if (ip_event_instance_) return true;

    // esp_netif and the default event loop are created in app_main before any
    // service starts, so only the netifs, driver and handlers are set up here.
    if (!station_netif_) station_netif_ = esp_netif_create_default_wifi_sta();
    if (!access_point_netif_) access_point_netif_ = esp_netif_create_default_wifi_ap();
    if (!station_netif_ || !access_point_netif_) {
        LOG_BLE("[WIFI] Could not create network interfaces\n");
        return false;
    }

    if (!wifi_driver_initialized_) {
        wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
        const esp_err_t err = esp_wifi_init(&config);
        if (err != ESP_OK) {
            LOG_BLE("[WIFI] Driver init failed: %s\n", esp_err_to_name(err));
            return false;
        }
        // Credentials live in this firmware's own NVS namespace; letting the
        // driver persist a second copy would make the two disagree after a
        // factory reset.
        esp_wifi_set_storage(WIFI_STORAGE_RAM);
        wifi_driver_initialized_ = true;
    }

    esp_err_t err = ESP_OK;
    if (!wifi_event_instance_) {
        err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                  wifi_event_handler, this,
                                                  &wifi_event_instance_);
        if (err != ESP_OK) wifi_event_instance_ = nullptr;
    }
    if (err == ESP_OK) {
        err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                  ip_event_handler, this, &ip_event_instance_);
        if (err != ESP_OK) ip_event_instance_ = nullptr;
    }
    if (err != ESP_OK) {
        LOG_BLE("[WIFI] Event registration failed: %s\n", esp_err_to_name(err));
        return false;
    }
    return true;
}

bool SmartGrindNetworkManager::start_radio() {
    if (wifi_started_) return true;

    const esp_err_t err = esp_wifi_start();
    if (err != ESP_OK) {
        LOG_BLE("[WIFI] Radio start failed: %s\n", esp_err_to_name(err));
        return false;
    }
    wifi_started_ = true;
    return true;
}

void SmartGrindNetworkManager::wifi_event_handler(void* context, esp_event_base_t,
                                                  int32_t id, void* data) {
    auto* self = static_cast<SmartGrindNetworkManager*>(context);
    if (!self) return;

    switch (id) {
        case WIFI_EVENT_STA_DISCONNECTED:
            if (data) {
                const auto* event = static_cast<const wifi_event_sta_disconnected_t*>(data);
                self->last_disconnect_reason_.store(event->reason);
                self->last_disconnect_rssi_.store(event->rssi);
            }
            [[fallthrough]];
        case WIFI_EVENT_STA_START:
            self->station_got_ip_.store(false);
            // Retry within the current attempt, or within a background attempt
            // behind the setup access point; giving up, backing off and falling
            // through to setup mode all stay in update() so the timing lives in
            // one place.
            if (self->state() == NetworkState::WIFI_CONNECTING ||
                self->setup_attempt_active_.load()) {
                esp_wifi_connect();
            }
            break;
        case WIFI_EVENT_AP_STACONNECTED:
            self->setup_clients_.fetch_add(1);
            break;
        case WIFI_EVENT_AP_STADISCONNECTED: {
            int clients = self->setup_clients_.load();
            while (clients > 0 && !self->setup_clients_.compare_exchange_weak(clients, clients - 1)) {
            }
            break;
        }
        default:
            break;
    }
}

void SmartGrindNetworkManager::ip_event_handler(void* context, esp_event_base_t,
                                                int32_t id, void*) {
    auto* self = static_cast<SmartGrindNetworkManager*>(context);
    if (self && id == IP_EVENT_STA_GOT_IP) {
        self->station_got_ip_.store(true);
    }
}

void SmartGrindNetworkManager::update() {
    if (!initialized_.load() || !enabled_.load()) return;

    if (station_got_ip_.load()) {
        if (state() != NetworkState::WIFI_CONNECTED) {
            if (state() == NetworkState::WIFI_SETUP_AP) close_setup_access_point();
            handle_connected();
        }
        return;
    }

    if (state() == NetworkState::WIFI_CONNECTED) {
        // Reconnect at once, as the Arduino core's auto-reconnect did. The retry
        // delay applies once this attempt times out, or if it cannot start.
        stop_mdns();
        LOG_BLE("[WIFI] Connection lost (%s); reconnecting\n", last_failure().c_str());
        set_state(NetworkState::WIFI_RETRY_WAIT);
        begin_connection();
        return;
    }

    const uint32_t elapsed_ms = millis() - state_changed_at_ms_;
    if (state() == NetworkState::WIFI_CONNECTING && elapsed_ms >= CONNECT_TIMEOUT_MS) {
        // Leave WIFI_CONNECTING before the disconnect raises DISCONNECTED: the
        // event handler, on the other core, reconnects only within an attempt.
        const std::string failure = last_failure();
        if (ever_connected_) {
            LOG_BLE("[WIFI] Reconnection timed out (%s); retrying in %lus\n", failure.c_str(),
                    RETRY_DELAY_MS / 1000);
            set_state(NetworkState::WIFI_RETRY_WAIT);
        } else {
            LOG_BLE("[WIFI] Configured network unavailable (%s); starting setup mode\n",
                    failure.c_str());
            set_state(NetworkState::WIFI_SETUP_REQUIRED);
        }
        esp_wifi_disconnect();
    } else if (state() == NetworkState::WIFI_RETRY_WAIT && elapsed_ms >= RETRY_DELAY_MS) {
        begin_connection();
    } else if (state() == NetworkState::WIFI_SETUP_AP) {
        update_setup_retry();
    }
}

void SmartGrindNetworkManager::update_setup_retry() {
    if (!has_credentials()) return;

    const uint32_t now = millis();
    if (setup_attempt_active_.load()) {
        const bool client_joined = setup_clients_.load() > 0;
        if (!client_joined && now - setup_attempt_at_ms_ < CONNECT_TIMEOUT_MS) return;
        if (!client_joined) {
            LOG_BLE("[WIFI] Configured network still unavailable (%s)\n", last_failure().c_str());
        }
        // Clear the flag before the disconnect raises DISCONNECTED, as above.
        setup_attempt_active_.store(false);
        esp_wifi_disconnect();
        setup_attempt_at_ms_ = now + SETUP_RETRY_INTERVAL_MS;
        return;
    }
    if (setup_clients_.load() > 0 || static_cast<int32_t>(now - setup_attempt_at_ms_) < 0) return;

    setup_attempt_at_ms_ = now;
    last_disconnect_reason_.store(0);
    setup_attempt_active_.store(true);
    if (esp_wifi_connect() != ESP_OK) {
        setup_attempt_active_.store(false);
        setup_attempt_at_ms_ = now + SETUP_RETRY_INTERVAL_MS;
        return;
    }
    LOG_BLE("[WIFI] Trying the configured network again\n");
}

void SmartGrindNetworkManager::close_setup_access_point() {
    setup_attempt_active_.store(false);
    // The station keeps its connection; only the access point stops.
    esp_wifi_set_mode(WIFI_MODE_STA);
    LOG_BLE("[WIFI] Configured network reached; setup access point closed\n");
}

bool SmartGrindNetworkManager::set_enabled(bool enabled) {
    if (!preferences_) return false;

    enabled_.store(enabled);
    if (!preferences_->putBool("wifi_on", enabled)) return false;

    if (!enabled) {
        stop_network();
    } else if (!has_credentials()) {
        set_state(NetworkState::WIFI_NO_CREDENTIALS);
    } else {
        begin_connection();
    }
    return true;
}

bool SmartGrindNetworkManager::set_credentials(const std::string& ssid,
                                               const std::string& password) {
    if (!preferences_ || ssid.empty() || ssid.length() > 32 || password.length() > 63) {
        return false;
    }
    if (!password.empty() && password.length() < 8) return false;

    if (settings_mutex_) xSemaphoreTake(settings_mutex_, portMAX_DELAY);
    const std::string previous_password = password_;
    // An open network stores an empty password, so success cannot be inferred
    // from how many bytes were written.
    if (!preferences_->putString("wifi_pass", password)) {
        if (settings_mutex_) xSemaphoreGive(settings_mutex_);
        return false;
    }
    if (!preferences_->putString("wifi_ssid", ssid)) {
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
    const bool present = !ssid_.empty();
    if (settings_mutex_) xSemaphoreGive(settings_mutex_);
    return present;
}

std::string SmartGrindNetworkManager::hostname() const {
    if (settings_mutex_) xSemaphoreTake(settings_mutex_, portMAX_DELAY);
    const std::string value = hostname_;
    if (settings_mutex_) xSemaphoreGive(settings_mutex_);
    return value;
}

std::string SmartGrindNetworkManager::device_id() const {
    char value[13];
    snprintf(value, sizeof(value), "%012llx",
             static_cast<unsigned long long>(device_info::efuse_mac()));
    return value;
}

std::string SmartGrindNetworkManager::network_name() const {
    if (settings_mutex_) xSemaphoreTake(settings_mutex_, portMAX_DELAY);
    const std::string value = ssid_;
    if (settings_mutex_) xSemaphoreGive(settings_mutex_);
    return value;
}

std::string SmartGrindNetworkManager::ip_address() const {
    if (!is_connected() || !station_netif_) return {};
    esp_netif_ip_info_t info = {};
    if (esp_netif_get_ip_info(station_netif_, &info) != ESP_OK) return {};
    return ip4_to_string(info.ip);
}

std::string SmartGrindNetworkManager::access_point_ip() const {
    if (state() != NetworkState::WIFI_SETUP_AP || !access_point_netif_) return {};
    esp_netif_ip_info_t info = {};
    if (esp_netif_get_ip_info(access_point_netif_, &info) != ESP_OK) return {};
    return ip4_to_string(info.ip);
}

uint32_t SmartGrindNetworkManager::access_point_ip_v4() const {
    if (state() != NetworkState::WIFI_SETUP_AP || !access_point_netif_) return 0;
    esp_netif_ip_info_t info = {};
    if (esp_netif_get_ip_info(access_point_netif_, &info) != ESP_OK) return 0;
    return ntohl(info.ip.addr);
}

std::vector<WifiScanResult> SmartGrindNetworkManager::scan_networks() {
    std::vector<WifiScanResult> results;
    if (!wifi_started_) return results;

    wifi_scan_config_t scan_config = {};
    scan_config.show_hidden = false;
    if (esp_wifi_scan_start(&scan_config, true) != ESP_OK) return results;

    uint16_t count = kMaxScanResults;
    std::vector<wifi_ap_record_t> records(count);
    if (esp_wifi_scan_get_ap_records(&count, records.data()) != ESP_OK) {
        esp_wifi_clear_ap_list();
        return results;
    }

    results.reserve(count);
    for (uint16_t i = 0; i < count; ++i) {
        const char* ssid = reinterpret_cast<const char*>(records[i].ssid);
        if (ssid[0] == '\0') continue;
        WifiScanResult entry;
        entry.ssid = ssid;
        entry.rssi = records[i].rssi;
        entry.secured = records[i].authmode != WIFI_AUTH_OPEN;
        results.push_back(std::move(entry));
    }

    // Strongest first, and only the first sighting of each name.
    std::sort(results.begin(), results.end(),
              [](const WifiScanResult& a, const WifiScanResult& b) { return a.rssi > b.rssi; });
    results.erase(std::unique(results.begin(), results.end(),
                              [](const WifiScanResult& a, const WifiScanResult& b) {
                                  return a.ssid == b.ssid;
                              }),
                  results.end());
    return results;
}

void SmartGrindNetworkManager::load_settings() {
    enabled_.store(preferences_ && preferences_->getBool("wifi_on", true));
    ssid_ = preferences_ ? preferences_->getString("wifi_ssid", "") : std::string();
    password_ = preferences_ ? preferences_->getString("wifi_pass", "") : std::string();
    hostname_ = preferences_ ? sanitize_hostname(preferences_->getString("wifi_host", ""))
                             : std::string();
    if (hostname_.empty()) hostname_ = kDefaultHostname;
}

void SmartGrindNetworkManager::begin_connection() {
    if (!enabled_.load()) return;

    if (settings_mutex_) xSemaphoreTake(settings_mutex_, portMAX_DELAY);
    const std::string ssid = ssid_;
    const std::string password = password_;
    const std::string hostname = hostname_;
    if (settings_mutex_) xSemaphoreGive(settings_mutex_);
    if (ssid.empty()) return;

    stop_mdns();
    station_got_ip_.store(false);
    setup_attempt_active_.store(false);

    // The interface has to be fully configured before the radio starts, so a
    // WIFI_EVENT_STA_START arriving immediately can act on the right settings.
    if (!ensure_wifi_initialized()) return;
    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) return;
    esp_netif_set_hostname(station_netif_, hostname.c_str());

    // The driver's fields are fixed-size and need no terminator, so copy the
    // exact length: an SSID may legitimately use all 32 bytes.
    wifi_config_t config = {};
    memcpy(config.sta.ssid, ssid.data(),
           std::min(ssid.size(), sizeof(config.sta.ssid)));
    memcpy(config.sta.password, password.data(),
           std::min(password.size(), sizeof(config.sta.password)));
    config.sta.threshold.authmode = password.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA_PSK;
    if (esp_wifi_set_config(WIFI_IF_STA, &config) != ESP_OK) return;

    last_disconnect_reason_.store(0);
    set_state(NetworkState::WIFI_CONNECTING);
    // Starting the radio raises WIFI_EVENT_STA_START, whose handler issues the
    // connect; a radio that is already running needs it issued here.
    const bool radio_was_running = wifi_started_;
    if (!start_radio()) return;
    if (radio_was_running) esp_wifi_connect();
    LOG_BLE("[WIFI] Connecting to configured network as %s.local\n", hostname.c_str());
}

bool SmartGrindNetworkManager::start_setup_access_point(const std::string& ssid,
                                                       const std::string& password) {
    if (ssid.empty() || (!password.empty() && password.length() < 8)) return false;

    stop_mdns();
    if (!ensure_wifi_initialized()) return false;
    esp_wifi_disconnect();

    // AP+STA keeps the setup network available while the station radio scans for
    // nearby routers for the provisioning dropdown.
    if (esp_wifi_set_mode(WIFI_MODE_APSTA) != ESP_OK) return false;

    wifi_config_t config = {};
    memcpy(config.ap.ssid, ssid.data(), std::min(ssid.size(), sizeof(config.ap.ssid)));
    config.ap.ssid_len = static_cast<uint8_t>(std::min(ssid.size(), sizeof(config.ap.ssid)));
    config.ap.max_connection = 4;
    config.ap.channel = 1;
    if (password.empty()) {
        config.ap.authmode = WIFI_AUTH_OPEN;
    } else {
        memcpy(config.ap.password, password.data(),
               std::min(password.size(), sizeof(config.ap.password)));
        config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    }
    if (esp_wifi_set_config(WIFI_IF_AP, &config) != ESP_OK) return false;
    if (!start_radio()) return false;

    // The DHCP server has to be restarted around an address change, and that
    // only works once the interface is up.
    esp_netif_ip_info_t ip_info = {};
    IP4_ADDR(&ip_info.ip, kSetupIp[0], kSetupIp[1], kSetupIp[2], kSetupIp[3]);
    IP4_ADDR(&ip_info.gw, kSetupIp[0], kSetupIp[1], kSetupIp[2], kSetupIp[3]);
    IP4_ADDR(&ip_info.netmask, kSetupNetmask[0], kSetupNetmask[1], kSetupNetmask[2],
             kSetupNetmask[3]);
    esp_netif_dhcps_stop(access_point_netif_);
    if (esp_netif_set_ip_info(access_point_netif_, &ip_info) != ESP_OK) return false;
    if (esp_netif_dhcps_start(access_point_netif_) != ESP_OK) return false;

    setup_clients_.store(0);
    setup_attempt_active_.store(false);
    setup_attempt_at_ms_ = millis() + SETUP_RETRY_INTERVAL_MS;
    set_state(NetworkState::WIFI_SETUP_AP);
    LOG_BLE("[WIFI] Setup access point started: %s (%s)\n", ssid.c_str(),
            access_point_ip().c_str());
    return true;
}

void SmartGrindNetworkManager::handle_connected() {
    ever_connected_ = true;
    set_state(NetworkState::WIFI_CONNECTED);
    const std::string current_hostname = hostname();
    const std::string current_device_id = device_id();

    mdns_started_ = mdns_init() == ESP_OK;
    if (mdns_started_) {
        mdns_hostname_set(current_hostname.c_str());
        mdns_instance_name_set(BLE_DEVICE_NAME);
        mdns_service_add(nullptr, "_http", "_tcp", 80, nullptr, 0);
        mdns_txt_item_t txt[] = {
            {"api", "v1"},
            {"protocol", "1"},
            {"version", BUILD_FIRMWARE_VERSION},
            {"id", current_device_id.c_str()},
        };
        mdns_service_add(nullptr, "_smartgrind", "_tcp", 80, txt,
                         sizeof(txt) / sizeof(txt[0]));
    }

    // UTC is rendered in the browser's local timezone. Synchronisation is
    // asynchronous and does not delay the control or UI tasks.
    if (!sntp_started_) {
        esp_sntp_config_t sntp_config =
            ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(2,
                                                   ESP_SNTP_SERVER_LIST("pool.ntp.org",
                                                                        "time.nist.gov"));
        sntp_started_ = esp_netif_sntp_init(&sntp_config) == ESP_OK;
    }

    LOG_BLE("[WIFI] Connected: %s (%s.local, mDNS=%s)\n", ip_address().c_str(),
            current_hostname.c_str(), mdns_started_ ? "OK" : "FAILED");
}

std::string SmartGrindNetworkManager::last_failure() const {
    const uint8_t reason = last_disconnect_reason_.load();
    const int8_t rssi = last_disconnect_rssi_.load();
    char text[64];
    const char* name = disconnect_reason_text(reason);
    int length = name ? snprintf(text, sizeof(text), "%s", name)
                      : snprintf(text, sizeof(text), "reason %u", reason);
    if (reason != 0 && rssi != kNoRssi && length > 0 && length < static_cast<int>(sizeof(text))) {
        snprintf(text + length, sizeof(text) - length, ", %d dBm", rssi);
    }
    return text;
}

void SmartGrindNetworkManager::stop_mdns() {
    if (!mdns_started_) return;
    mdns_free();
    mdns_started_ = false;
}

void SmartGrindNetworkManager::stop_network() {
    stop_mdns();
    station_got_ip_.store(false);
    setup_attempt_active_.store(false);
    if (wifi_started_) {
        esp_wifi_disconnect();
        esp_wifi_stop();
        wifi_started_ = false;
    }
    set_state(NetworkState::WIFI_DISABLED);
}

void SmartGrindNetworkManager::set_state(NetworkState state) {
    state_.store(state);
    state_changed_at_ms_ = millis();
}

std::string SmartGrindNetworkManager::sanitize_hostname(const std::string& hostname) {
    std::string sanitized;
    sanitized.reserve(hostname.length());
    for (size_t i = 0; i < hostname.length() && sanitized.length() < 63; ++i) {
        const char value = static_cast<char>(tolower(static_cast<unsigned char>(hostname[i])));
        if ((value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') || value == '-') {
            sanitized += value;
        }
    }
    while (!sanitized.empty() && sanitized.front() == '-') sanitized.erase(0, 1);
    while (!sanitized.empty() && sanitized.back() == '-') sanitized.pop_back();
    return sanitized;
}
