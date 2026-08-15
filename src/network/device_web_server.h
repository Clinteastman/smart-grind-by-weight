#pragma once

#include <ESPAsyncWebServer.h>
#include <atomic>

class GrindController;
class HardwareManager;
class BluetoothManager;
class ProfileController;

enum class OtaPreparationState : uint8_t {
    IDLE,
    REQUESTED,
    READY,
};

class DeviceWebServer {
public:
    void init(HardwareManager* hardware_manager, GrindController* grind_controller,
              BluetoothManager* bluetooth_manager, ProfileController* profile_controller);
    void begin();
    void update();
    bool is_ota_active() const { return ota_active_.load(); }
    bool is_ota_ready() const;
    bool is_ota_preparing() const {
        return ota_preparation_state_.load() == OtaPreparationState::REQUESTED;
    }
    uint8_t ota_progress_percent() const;
    AsyncWebServer& server() { return server_; }

private:
    AsyncWebServer server_{80};
    bool initialized_ = false;
    bool started_ = false;
    std::atomic<bool> ota_active_{false};
    std::atomic<OtaPreparationState> ota_preparation_state_{OtaPreparationState::IDLE};
    std::atomic<uint32_t> ota_preparation_deadline_ms_{0};
    std::atomic<bool> ota_bluetooth_stopped_{false};
    std::atomic<bool> reboot_pending_{false};
    std::atomic<uint32_t> reboot_at_ms_{0};
    std::atomic<size_t> ota_received_{0};
    std::atomic<size_t> ota_total_{0};
    HardwareManager* hardware_manager_ = nullptr;
    GrindController* grind_controller_ = nullptr;
    BluetoothManager* bluetooth_manager_ = nullptr;

    void configure_routes();
    void handle_ota_upload(AsyncWebServerRequest* request, const String& filename,
                           size_t index, uint8_t* data, size_t len, bool final);
    bool start_github_ota(const String& tag);
    static void github_ota_task(void* parameter);
    void perform_github_ota(const String& tag);
    void request_ota_preparation();
    void recover_from_ota_failure();
    void finish_ota(bool success);
};

extern DeviceWebServer device_web_server;
