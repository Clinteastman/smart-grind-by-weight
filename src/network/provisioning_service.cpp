#include "provisioning_service.h"

#include <cstring>
#include <algorithm>
#include <vector>
#include <WiFi.h>

#include "../config/build_info.h"
#include "../config/constants.h"
#include "device_page.h"
#include "network_manager.h"

namespace {
constexpr char SETUP_PAGE[] PROGMEM = R"HTML(
<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Smart Grind Wi-Fi</title><style>
body{font-family:system-ui,sans-serif;background:#161914;color:#f4f4ed;max-width:34rem;margin:3rem auto;padding:0 1.2rem}
form{background:#242920;padding:1.4rem;border-radius:1rem}label{display:block;margin:.8rem 0 .25rem}
input,select,button{box-sizing:border-box;width:100%;padding:.8rem;border-radius:.55rem;border:1px solid #68735e;font-size:1rem}
button{margin-top:1.2rem;background:#86a869;color:#10140d;font-weight:700;border:0}.secondary{margin-top:.5rem;background:#3d4638;color:#f4f4ed}small,summary{color:#b8c1b0}details{margin-top:1rem}
</style></head><body><h1>Connect Smart Grind</h1>
<p>Enter the 2.4 GHz Wi-Fi network used by your phone and Home Assistant.</p>
<form id="wifiForm" method="post" action="/api/v1/setup/wifi"><label for="networks">Wi-Fi network</label>
<select id="networks"><option value="">Scanning for networks...</option></select>
<button class="secondary" id="refresh" type="button">Refresh network list</button>
<details><summary>Hidden network or manual entry</summary><label for="manualSsid">Network name</label>
<input id="manualSsid" maxlength="32" autocomplete="off"></details>
<input id="ssid" name="ssid" type="hidden">
<label for="password">Password</label><input id="password" name="password" type="password" maxlength="63">
<small>Leave blank only for an open network.</small><button type="submit">Save and connect</button></form>
<script>
const list=document.getElementById('networks'),manual=document.getElementById('manualSsid');
async function loadNetworks(refresh=false){
 try{const data=await fetch('/api/v1/setup/networks'+(refresh?'?refresh=1':''),{cache:'no-store'}).then(r=>r.json());
  if(data.scanning){list.innerHTML='<option value="">Scanning for networks...</option>';setTimeout(()=>loadNetworks(),800);return;}
  const previous=list.value;list.innerHTML='<option value="">Select a network</option>';
  data.networks.forEach(n=>{const o=document.createElement('option');o.value=n.ssid;o.textContent=`${n.ssid} (${n.rssi} dBm)${n.secure?' 🔒':''}`;list.appendChild(o)});
  if([...list.options].some(o=>o.value===previous))list.value=previous;
 }catch(e){list.innerHTML='<option value="">Could not scan — enter it manually</option>';}
}
document.getElementById('refresh').onclick=()=>loadNetworks(true);
document.getElementById('wifiForm').onsubmit=e=>{const ssid=(manual.value.trim()||list.value);if(!ssid){e.preventDefault();alert('Select or enter a Wi-Fi network.');return;}document.getElementById('ssid').value=ssid;};
loadNetworks();
</script></body></html>
)HTML";

void redirect_to_setup_page(AsyncWebServerRequest* request) {
    const String location = "http://" + WiFi.softAPIP().toString() + "/";
    LOG_BLE("[WIFI] Captive portal probe: %s -> %s\n",
            request->url().c_str(), location.c_str());
    AsyncWebServerResponse* response = request->beginResponse(302, "text/plain", "Open Smart Grind setup");
    response->addHeader("Location", location);
    response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    response->addHeader("Pragma", "no-cache");
    response->addHeader("Connection", "close");
    request->send(response);
}

String json_escape(const String& value) {
    String escaped;
    escaped.reserve(value.length() + 8);
    for (size_t i = 0; i < value.length(); ++i) {
        const char ch = value[i];
        if (ch == '"' || ch == '\\') escaped += '\\';
        if (static_cast<uint8_t>(ch) >= 0x20) escaped += ch;
    }
    return escaped;
}
}

ProvisioningService provisioning_service;

void ProvisioningService::init(Preferences* preferences, AsyncWebServer* server) {
    preferences_ = preferences;
    server_ = server;
    ap_ssid_ = build_ap_ssid();
    ap_password_ = load_or_create_ap_password();
    if (server_) configure_routes();
    initialized_ = true;
}

void ProvisioningService::update() {
    if (!initialized_) return;

    update_improv_serial();

    const NetworkState state = network_manager.state();
    if (!active_ && (state == NetworkState::WIFI_NO_CREDENTIALS ||
                     state == NetworkState::WIFI_SETUP_REQUIRED)) {
        start();
    }

    if (active_) dns_server_.processNextRequest();

    if (reboot_pending_ && static_cast<int32_t>(millis() - reboot_at_ms_) >= 0) {
        LOG_BLE("[WIFI] Restarting with saved network credentials\n");
        Serial.flush();
        ESP.restart();
    }
}

void ProvisioningService::configure_routes() {
    server_->on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        const char* page = network_manager.state() == NetworkState::WIFI_SETUP_AP
                               ? SETUP_PAGE
                               : SMART_GRIND_DEVICE_PAGE;
        request->send(200, "text/html", page);
    });
    server_->on(AsyncURIMatcher::exact("/api/v1/setup/networks"), HTTP_GET, [](AsyncWebServerRequest* request) {
        if (network_manager.state() != NetworkState::WIFI_SETUP_AP) {
            request->send(403, "application/json", "{\"error\":\"Wi-Fi setup is not active\"}");
            return;
        }

        if (request->hasParam("refresh")) {
            WiFi.scanDelete();
            WiFi.scanNetworks(true, true);
        }
        int count = WiFi.scanComplete();
        if (count == WIFI_SCAN_FAILED) {
            WiFi.scanNetworks(true, true);
            count = WIFI_SCAN_RUNNING;
        }
        if (count == WIFI_SCAN_RUNNING) {
            request->send(200, "application/json", "{\"scanning\":true,\"networks\":[]}");
            return;
        }

        struct NetworkResult { String ssid; int32_t rssi; bool secure; };
        std::vector<NetworkResult> networks;
        networks.reserve(std::min(count, 20));
        for (int i = 0; i < count; ++i) {
            const String ssid = WiFi.SSID(i);
            if (ssid.isEmpty()) continue;
            auto existing = std::find_if(networks.begin(), networks.end(),
                                         [&ssid](const NetworkResult& item) { return item.ssid == ssid; });
            if (existing != networks.end()) {
                if (WiFi.RSSI(i) > existing->rssi) existing->rssi = WiFi.RSSI(i);
                continue;
            }
            if (networks.size() >= 20) continue;
            networks.push_back({ssid, WiFi.RSSI(i), WiFi.encryptionType(i) != WIFI_AUTH_OPEN});
        }
        std::sort(networks.begin(), networks.end(),
                  [](const NetworkResult& left, const NetworkResult& right) {
                      return left.rssi > right.rssi;
                  });

        AsyncResponseStream* response = request->beginResponseStream("application/json");
        response->print("{\"scanning\":false,\"networks\":[");
        for (size_t i = 0; i < networks.size(); ++i) {
            if (i) response->print(',');
            response->printf("{\"ssid\":\"%s\",\"rssi\":%ld,\"secure\":%s}",
                             json_escape(networks[i].ssid).c_str(),
                             static_cast<long>(networks[i].rssi),
                             networks[i].secure ? "true" : "false");
        }
        response->print("]}");
        response->addHeader("Cache-Control", "no-store");
        request->send(response);
    });
    server_->on(AsyncURIMatcher::exact("/api/v1/setup/wifi"), HTTP_POST, [this](AsyncWebServerRequest* request) {
        if (network_manager.state() != NetworkState::WIFI_SETUP_AP) {
            request->send(403, "text/plain", "Wi-Fi setup is not active");
            return;
        }
        if (!request->hasParam("ssid", true) || !request->hasParam("password", true)) {
            request->send(400, "text/plain", "Network name and password fields are required");
            return;
        }
        const String ssid = request->getParam("ssid", true)->value();
        const String password = request->getParam("password", true)->value();
        if (!network_manager.set_credentials(ssid, password)) {
            request->send(400, "text/plain", "Invalid network name or password");
            return;
        }
        request->send(200, "text/html", "<h1>Saved</h1><p>Smart Grind is restarting and will join your network.</p>");
        schedule_reboot();
    });

    const char* captive_paths[] = {
        // Android / ChromeOS
        "/generate_204", "/gen_204", "/redirect", "/canonical.html",
        // Apple
        "/hotspot-detect.html", "/library/test/success.html",
        // Windows
        "/ncsi.txt", "/redirect.msft",
        // Firefox / Kindle
        "/kindle-wifi/wifistub.html", "/fwlink"
    };
    for (const char* path : captive_paths) {
        server_->on(path, HTTP_ANY, redirect_to_setup_page);
    }
    // These exact responses match the clients' captive-network expectations.
    server_->on("/connecttest.txt", HTTP_ANY, [](AsyncWebServerRequest* request) {
        LOG_BLE("[WIFI] Captive portal probe: %s -> logout.net\n", request->url().c_str());
        request->redirect("http://logout.net");
    });
    server_->on("/success.txt", HTTP_ANY, [](AsyncWebServerRequest* request) {
        LOG_BLE("[WIFI] Captive portal probe: %s -> 200\n", request->url().c_str());
        request->send(200);
    });
    server_->on("/wpad.dat", HTTP_ANY, [](AsyncWebServerRequest* request) { request->send(404); });
    server_->onNotFound([](AsyncWebServerRequest* request) {
        if (network_manager.state() == NetworkState::WIFI_SETUP_AP) redirect_to_setup_page(request);
        else request->send(404, "text/plain", "Not found");
    });
}

void ProvisioningService::start() {
    if (!network_manager.start_setup_access_point(ap_ssid_, ap_password_)) {
        LOG_BLE("[WIFI] Failed to start setup access point\n");
        return;
    }
    dns_server_.setTTL(3600);
    dns_server_.start(53, "*", WiFi.softAPIP());
    WiFi.scanDelete();
    WiFi.scanNetworks(true, true);
    active_ = true;
    LOG_BLE("[WIFI] Setup page: http://%s/\n", WiFi.softAPIP().toString().c_str());
}

void ProvisioningService::stop_dns() {
    if (!active_) return;
    dns_server_.stop();
    active_ = false;
}

void ProvisioningService::schedule_reboot() {
    stop_dns();
    reboot_pending_ = true;
    reboot_at_ms_ = millis() + 1500;
}

void ProvisioningService::update_improv_serial() {
    constexpr uint32_t FRAME_TIMEOUT_MS = 100;
    constexpr uint32_t CONNECT_TIMEOUT_MS = 20000;
    constexpr size_t MAX_BYTES_PER_UPDATE = 64;

    if (improv_rx_position_ > 0 && millis() - improv_last_byte_ms_ > FRAME_TIMEOUT_MS) {
        improv_rx_position_ = 0;
    }

    size_t processed = 0;
    while (Serial.available() > 0 && processed++ < MAX_BYTES_PER_UPDATE) {
        const uint8_t byte = static_cast<uint8_t>(Serial.read());
        if (improv_rx_position_ >= sizeof(improv_rx_buffer_)) improv_rx_position_ = 0;
        const size_t position = improv_rx_position_;
        improv_rx_buffer_[position] = byte;
        improv_last_byte_ms_ = millis();

        const bool frame_complete = position >= 9 && position == 9 + improv_rx_buffer_[8];
        if (frame_complete && improv_rx_buffer_[7] == improv::TYPE_RPC) {
            const size_t rpc_length = improv_rx_buffer_[8];
            const uint8_t* rpc = improv_rx_buffer_ + 9;
            bool structurally_valid = rpc_length >= 2 && rpc[1] == rpc_length - 2;
            if (structurally_valid && rpc[0] == improv::WIFI_SETTINGS) {
                structurally_valid = rpc_length >= 4;
                if (structurally_valid) {
                    const size_t password_length_index = 3 + rpc[2];
                    structurally_valid = password_length_index < rpc_length &&
                                         password_length_index + 1 + rpc[password_length_index] == rpc_length;
                }
            }
            if (!structurally_valid) {
                send_improv_error(improv::ERROR_INVALID_RPC);
                improv_rx_position_ = 0;
                continue;
            }
        }

        const bool keep = improv::parse_improv_serial_byte(
            position, byte, improv_rx_buffer_,
            [this](improv::ImprovCommand command) {
                return handle_improv_command(command);
            },
            [this](improv::Error error) {
                send_improv_error(error);
            });
        if (frame_complete || !keep) {
            improv_rx_position_ = 0;
            if (!keep && byte == 'I') {
                improv_rx_buffer_[0] = byte;
                improv_rx_position_ = 1;
            }
        } else {
            improv_rx_position_++;
        }
    }

    if (!improv_connect_pending_) return;
    if (network_manager.is_connected()) {
        complete_improv_connection();
    } else if (network_manager.state() == NetworkState::WIFI_SETUP_REQUIRED ||
               network_manager.state() == NetworkState::WIFI_DISABLED ||
               millis() - improv_connect_started_ms_ >= CONNECT_TIMEOUT_MS) {
        improv_connect_pending_ = false;
        send_improv_error(improv::ERROR_UNABLE_TO_CONNECT);
        send_improv_state(network_manager.is_enabled() ? improv::STATE_AUTHORIZED
                                                       : improv::STATE_STOPPED);
    }
}

bool ProvisioningService::handle_improv_command(const improv::ImprovCommand& command) {
    send_improv_error(improv::ERROR_NONE);
    switch (command.command) {
        case improv::WIFI_SETTINGS: {
            if (improv_connect_pending_ || !network_manager.is_enabled()) {
                send_improv_error(improv::ERROR_UNABLE_TO_CONNECT);
                return true;
            }
            const String ssid(command.ssid.c_str());
            const String password(command.password.c_str());
            if (!network_manager.set_credentials(ssid, password)) {
                send_improv_error(improv::ERROR_INVALID_RPC);
                return true;
            }
            stop_dns();
            if (network_manager.state() != NetworkState::WIFI_CONNECTING &&
                !network_manager.connect_saved_credentials()) {
                send_improv_error(improv::ERROR_UNABLE_TO_CONNECT);
                return true;
            }
            improv_connect_pending_ = true;
            improv_connect_started_ms_ = millis();
            send_improv_state(improv::STATE_PROVISIONING);
            return true;
        }
        case improv::GET_CURRENT_STATE: {
            const improv::State state = !network_manager.is_enabled()
                                            ? improv::STATE_STOPPED
                                            : network_manager.is_connected()
                                                  ? improv::STATE_PROVISIONED
                                                  : improv_connect_pending_
                                                        ? improv::STATE_PROVISIONING
                                                        : improv::STATE_AUTHORIZED;
            send_improv_state(state);
            if (state == improv::STATE_PROVISIONED) {
                send_improv_response(command.command,
                                     {"http://" + network_manager.ip_address()});
            }
            return true;
        }
        case improv::GET_DEVICE_INFO: {
#ifdef HW_DISPLAY_VARIANT_V2
            const String hardware = "Waveshare ESP32-S3 Touch AMOLED 1.64 V2";
#else
            const String hardware = "Waveshare ESP32-S3 Touch AMOLED 1.64 V1";
#endif
            send_improv_response(command.command,
                                 {"Smart Grind-by-Weight", BUILD_FIRMWARE_VERSION,
                                  hardware, "Smart Grind"});
            return true;
        }
        case improv::GET_NETWORK_STATE: {
            uint8_t flags = improv::NETWORK_SUPPORTS_WIFI;
            std::vector<String> values;
            if (network_manager.is_connected()) {
                flags |= improv::NETWORK_IS_ONLINE;
                values.push_back(String(flags));
                values.push_back("http://" + network_manager.ip_address());
            } else {
                values.push_back(String(flags));
            }
            send_improv_response(command.command, values);
            return true;
        }
        case improv::BAD_CHECKSUM:
        case improv::UNKNOWN:
            send_improv_error(improv::ERROR_INVALID_RPC);
            return false;
        default:
            send_improv_error(improv::ERROR_UNKNOWN_RPC);
            return false;
    }
}

void ProvisioningService::send_improv_state(improv::State state) {
    const uint8_t value = static_cast<uint8_t>(state);
    send_improv_packet(improv::TYPE_CURRENT_STATE, &value, 1);
}

void ProvisioningService::send_improv_error(improv::Error error) {
    const uint8_t value = static_cast<uint8_t>(error);
    send_improv_packet(improv::TYPE_ERROR_STATE, &value, 1);
}

void ProvisioningService::send_improv_response(improv::Command command,
                                               const std::vector<String>& values) {
    const std::vector<uint8_t> response = improv::build_rpc_response(command, values, false);
    send_improv_packet(improv::TYPE_RPC_RESPONSE, response.data(), response.size());
}

void ProvisioningService::send_improv_packet(improv::ImprovSerialType type,
                                             const uint8_t* data, size_t length) {
    if (length > 255 || (length > 0 && !data)) return;
    uint8_t frame[267] = {'I', 'M', 'P', 'R', 'O', 'V', improv::IMPROV_SERIAL_VERSION,
                          static_cast<uint8_t>(type), static_cast<uint8_t>(length)};
    if (length > 0) memcpy(frame + 9, data, length);
    uint8_t checksum = 0;
    for (size_t i = 0; i < 9 + length; ++i) checksum += frame[i];
    frame[9 + length] = checksum;
    frame[10 + length] = '\n';
    Serial.write(frame, 11 + length);
}

void ProvisioningService::complete_improv_connection() {
    improv_connect_pending_ = false;
    send_improv_state(improv::STATE_PROVISIONED);
    send_improv_response(improv::WIFI_SETTINGS,
                         {"http://" + network_manager.ip_address()});
}

String ProvisioningService::load_or_create_ap_password() {
    if (!preferences_) return make_random_password(12);
    String password = preferences_->getString("wifi_ap_pass", "");
    if (password.length() >= 8) return password;
    password = make_random_password(12);
    preferences_->putString("wifi_ap_pass", password);
    return password;
}

String ProvisioningService::build_ap_ssid() {
    char ssid[32];
    snprintf(ssid, sizeof(ssid), "SmartGrind-%06lx",
             static_cast<unsigned long>(ESP.getEfuseMac() & 0xFFFFFFULL));
    return String(ssid);
}

String ProvisioningService::make_random_password(size_t length) {
    static constexpr char ALPHABET[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
    String password;
    password.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        password += ALPHABET[esp_random() % (sizeof(ALPHABET) - 1)];
    }
    return password;
}
