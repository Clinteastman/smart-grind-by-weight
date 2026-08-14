#include "device_web_server.h"

#include <Arduino.h>
#include <Update.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>

#include "../config/build_info.h"
#include "../config/constants.h"
#include "../controllers/grind_controller.h"
#include "../bluetooth/manager.h"
#include "../hardware/hardware_manager.h"
#include "network_manager.h"
#include "device_api.h"

namespace {
const char* network_state_name(NetworkState state) {
    switch (state) {
        case NetworkState::WIFI_DISABLED: return "disabled";
        case NetworkState::WIFI_NO_CREDENTIALS: return "no_credentials";
        case NetworkState::WIFI_CONNECTING: return "connecting";
        case NetworkState::WIFI_CONNECTED: return "connected";
        case NetworkState::WIFI_RETRY_WAIT: return "retry_wait";
        case NetworkState::WIFI_SETUP_REQUIRED: return "setup_required";
        case NetworkState::WIFI_SETUP_AP: return "setup_ap";
    }
    return "unknown";
}

String json_escape(const String& value) {
    String escaped;
    escaped.reserve(value.length() + 8);
    for (size_t i = 0; i < value.length(); ++i) {
        const char ch = value[i];
        switch (ch) {
            case '"': escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\b': escaped += "\\b"; break;
            case '\f': escaped += "\\f"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default:
                if (static_cast<uint8_t>(ch) >= 0x20) escaped += ch;
                break;
        }
    }
    return escaped;
}

struct OtaRequestState {
    bool success;
    bool complete;
};

constexpr uint32_t OTA_ARM_WINDOW_MS = 120000;
constexpr uint32_t OTA_REBOOT_DELAY_MS = 1500;
constexpr size_t OTA_MIN_INTERNAL_HEAP = 64U * 1024U;
UpdateClass web_firmware_update;
}

DeviceWebServer device_web_server;

void DeviceWebServer::init(HardwareManager* hardware_manager, GrindController* grind_controller,
                           BluetoothManager* bluetooth_manager) {
    if (initialized_) return;
    hardware_manager_ = hardware_manager;
    grind_controller_ = grind_controller;
    bluetooth_manager_ = bluetooth_manager;
    device_api.init(&server_, hardware_manager_, grind_controller_);
    configure_routes();
    initialized_ = true;
}

void DeviceWebServer::begin() {
    if (!initialized_ || started_) return;
    server_.begin();
    started_ = true;
    LOG_BLE("[WEB] HTTP service listening on port 80\n");
}

void DeviceWebServer::update() {
    device_api.update();
    if (ota_armed_.load() && !ota_active_.load() && !is_ota_armed()) ota_armed_.store(false);
    if (reboot_pending_.load() &&
        static_cast<int32_t>(millis() - reboot_at_ms_.load()) >= 0) {
        LOG_BLE("[WEB OTA] Restarting into updated firmware\n");
        Serial.flush();
        ESP.restart();
    }
}

bool DeviceWebServer::arm_ota() {
    if (!initialized_ || ota_active_.load() || !network_manager.is_connected() ||
        !grind_controller_ || grind_controller_->is_active() ||
        (bluetooth_manager_ && bluetooth_manager_->is_transfer_active())) {
        return false;
    }
    uint64_t token = (static_cast<uint64_t>(esp_random()) << 32) | esp_random();
    if (token == 0) token = 1;
    ota_token_.store(token);
    ota_armed_until_ms_.store(millis() + OTA_ARM_WINDOW_MS);
    ota_armed_.store(true);
    LOG_BLE("[WEB OTA] Upload window armed for %lus\n", OTA_ARM_WINDOW_MS / 1000);
    return true;
}

bool DeviceWebServer::is_ota_armed() const {
    return ota_armed_.load() &&
           static_cast<int32_t>(ota_armed_until_ms_.load() - millis()) > 0;
}

uint32_t DeviceWebServer::ota_seconds_remaining() const {
    if (!is_ota_armed()) return 0;
    return (ota_armed_until_ms_.load() - millis() + 999) / 1000;
}

uint8_t DeviceWebServer::ota_progress_percent() const {
    const size_t total = ota_total_.load();
    if (!ota_active_.load() || total == 0) return 0;
    const size_t percent = (ota_received_.load() * 100U) / total;
    return static_cast<uint8_t>(percent > 100 ? 100 : percent);
}

String DeviceWebServer::ota_token_string() const {
    const uint64_t token = ota_token_.load();
    char text[17];
    snprintf(text, sizeof(text), "%08lx%08lx",
             static_cast<unsigned long>(token >> 32),
             static_cast<unsigned long>(token & 0xFFFFFFFFULL));
    return String(text);
}

void DeviceWebServer::configure_routes() {
    server_.on("/api/v1/status", HTTP_GET, [](AsyncWebServerRequest* request) {
        AsyncResponseStream* response = request->beginResponseStream("application/json");
        const String hostname = json_escape(network_manager.hostname());
        const String device_id = json_escape(network_manager.device_id());
        const String network_name = json_escape(network_manager.network_name());
        const String ip_address = json_escape(network_manager.ip_address());
        const String ota_token = device_web_server.is_ota_armed()
                                     ? device_web_server.ota_token_string()
                                     : String();
        response->printf(
#ifdef HW_DISPLAY_VARIANT_V2
            "{\"api\":\"v1\",\"device\":{\"id\":\"%s\",\"model\":\"ESP32-S3-Touch-AMOLED-1.64\",\"hardware_revision\":\"v2\"},"
#else
            "{\"api\":\"v1\",\"device\":{\"id\":\"%s\",\"model\":\"ESP32-S3-Touch-AMOLED-1.64\",\"hardware_revision\":\"v1\"},"
#endif
            "\"firmware\":{\"version\":\"%s\",\"build\":%d,\"commit\":\"%s\"},"
            "\"network\":{\"state\":\"%s\",\"hostname\":\"%s\",\"ssid\":\"%s\",\"ip\":\"%s\"},"
            "\"system\":{\"uptime_ms\":%lu,\"free_heap\":%u,\"free_internal_heap\":%u,"
            "\"largest_internal_block\":%u,\"free_psram\":%u},"
            "\"ota\":{\"armed\":%s,\"active\":%s,\"arm_seconds\":%lu,\"progress\":%u,\"token\":\"%s\"}}",
            device_id.c_str(),
            BUILD_FIRMWARE_VERSION,
            BUILD_NUMBER,
            GIT_COMMIT_ID,
            network_state_name(network_manager.state()),
            hostname.c_str(),
            network_name.c_str(),
            ip_address.c_str(),
            static_cast<unsigned long>(millis()),
            static_cast<unsigned int>(ESP.getFreeHeap()),
            static_cast<unsigned int>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
            static_cast<unsigned int>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
            static_cast<unsigned int>(ESP.getFreePsram()),
            device_web_server.is_ota_armed() ? "true" : "false",
            device_web_server.is_ota_active() ? "true" : "false",
            static_cast<unsigned long>(device_web_server.ota_seconds_remaining()),
            device_web_server.ota_progress_percent(),
            ota_token.c_str());
        response->addHeader("Cache-Control", "no-store");
        request->send(response);
    });

    server_.on("/health", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/plain", "ok");
    });

    server_.on(
        "/api/v1/ota", HTTP_POST,
        [](AsyncWebServerRequest* request) {
            if (request->getResponse()) return;
            OtaRequestState* state = static_cast<OtaRequestState*>(request->_tempObject);
            if (!state || !state->complete) {
                request->send(400, "text/plain", "Firmware file was not received");
                return;
            }
            if (!state->success) {
                request->send(500, "text/plain", "Firmware update failed");
                return;
            }
            request->send(200, "text/plain", "Firmware accepted; Smart Grind is restarting");
        },
        [this](AsyncWebServerRequest* request, String filename, size_t index,
               uint8_t* data, size_t len, bool final) {
            handle_ota_upload(request, filename, index, data, len, final);
        });
}

void DeviceWebServer::handle_ota_upload(AsyncWebServerRequest* request, const String& filename,
                                         size_t index, uint8_t* data, size_t len, bool final) {
    if (request->getResponse()) return;

    OtaRequestState* state = static_cast<OtaRequestState*>(request->_tempObject);
    if (index == 0) {
        state = static_cast<OtaRequestState*>(calloc(1, sizeof(OtaRequestState)));
        request->_tempObject = state;
        if (!state) {
            request->send(503, "text/plain", "Not enough memory to start firmware update");
            return;
        }
        if (!is_ota_armed()) {
            request->send(403, "text/plain", "Arm firmware update on the grinder screen first");
            return;
        }
        if (!request->hasHeader("X-Smart-Grind-OTA-Token") ||
            request->getHeader("X-Smart-Grind-OTA-Token")->value() != ota_token_string()) {
            request->send(403, "text/plain", "Reload the page after arming firmware update");
            return;
        }
        bool expected_inactive = false;
        if (!ota_active_.compare_exchange_strong(expected_inactive, true)) {
            request->send(409, "text/plain", "Another firmware update is already active");
            return;
        }
        ota_armed_.store(false);
        if (!filename.endsWith(".bin") || len == 0 || data[0] != 0xE9) {
            finish_ota(false);
            request->send(400, "text/plain", "Not a valid ESP32 firmware image");
            return;
        }
        if (!grind_controller_ || grind_controller_->is_active()) {
            finish_ota(false);
            request->send(409, "text/plain", "Stop the grinder before updating firmware");
            return;
        }
        if (bluetooth_manager_ && bluetooth_manager_->is_transfer_active()) {
            finish_ota(false);
            request->send(409, "text/plain", "Wait for the Bluetooth transfer to finish");
            return;
        }
        const size_t internal_heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (internal_heap < OTA_MIN_INTERNAL_HEAP) {
            finish_ota(false);
            request->send(503, "text/plain", "Not enough internal memory for a safe update");
            return;
        }
        const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
        if (!target || request->contentLength() > target->size + 8192U) {
            finish_ota(false);
            request->send(413, "text/plain", "Firmware image is too large");
            return;
        }
        if (hardware_manager_ && hardware_manager_->get_grinder()) {
            hardware_manager_->get_grinder()->stop();
        }
        if (!web_firmware_update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
            LOG_BLE("[WEB OTA] Update.begin failed: %s\n", web_firmware_update.errorString());
            finish_ota(false);
            request->send(500, "text/plain", "Could not open the inactive firmware partition");
            return;
        }
        ota_received_.store(0);
        ota_total_.store(request->contentLength());
        request->onDisconnect([this, state]() {
            if (ota_active_.load() && state && !state->complete) {
                LOG_BLE("[WEB OTA] Client disconnected; aborting incomplete upload\n");
                web_firmware_update.abort();
                finish_ota(false);
            }
        });
        LOG_BLE("[WEB OTA] Receiving %s\n", filename.c_str());
    }

    if (!state || !ota_active_.load()) return;
    if (len > 0 && web_firmware_update.write(data, len) != len) {
        LOG_BLE("[WEB OTA] Write failed at %lu: %s\n",
                static_cast<unsigned long>(ota_received_.load()), web_firmware_update.errorString());
        web_firmware_update.abort();
        state->complete = true;
        state->success = false;
        finish_ota(false);
        request->send(500, "text/plain", "Firmware write failed");
        return;
    }
    ota_received_.fetch_add(len);

    if (final) {
        state->complete = true;
        state->success = web_firmware_update.end(true);
        if (!state->success) {
            LOG_BLE("[WEB OTA] Image validation failed: %s\n", web_firmware_update.errorString());
            finish_ota(false);
            return;
        }
        LOG_BLE("[WEB OTA] Firmware validated (%lu bytes)\n",
                static_cast<unsigned long>(ota_received_.load()));
        finish_ota(true);
    }
}

void DeviceWebServer::finish_ota(bool success) {
    ota_active_.store(false);
    ota_armed_.store(false);
    ota_token_.store(0);
    if (success) {
        reboot_at_ms_.store(millis() + OTA_REBOOT_DELAY_MS);
        reboot_pending_.store(true);
    }
}
