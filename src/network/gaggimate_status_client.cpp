#include "gaggimate_status_client.h"

#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "../config/logging.h"

namespace {

constexpr const char* kPreferencesNamespace = "screensaver";
constexpr const char* kHostKey = "gm_host";

bool parse_number(const String& json, const char* key, float& output) {
    const String token = String('"') + key + '"';
    int position = json.indexOf(token);
    if (position < 0) return false;
    position = json.indexOf(':', position + token.length());
    if (position < 0) return false;

    const char* start = json.c_str() + position + 1;
    while (*start == ' ' || *start == '\t') ++start;
    char* end = nullptr;
    const float value = strtof(start, &end);
    if (end == start || !isfinite(value)) return false;
    output = value;
    return true;
}

bool parse_bool(const String& json, const char* key, bool& output) {
    const String token = String('"') + key + '"';
    int position = json.indexOf(token);
    if (position < 0) return false;
    position = json.indexOf(':', position + token.length());
    if (position < 0) return false;

    String value = json.substring(position + 1);
    value.trim();
    if (value.startsWith("true") || value.startsWith("1")) {
        output = true;
        return true;
    }
    if (value.startsWith("false") || value.startsWith("0")) {
        output = false;
        return true;
    }
    return false;
}

bool parse_string(const String& json, const char* key, char* output, size_t output_size) {
    if (!output || output_size == 0) return false;
    const String token = String('"') + key + '"';
    int position = json.indexOf(token);
    if (position < 0) return false;
    position = json.indexOf(':', position + token.length());
    if (position < 0) return false;
    const int opening_quote = json.indexOf('"', position + 1);
    if (opening_quote < 0) return false;
    const int closing_quote = json.indexOf('"', opening_quote + 1);
    if (closing_quote < 0) return false;

    const String value = json.substring(opening_quote + 1, closing_quote);
    strncpy(output, value.c_str(), output_size - 1);
    output[output_size - 1] = '\0';
    return true;
}

bool valid_host(const String& host) {
    if (host.isEmpty() || host.length() > 63) return false;
    for (size_t index = 0; index < host.length(); ++index) {
        const char value = host[index];
        if (!isalnum(static_cast<unsigned char>(value)) && value != '.' && value != '-') {
            return false;
        }
    }
    return true;
}

}  // namespace

GaggiMateStatusClient gaggimate_status_client;

void GaggiMateStatusClient::init() {
    if (!mutex_) mutex_ = xSemaphoreCreateMutex();
    Preferences preferences;
    if (preferences.begin(kPreferencesNamespace, true)) {
        host_ = preferences.getString(kHostKey, "gaggimate.local");
        enabled_ = preferences.getString("style", "minimal") == "gaggimate";
        preferences.end();
    }
    websocket_.onEvent([this](WStype_t type, uint8_t* payload, size_t length) {
        handle_websocket_event(type, payload, length);
    });
    websocket_.setReconnectInterval(3000);
    websocket_.enableHeartbeat(15000, 3000, 2);
    if (!task_handle_) {
        if (xTaskCreatePinnedToCore(task_entry, "GaggiMateStatus", 6144, this, 1,
                                    &task_handle_, 1) != pdPASS) {
            task_handle_ = nullptr;
            LOG_BLE("[GAGGIMATE] Failed to create status polling task\n");
        }
    }
}

bool GaggiMateStatusClient::configure(bool enabled, const String& host) {
    if ((!host.isEmpty() && !valid_host(host)) || (enabled && host.isEmpty())) return false;

    Preferences preferences;
    if (!preferences.begin(kPreferencesNamespace, false)) return false;
    const bool stored = host.isEmpty() || preferences.putString(kHostKey, host) > 0;
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

String GaggiMateStatusClient::configured_host() const {
    if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);
    const String copy = host_;
    if (mutex_) xSemaphoreGive(mutex_);
    return copy;
}

void GaggiMateStatusClient::task_entry(void* context) {
    static_cast<GaggiMateStatusClient*>(context)->task_loop();
}

void GaggiMateStatusClient::task_loop() {
    for (;;) {
        bool enabled = false;
        String host;
        read_configuration(enabled, host);
        if (enabled && WiFi.status() == WL_CONNECTED && valid_host(host)) {
            bool reconnect = false;
            uint32_t last_success_ms = 0;
            if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);
            reconnect = reconnect_requested_ || !websocket_started_ || connected_host_ != host;
            reconnect_requested_ = false;
            last_success_ms = last_success_ms_;
            if (mutex_) xSemaphoreGive(mutex_);
            if (reconnect) start_websocket(host);

            websocket_.loop();
            const uint32_t now_ms = millis();
            if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);
            last_success_ms = last_success_ms_;
            if (mutex_) xSemaphoreGive(mutex_);
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
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void GaggiMateStatusClient::start_websocket(const String& host) {
    stop_websocket();
    connected_host_ = host;
    last_http_poll_ms_ = 0;
    websocket_.begin(host.c_str(), 80, "/ws", "");
    websocket_started_ = true;
}

void GaggiMateStatusClient::stop_websocket() {
    if (!websocket_started_) return;
    websocket_.disconnect();
    websocket_started_ = false;
    connected_host_ = "";
}

void GaggiMateStatusClient::handle_websocket_event(WStype_t type, uint8_t* payload, size_t length) {
    if (type == WStype_TEXT && payload && length > 0) {
        const String message(reinterpret_cast<const char*>(payload), length);
        apply_status_payload(message, true);
    } else if (type == WStype_DISCONNECTED || type == WStype_ERROR) {
        mark_offline_if_stale(millis());
    }
}

bool GaggiMateStatusClient::apply_status_payload(const String& payload, bool require_event_type) {
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

void GaggiMateStatusClient::poll_http_fallback(const String& host) {

    WiFiClient client;
    HTTPClient request;
    request.setConnectTimeout(500);
    request.setTimeout(750);
    const String url = String("http://") + host + "/api/status";
    if (!request.begin(client, url)) {
        mark_offline_if_stale(millis());
        return;
    }

    const int response_code = request.GET();
    if (response_code != HTTP_CODE_OK) {
        request.end();
        mark_offline_if_stale(millis());
        return;
    }

    const String payload = request.getString();
    request.end();
    if (!apply_status_payload(payload, false)) {
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

bool GaggiMateStatusClient::read_configuration(bool& enabled, String& host) const {
    if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);
    enabled = enabled_;
    host = host_;
    if (mutex_) xSemaphoreGive(mutex_);
    return enabled && !host.isEmpty();
}
