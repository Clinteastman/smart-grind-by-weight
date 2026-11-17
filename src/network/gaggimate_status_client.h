#pragma once

#include <esp_websocket_client.h>

#include <cstdint>
#include <string>
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
    bool configure(bool enabled, const std::string& host);
    GaggiMateStatus status() const;
    /** Accepts a hostname or dotted address this client can reach. */
    static bool is_valid_host(const std::string& host);
    std::string configured_host() const;

private:
    static constexpr uint32_t OFFLINE_GRACE_MS = 5000;
    static constexpr uint32_t HTTP_FALLBACK_INTERVAL_MS = 5000;
    // Both the client's own reconnect interval and the retry after a failed start.
    static constexpr uint32_t WEBSOCKET_RETRY_MS = 3000;
    // A status frame larger than this is dropped rather than buffered.
    static constexpr size_t WEBSOCKET_FRAME_MAX_BYTES = 4096;

    mutable SemaphoreHandle_t mutex_ = nullptr;
    TaskHandle_t task_handle_ = nullptr;
    esp_websocket_client_handle_t websocket_ = nullptr;
    bool enabled_ = false;
    bool reconnect_requested_ = false;
    std::string host_;
    std::string connected_host_;
    // The text frame being received, only touched on the WebSocket client's task.
    std::string websocket_frame_;
    GaggiMateStatus status_{};
    uint32_t last_success_ms_ = 0;
    uint32_t last_http_poll_ms_ = 0;
    uint32_t last_websocket_start_ms_ = 0;

    bool ensure_task();
    static void task_entry(void* context);
    void task_loop();
    void start_websocket(const std::string& host);
    void stop_websocket();
    /** esp_websocket_client event trampoline. */
    static void websocket_event_handler(void* context, esp_event_base_t base, int32_t id,
                                        void* data);
    bool apply_status_payload(const std::string& payload, bool require_event_type);
    void poll_http_fallback(const std::string& host);
    void mark_offline_if_stale(uint32_t now_ms);
    bool read_configuration(bool& enabled, std::string& host) const;
};

extern GaggiMateStatusClient gaggimate_status_client;
