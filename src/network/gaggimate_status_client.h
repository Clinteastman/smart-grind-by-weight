#pragma once

#include <Arduino.h>
#include <WebSocketsClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

struct GaggiMateStatus {
    bool online = false;
    bool active = false;
    float current_temperature = 0.0f;
    float target_temperature = 0.0f;
    float pressure = 0.0f;
    float flow = 0.0f;
    uint32_t elapsed_ms = 0;
    char phase[24] = "Idle";
    char profile[32] = "";
};

class GaggiMateStatusClient {
public:
    void init();
    bool configure(bool enabled, const String& host);
    GaggiMateStatus status() const;
    String configured_host() const;

private:
    static constexpr uint32_t OFFLINE_GRACE_MS = 5000;
    static constexpr uint32_t HTTP_FALLBACK_INTERVAL_MS = 5000;

    mutable SemaphoreHandle_t mutex_ = nullptr;
    TaskHandle_t task_handle_ = nullptr;
    WebSocketsClient websocket_;
    bool enabled_ = false;
    bool reconnect_requested_ = false;
    bool websocket_started_ = false;
    String host_;
    String connected_host_;
    GaggiMateStatus status_{};
    uint32_t last_success_ms_ = 0;
    uint32_t last_http_poll_ms_ = 0;

    static void task_entry(void* context);
    void task_loop();
    void start_websocket(const String& host);
    void stop_websocket();
    void handle_websocket_event(WStype_t type, uint8_t* payload, size_t length);
    bool apply_status_payload(const String& payload, bool require_event_type);
    void poll_http_fallback(const String& host);
    void mark_offline_if_stale(uint32_t now_ms);
    bool read_configuration(bool& enabled, String& host) const;
};

extern GaggiMateStatusClient gaggimate_status_client;
