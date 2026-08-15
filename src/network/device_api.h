#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <atomic>
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
    bool auto_return = false;
    int purge_mode = 1;
    float purge_amount_g = 1.0f;
    float freshness_hours = 8.0f;
    float coast_ratio = 1.0f;
    bool logging_enabled = false;
    bool swipe_enabled = false;
    int brightness_percent = 100;
    int screensaver_brightness_percent = 35;
    bool screensaver_startup = false;
    bool screensaver_sleep = false;
    uint16_t screensaver_idle_timeout_s = 300;
    uint8_t screensaver_startup_timeout_s = 3;
    char screensaver_style[12] = "minimal";
    bool bluetooth_startup = true;
};

class DeviceApi {
public:
    void init(AsyncWebServer* server, HardwareManager* hardware,
              GrindController* grind_controller, ProfileController* profile_controller);
    void update();
    bool process_commands();
    String settings_json();
    void mark_settings_dirty() { settings_cache_dirty_.store(true); }

private:
    enum class CommandAction : uint8_t { START, STOP, DISMISS, SELECT_PROFILE, APPLY_SETTINGS };
    struct Command {
        uint32_t client_id;
        CommandAction action;
        int profile_index = 0;
        DeviceSettingsUpdate settings;
    };

    static constexpr size_t MAX_CLIENTS = 4;
    static constexpr uint32_t PUBLISH_INTERVAL_MS = 100;

    AsyncWebSocket websocket_{"/ws"};
    HardwareManager* hardware_ = nullptr;
    GrindController* grind_controller_ = nullptr;
    ProfileController* profile_controller_ = nullptr;
    QueueHandle_t command_queue_ = nullptr;
    SemaphoreHandle_t settings_mutex_ = nullptr;
    String settings_json_cache_;
    std::atomic<uint32_t> client_ids_[MAX_CLIENTS]{};
    uint32_t last_publish_ms_ = 0;
    std::atomic<uint32_t> sequence_{0};
    std::atomic<bool> settings_cache_dirty_{false};
    bool initialized_ = false;

    void handle_event(AsyncWebSocket* server, AsyncWebSocketClient* client,
                      AwsEventType type, void* arg, uint8_t* data, size_t len);
    void add_client(AsyncWebSocketClient* client);
    void remove_client(uint32_t client_id);
    void queue_command(uint32_t client_id, const uint8_t* data, size_t len);
    void send_ack(uint32_t client_id, const char* action, bool accepted,
                  const char* reason);
    void configure_settings_routes(AsyncWebServer* server);
    bool queue_profile_selection(AsyncWebServerRequest* request);
    bool queue_settings_update(AsyncWebServerRequest* request);
    bool apply_settings(const DeviceSettingsUpdate& settings);
    void refresh_settings_cache();
    String build_state_message();
};

extern DeviceApi device_api;
