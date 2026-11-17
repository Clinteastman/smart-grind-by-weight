#include "gaggimate_status_client.h"
#include <string>
#include "../system/string_utils.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <cstdint>
#include <freertos/task.h>
#include "../system/timing.h"

#include <esp_http_client.h>
#include "../storage/preferences.h"
#include <esp_err.h>

#include "network_manager.h"
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "../config/constants.h"

namespace {

constexpr const char* kPreferencesNamespace = "screensaver";
constexpr const char* kHostKey = "gm_host";

bool parse_number(const std::string& json, const char* key, float& output) {
    const std::string token = std::string("\"") + key + '"';
    int position = strings::index_of(json, token);
    if (position < 0) return false;
    position = strings::index_of(json, ':', static_cast<size_t>(position) + token.length());
    if (position < 0) return false;

    const char* start = json.c_str() + position + 1;
    while (*start == ' ' || *start == '\t') ++start;
    char* end = nullptr;
    const float value = strtof(start, &end);
    if (end == start || !std::isfinite(value)) return false;
    output = value;
    return true;
}

bool parse_bool(const std::string& json, const char* key, bool& output) {
    const std::string token = std::string("\"") + key + '"';
    int position = strings::index_of(json, token);
    if (position < 0) return false;
    position = strings::index_of(json, ':', static_cast<size_t>(position) + token.length());
    if (position < 0) return false;

    std::string value = json.substr(position + 1);
    value = strings::trim(value);
    if (strings::starts_with(value, "true") || strings::starts_with(value, "1")) {
        output = true;
        return true;
    }
    if (strings::starts_with(value, "false") || strings::starts_with(value, "0")) {
        output = false;
        return true;
    }
    return false;
}

bool parse_string(const std::string& json, const char* key, char* output, size_t output_size) {
    if (!output || output_size == 0) return false;
    const std::string token = std::string("\"") + key + '"';
    int position = strings::index_of(json, token);
    if (position < 0) return false;
    position = strings::index_of(json, ':', static_cast<size_t>(position) + token.length());
    if (position < 0) return false;
    const int opening_quote = strings::index_of(json, '"', position + 1);
    if (opening_quote < 0) return false;
    const int closing_quote = strings::index_of(json, '"', opening_quote + 1);
    if (closing_quote < 0) return false;

    const std::string value = strings::slice(json, opening_quote + 1, closing_quote);
    strncpy(output, value.c_str(), output_size - 1);
    output[output_size - 1] = '\0';
    return true;
}

}  // namespace

GaggiMateStatusClient gaggimate_status_client;

bool GaggiMateStatusClient::is_valid_host(const std::string& host) {
    if (host.empty() || host.length() > 63) return false;
    for (const char value : host) {
        if (!isalnum(static_cast<unsigned char>(value)) && value != '.' && value != '-') {
            return false;
        }
    }
    return true;
}

void GaggiMateStatusClient::init() {
    if (!mutex_) mutex_ = xSemaphoreCreateMutex();
    Preferences preferences;
    if (preferences.begin(kPreferencesNamespace, true)) {
        host_ = preferences.getString(kHostKey, "gaggimate.local");
        enabled_ = preferences.getString("style", "minimal") == "gaggimate";
        preferences.end();
    }
    if (enabled_) ensure_task();
}

bool GaggiMateStatusClient::configure(bool enabled, const std::string& host) {
    if ((!host.empty() && !is_valid_host(host)) || (enabled && host.empty())) return false;
    if (enabled && !ensure_task()) return false;

    Preferences preferences;
    if (!preferences.begin(kPreferencesNamespace, false)) return false;
    // An empty host means "leave the saved one alone", not "store nothing".
    const bool stored = host.empty() || preferences.putString(kHostKey, host);
    preferences.end();
    if (!stored) return false;

    if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool connection_changed = enabled_ != enabled || host_ != host;
    enabled_ = enabled;
    host_ = host;
    reconnect_requested_ = connection_changed;
    if (connection_changed) {
        status_ = GaggiMateStatus{};
        last_success_ms_ = 0;
    }
    if (mutex_) xSemaphoreGive(mutex_);
    return true;
}

GaggiMateStatus GaggiMateStatusClient::status() const {
    if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);
    const GaggiMateStatus copy = status_;
    if (mutex_) xSemaphoreGive(mutex_);
    return copy;
}

std::string GaggiMateStatusClient::configured_host() const {
    if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);
    const std::string copy = host_;
    if (mutex_) xSemaphoreGive(mutex_);
    return copy;
}

bool GaggiMateStatusClient::ensure_task() {
    if (task_handle_) return true;
    if (xTaskCreatePinnedToCore(task_entry, "GaggiMateStatus", 6144, this, 1,
                                &task_handle_, 1) == pdPASS) {
        return true;
    }
    task_handle_ = nullptr;
    LOG_BLE("[GAGGIMATE] Failed to create status polling task\n");
    return false;
}

void GaggiMateStatusClient::task_entry(void* context) {
    static_cast<GaggiMateStatusClient*>(context)->task_loop();
}

void GaggiMateStatusClient::task_loop() {
    for (;;) {
        bool enabled = false;
        std::string host;
        read_configuration(enabled, host);
        if (enabled && network_manager.is_connected() && is_valid_host(host)) {
            // One critical section per tick: this loop runs at 50 Hz purely to
            // notice that the 5 s HTTP fallback is not yet due.
            bool requested = false;
            bool reconnect = false;
            uint32_t last_success_ms = 0;
            if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);
            requested = reconnect_requested_;
            reconnect = requested || websocket_ == nullptr || connected_host_ != host;
            reconnect_requested_ = false;
            last_success_ms = last_success_ms_;
            if (mutex_) xSemaphoreGive(mutex_);
            const uint32_t now_ms = millis();
            // A client that could not start, typically for lack of internal RAM,
            // is retried at its reconnect interval rather than on every tick.
            if (reconnect &&
                (requested || now_ms - last_websocket_start_ms_ >= WEBSOCKET_RETRY_MS)) {
                last_websocket_start_ms_ = now_ms;
                start_websocket(host);
            }
            if (last_success_ms == 0 || now_ms - last_success_ms >= HTTP_FALLBACK_INTERVAL_MS) {
                if (last_http_poll_ms_ == 0 || now_ms - last_http_poll_ms_ >= HTTP_FALLBACK_INTERVAL_MS) {
                    last_http_poll_ms_ = now_ms;
                    poll_http_fallback(host);
                }
            }
        } else {
            stop_websocket();
            mark_offline_if_stale(millis());
        }
        vTaskDelay(pdMS_TO_TICKS(enabled ? 20 : 500));
    }
}

void GaggiMateStatusClient::start_websocket(const std::string& host) {
    stop_websocket();

    const std::string uri = "ws://" + host + ":80/ws";
    esp_websocket_client_config_t config = {};
    config.uri = uri.c_str();
    config.reconnect_timeout_ms = WEBSOCKET_RETRY_MS;
    config.network_timeout_ms = 3000;
    // GaggiMate pushes a status frame every second; a 15 s ping keeps NAT and
    // the socket alive when the machine is idle.
    config.ping_interval_sec = 15;
    config.disable_auto_reconnect = false;
    config.task_stack = 4096;
    config.buffer_size = 1024;

    websocket_ = esp_websocket_client_init(&config);
    if (!websocket_) {
        LOG_BLE("[GAGGIMATE] Could not create WebSocket client for %s\n", host.c_str());
        return;
    }
    if (esp_websocket_register_events(websocket_, WEBSOCKET_EVENT_ANY, websocket_event_handler,
                                      this) != ESP_OK ||
        esp_websocket_client_start(websocket_) != ESP_OK) {
        LOG_BLE("[GAGGIMATE] Could not start WebSocket client for %s\n", host.c_str());
        esp_websocket_client_destroy(websocket_);
        websocket_ = nullptr;
        return;
    }

    connected_host_ = host;
    websocket_frame_.clear();
    last_http_poll_ms_ = 0;
}

void GaggiMateStatusClient::stop_websocket() {
    if (websocket_) {
        esp_websocket_client_stop(websocket_);
        esp_websocket_client_destroy(websocket_);
        websocket_ = nullptr;
    }
    connected_host_.clear();
}

void GaggiMateStatusClient::websocket_event_handler(void* context, esp_event_base_t, int32_t id,
                                                    void* data) {
    auto* self = static_cast<GaggiMateStatusClient*>(context);
    auto* event = static_cast<esp_websocket_event_data_t*>(data);
    if (!self || !event) return;

    switch (id) {
        case WEBSOCKET_EVENT_DATA: {
            // Only text frames carry a status update. The client posts a frame
            // larger than its receive buffer as several DATA events, so parse
            // the payload once every piece has arrived.
            if (event->op_code != 0x01 || !event->data_ptr || event->data_len <= 0) break;
            std::string& frame = self->websocket_frame_;
            if (event->payload_offset == 0) frame.clear();
            const bool fits = event->payload_len > 0 &&
                              static_cast<size_t>(event->payload_len) <= WEBSOCKET_FRAME_MAX_BYTES;
            if (!fits || static_cast<size_t>(event->payload_offset) != frame.size()) {
                frame.clear();
                break;
            }
            frame.append(event->data_ptr, static_cast<size_t>(event->data_len));
            if (frame.size() >= static_cast<size_t>(event->payload_len)) {
                self->apply_status_payload(frame, true);
                frame.clear();
            }
            break;
        }
        case WEBSOCKET_EVENT_DISCONNECTED:
        case WEBSOCKET_EVENT_ERROR:
        case WEBSOCKET_EVENT_CLOSED:
            self->mark_offline_if_stale(millis());
            break;
        default:
            break;
    }
}

bool GaggiMateStatusClient::apply_status_payload(const std::string& payload, bool require_event_type) {
    if (require_event_type) {
        char event_type[24] = "";
        if (!parse_string(payload, "tp", event_type, sizeof(event_type)) ||
            strcmp(event_type, "evt:status") != 0) {
            return false;
        }
    }

    GaggiMateStatus next{};
    if (!parse_number(payload, "ct", next.current_temperature) ||
        !parse_number(payload, "tt", next.target_temperature)) {
        return false;
    }
    parse_number(payload, "pr", next.pressure);
    parse_number(payload, "fl", next.flow);
    parse_string(payload, "p", next.profile, sizeof(next.profile));

    float active = 0.0f;
    if (parse_number(payload, "a", active)) next.active = active > 0.5f;
    float elapsed_ms = 0.0f;
    if (parse_number(payload, "e", elapsed_ms) && elapsed_ms >= 0.0f) {
        next.elapsed_ms = static_cast<uint32_t>(elapsed_ms);
    }
    parse_string(payload, "l", next.phase, sizeof(next.phase));

    // The compact HTTP fallback uses descriptive field names. These are
    // optional so current GaggiMate releases remain fully compatible.
    parse_bool(payload, "active", next.active);
    if (parse_number(payload, "elapsed_ms", elapsed_ms) && elapsed_ms >= 0.0f) {
        next.elapsed_ms = static_cast<uint32_t>(elapsed_ms);
    }
    parse_string(payload, "phase", next.phase, sizeof(next.phase));
    parse_string(payload, "profile", next.profile, sizeof(next.profile));
    next.online = true;

    if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);
    status_ = next;
    last_success_ms_ = millis();
    if (mutex_) xSemaphoreGive(mutex_);
    return true;
}

void GaggiMateStatusClient::poll_http_fallback(const std::string& host) {

    const std::string url = "http://" + host + "/api/status";
    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.timeout_ms = 750;
    config.buffer_size = 1024;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        mark_offline_if_stale(millis());
        return;
    }

    std::string payload;
    if (esp_http_client_open(client, 0) == ESP_OK) {
        const int64_t length = esp_http_client_fetch_headers(client);
        if (esp_http_client_get_status_code(client) == 200 && length > 0 && length <= 4096) {
            payload.assign(static_cast<size_t>(length), '\0');
            if (esp_http_client_read_response(client, payload.data(), length) != length) {
                payload.clear();
            }
        }
        esp_http_client_close(client);
    }
    esp_http_client_cleanup(client);

    if (payload.empty() || !apply_status_payload(payload, false)) {
        mark_offline_if_stale(millis());
    }
}

void GaggiMateStatusClient::mark_offline_if_stale(uint32_t now_ms) {
    if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);
    if (last_success_ms_ == 0 || now_ms - last_success_ms_ >= OFFLINE_GRACE_MS) {
        status_.online = false;
        status_.active = false;
    }
    if (mutex_) xSemaphoreGive(mutex_);
}

bool GaggiMateStatusClient::read_configuration(bool& enabled, std::string& host) const {
    if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);
    enabled = enabled_;
    host = host_;
    if (mutex_) xSemaphoreGive(mutex_);
    return enabled && !host.empty();
}
