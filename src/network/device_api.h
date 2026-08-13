#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

class GrindController;
class HardwareManager;

class DeviceApi {
public:
    void init(AsyncWebServer* server, HardwareManager* hardware,
              GrindController* grind_controller);
    void update();
    void process_commands();

private:
    enum class CommandAction : uint8_t { STOP, DISMISS };
    struct Command {
        uint32_t client_id;
        CommandAction action;
    };

    static constexpr size_t MAX_CLIENTS = 4;
    static constexpr uint32_t PUBLISH_INTERVAL_MS = 100;

    AsyncWebSocket websocket_{"/ws"};
    HardwareManager* hardware_ = nullptr;
    GrindController* grind_controller_ = nullptr;
    QueueHandle_t command_queue_ = nullptr;
    std::atomic<uint32_t> client_ids_[MAX_CLIENTS]{};
    uint32_t last_publish_ms_ = 0;
    std::atomic<uint32_t> sequence_{0};
    bool initialized_ = false;

    void handle_event(AsyncWebSocket* server, AsyncWebSocketClient* client,
                      AwsEventType type, void* arg, uint8_t* data, size_t len);
    void add_client(AsyncWebSocketClient* client);
    void remove_client(uint32_t client_id);
    void queue_command(uint32_t client_id, const uint8_t* data, size_t len);
    void send_ack(uint32_t client_id, const char* action, bool accepted,
                  const char* reason);
    String build_state_message();
};

extern DeviceApi device_api;
