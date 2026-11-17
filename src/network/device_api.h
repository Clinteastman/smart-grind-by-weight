#pragma once

#include <esp_http_server.h>
#include <atomic>
#include <cstdint>
#include <string>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

class GrindController;
class HardwareManager;
class ProfileController;

struct DeviceSettingsUpdate {
    int current_profile = 1;
    int grind_mode = 0;
    float profile_weights[3]{};
    float profile_times[3]{};
    bool auto_start = false;
    float auto_start_threshold_g = 50.0f;
    bool auto_return = false;
    int purge_mode = 1;
    float purge_amount_g = 1.0f;
    float freshness_hours = 8.0f;
    float coast_ratio = 1.0f;
    float motor_latency_ms = 50.0f;
    bool logging_enabled = false;
    bool swipe_enabled = false;
    int brightness_percent = 100;
    int screensaver_brightness_percent = 35;
    bool screensaver_startup = false;
    bool screensaver_sleep = false;
    uint16_t screensaver_idle_timeout_s = 300;
    uint8_t screensaver_startup_timeout_s = 3;
    bool display_off_enabled = false;
    uint16_t display_off_delay_s = 3600;
    bool has_display_off_enabled = true;
    bool has_display_off_delay_s = true;
    char screensaver_style[12] = "minimal";
    char gaggimate_host[64] = "gaggimate.local";
    bool bluetooth_startup = true;
};

class DeviceApi {
public:
    /**
     * Register the API routes and the /ws telemetry socket on an already-started
     * HTTP server.
     */
    void attach_routes(httpd_handle_t server, HardwareManager* hardware,
                       GrindController* grind_controller,
                       ProfileController* profile_controller);
    void update();
    bool process_commands();
    // UI task calls this only after reloading runtime settings.
    void complete_settings_application(bool runtime_applied);
    std::string settings_json();
    void mark_settings_dirty() { settings_cache_dirty_.store(true); }

private:
    enum class CommandAction : uint8_t {
        START,
        START_MANUAL,
        STOP,
        DISMISS,
        TARE,
        SELECT_PROFILE,
        SET_MODE,
        APPLY_SETTINGS
    };
    struct Command {
        // Socket descriptor of the WebSocket client, or -1 for HTTP requests.
        int client_fd;
        CommandAction action;
        uint32_t request_id = 0;
        bool has_request_id = false;
        int profile_index = 0;
        int grind_mode = 0;
        DeviceSettingsUpdate settings;
    };

    static constexpr size_t MAX_CLIENTS = 4;
    static constexpr uint32_t PUBLISH_INTERVAL_MS = 100;
    static constexpr uint8_t MAX_CONSECUTIVE_BACKPRESSURE_SKIPS = 50;
    static constexpr uint32_t WS_SEND_MUTEX_TIMEOUT_MS = 50;

    static constexpr int NO_CLIENT = -1;

    httpd_handle_t server_ = nullptr;
    HardwareManager* hardware_ = nullptr;
    GrindController* grind_controller_ = nullptr;
    ProfileController* profile_controller_ = nullptr;
    QueueHandle_t command_queue_ = nullptr;
    SemaphoreHandle_t settings_mutex_ = nullptr;
    // Frames are written from the HTTP server task, the UI task and the
    // service loop, so socket writes have to be serialised.
    SemaphoreHandle_t ws_send_mutex_ = nullptr;
    std::string settings_json_cache_;
    struct SettingsResult {
        uint32_t id = 0;
        const char* status = "unknown";
    };
    SettingsResult settings_results_[4]{}; // Protected by settings_mutex_.
    uint32_t next_settings_id_ = 0;
    size_t next_settings_result_ = 0;
    uint32_t applying_settings_id_ = 0; // UI task only.
    uint32_t settings_operation_token_ = 0;
    bool settings_persisted_ = false;
    std::atomic<int> client_fds_[MAX_CLIENTS]{};
    std::atomic<uint8_t> backpressure_skips_[MAX_CLIENTS]{};
    uint32_t last_publish_ms_ = 0;
    std::atomic<uint32_t> sequence_{0};
    std::atomic<bool> settings_cache_dirty_{false};
    bool initialized_ = false;

    esp_err_t handle_websocket(httpd_req_t* request);
    void add_client(int client_fd);
    void remove_client(int client_fd);
    /** True when the socket holds a client slot, i.e. passed the handshake checks. */
    bool is_client(int client_fd) const;
    void queue_command(int client_fd, const uint8_t* data, size_t len);
    /** Send one text frame; false when the socket is gone or would block. */
    bool send_text(int client_fd, const std::string& message);
    /** True when the socket can accept a write without blocking. */
    static bool socket_writable(int client_fd);
    void send_ack(int client_fd, uint32_t request_id, bool has_request_id,
                  const char* action, bool accepted, const char* reason);
    void send_ack(const Command& command, const char* action, bool accepted,
                  const char* reason);
    void configure_settings_routes();
    esp_err_t queue_profile_selection(httpd_req_t* request);
    esp_err_t queue_settings_update(httpd_req_t* request);
    bool apply_settings(const DeviceSettingsUpdate& settings);
    uint32_t reserve_settings_result();
    void set_settings_result(uint32_t id, const char* status);
    const char* settings_result(uint32_t id);
    void refresh_settings_cache();
    std::string build_state_message();
};

extern DeviceApi device_api;
