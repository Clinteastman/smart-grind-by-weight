#pragma once

#include <ESPAsyncWebServer.h>
#include <atomic>

class GrindController;
class HardwareManager;
class BluetoothManager;

class DeviceWebServer {
public:
    void init(HardwareManager* hardware_manager, GrindController* grind_controller,
              BluetoothManager* bluetooth_manager);
    void begin();
    void update();
    bool arm_ota();
    bool is_ota_armed() const;
    bool is_ota_active() const { return ota_active_.load(); }
    uint32_t ota_seconds_remaining() const;
    uint8_t ota_progress_percent() const;
    AsyncWebServer& server() { return server_; }

private:
    AsyncWebServer server_{80};
    bool initialized_ = false;
    bool started_ = false;
    std::atomic<bool> ota_armed_{false};
    std::atomic<bool> ota_active_{false};
    std::atomic<bool> reboot_pending_{false};
    std::atomic<uint32_t> ota_armed_until_ms_{0};
    std::atomic<uint32_t> reboot_at_ms_{0};
    std::atomic<size_t> ota_received_{0};
    std::atomic<size_t> ota_total_{0};
    std::atomic<uint64_t> ota_token_{0};
    HardwareManager* hardware_manager_ = nullptr;
    GrindController* grind_controller_ = nullptr;
    BluetoothManager* bluetooth_manager_ = nullptr;

    void configure_routes();
    void handle_ota_upload(AsyncWebServerRequest* request, const String& filename,
                           size_t index, uint8_t* data, size_t len, bool final);
    void finish_ota(bool success);
    String ota_token_string() const;
};

extern DeviceWebServer device_web_server;
