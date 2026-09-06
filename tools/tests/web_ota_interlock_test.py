"""Run production OTA preparation/upload/recovery with concurrent request fakes."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
from controller_serialization_test import function

ROOT = Path(__file__).resolve().parents[2]


class WebOtaInterlockTest(unittest.TestCase):
    def test_reservation_lifecycle(self):
        source = (ROOT / "src/network/device_web_server.cpp").read_text()
        grind_source = (ROOT / "src/controllers/grind_controller.cpp").read_text()
        # Execute all production grind admission checks and reservation. The
        # session state machine after admission is exercised by other tests.
        grind_start = function(grind_source, "bool GrindController::start_grind(")
        grind_start = grind_start.split("    // Finish pending history writes", 1)[0] + "return true;\n}"
        header = (ROOT / "src/network/device_web_server.h").read_text()
        header = re.sub(r'^#(?:include|pragma).*$', '', header, flags=re.M).replace("private:", "public:")
        methods = "\n".join(function(source, sig) for sig in (
            "bool DeviceWebServer::request_ota_preparation()",
            "bool DeviceWebServer::is_ota_ready() const",
            "void DeviceWebServer::recover_from_ota_failure()",
            "void DeviceWebServer::finish_ota(",
            "void DeviceWebServer::handle_ota_upload(",
            "bool DeviceWebServer::start_github_ota(",
            "void DeviceWebServer::update()",
        ))
        state = re.search(r"struct OtaRequestState \{.*?\};", source, re.S).group()
        harness = r'''
#include "system/operation_interlock.h"
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <cstdint>
#include <functional>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <algorithm>
#define SMART_GRIND_SIM 1
#define LOG_BLE(...) ((void)0)
struct String : std::string {
    using std::string::string;
    bool isEmpty() const { return empty(); }
    bool endsWith(const char* suffix) const {
        std::string s(suffix); return size() >= s.size() && compare(size()-s.size(),s.size(),s)==0;
    }
};
struct AsyncWebServer { explicit AsyncWebServer(int) {} };
struct AsyncWebServerRequest {
    void* _tempObject = nullptr;
    int response = 0;
    std::function<void()> disconnected;
    bool getResponse() const { return response != 0; }
    void send(int code, const char*, const char*) { response = code; }
    size_t contentLength() const { return 1024; }
    void onDisconnect(std::function<void()> callback) { disconnected = callback; }
    ~AsyncWebServerRequest() { free(_tempObject); }
};
uint32_t now = 100;
uint32_t millis() { return now; }
constexpr uint32_t OTA_REBOOT_DELAY_MS=1500, OTA_PREPARE_TIMEOUT_MS=15000, OTA_READY_WINDOW_MS=30000;
constexpr uint32_t UPDATE_CHECK_RETRY_MS=300000, UPDATE_CHECK_INTERVAL_MS=1800000;
constexpr size_t OTA_MIN_INTERNAL_HEAP=65536, OTA_DOWNLOAD_TASK_STACK=12288, UPDATE_CHECK_TASK_STACK=10240;
constexpr int MALLOC_CAP_INTERNAL=1, MALLOC_CAP_8BIT=2, pdPASS=1, U_FLASH=0;
constexpr size_t UPDATE_SIZE_UNKNOWN=0;
size_t free_heap=100000;
size_t heap_caps_get_free_size(int) { return free_heap; }
struct { bool connected=true; bool is_connected() const { return connected; } } network_manager;
enum class GrindMode { WEIGHT, TIME, MANUAL };
enum class GrindPhase { IDLE, INITIALIZING };
enum class GrinderPurgeMode { PRIME, PURGE };
constexpr int GRIND_PURGE_MODE_DEFAULT=1;
constexpr float GRIND_PURGE_AMOUNT_DEFAULT_G=1, GRIND_PURGE_AMOUNT_MIN_G=0.1f, GRIND_PURGE_AMOUNT_MAX_G=5;
struct Sensor {
    bool fresh=true, fault=false;
    bool has_recent_sample() { return fresh; }
    bool has_hardware_fault() { return fault; }
    int get_hardware_fault() { return fault; }
} sensor;
struct Preferences {
    int getInt(const char*,int value) { return value; }
    float getFloat(const char*,float value) { return value; }
};
struct GrindMotor { bool initialized=true; bool is_initialized() { return initialized; } } grind_motor;
struct GrindController {
    std::recursive_mutex control_mutex;
    auto lock_control() { return std::unique_lock<std::recursive_mutex>(control_mutex); }
    bool active=false;
    bool is_active() const { return active; }
    GrindPhase phase=GrindPhase::IDLE;
    GrindMotor* grinder=&grind_motor;
    Sensor* weight_sensor=&sensor;
    Preferences* preferences=nullptr;
    GrinderPurgeMode grinder_purge_mode_for_session{};
    float grinder_purge_amount_g_for_session=0;
    const char* PREF_KEY_GRINDER_MODE="mode";
    const char* PREF_KEY_GRINDER_AMOUNT_G="amount";
    OperationInterlock::Token operation_token_=0;
    bool start_grind(float,uint32_t,GrindMode);
} controller;
struct BluetoothManager {
    bool enabled=false, transferring=false;
    bool is_enabled() const { return enabled; }
    bool is_transfer_active() const { return transferring; }
    void disable() { enabled=false; }
} bluetooth;
struct Grinder { unsigned stops=0; void stop() { assert(!operation_interlock().try_acquire()); ++stops; } } motor;
struct HardwareManager { Grinder* get_grinder() { return &motor; } } hardware;
struct { void update() {} } device_api;
struct { void flush() {} } Serial;
struct { unsigned restarts=0; void restart() { ++restarts; } } ESP;
struct esp_partition_t { size_t size=1000000; } partition;
const esp_partition_t* esp_ota_get_next_update_partition(void*) { return &partition; }
struct Update {
    bool fail_begin=false, fail_write=false, valid=true, opened=false;
    unsigned aborts=0, writes=0;
    bool begin(size_t,int) { opened=!fail_begin; return opened; }
    size_t write(uint8_t*,size_t len) { assert(opened); ++writes; return fail_write ? 0 : len; }
    bool end(bool) { opened=false; return valid; }
    void abort() { opened=false; ++aborts; }
    const char* errorString() { return "injected"; }
} web_firmware_update;
bool task_fails=false;
int xTaskCreate(void(*)(void*), const char* name, size_t, void* parameter, int, void*) {
    if (task_fails) return 0;
    // Consume successful ownership transfer without performing network I/O.
    if (std::string(name)=="github_ota") delete static_cast<String*>(parameter);
    return pdPASS;
}
''' + header + state + grind_start + r'''
DeviceWebServer device_web_server;
void DeviceWebServer::github_ota_task(void*) {}
void DeviceWebServer::firmware_update_check_task(void*) {}
String DeviceWebServer::latest_release_tag() const { return "v1.5.8"; }
''' + methods + r'''
void setup(DeviceWebServer& web) {
    web.initialized_=true; web.grind_controller_=&controller;
    web.hardware_manager_=&hardware; web.bluetooth_manager_=&bluetooth;
    web.last_firmware_update_check_ms_=now;
}
void assert_available() {
    auto t=operation_interlock().try_acquire(); assert(t); operation_interlock().release(t);
}
void ready(DeviceWebServer& web) {
    assert(web.request_ota_preparation());
    assert(!operation_interlock().try_acquire());
    web.update(); assert(web.is_ota_ready());
    assert(!operation_interlock().try_acquire());
}
int main() {
    DeviceWebServer web; setup(web);
    sensor.fresh=false;
    assert(!controller.start_grind(18,0,GrindMode::WEIGHT)); assert_available();
    sensor.fresh=true;
    for (auto mode : {GrindMode::WEIGHT, GrindMode::TIME, GrindMode::MANUAL}) {
        assert(controller.start_grind(18,5000,mode));
        assert(!web.request_ota_preparation());
        assert(operation_interlock().owns(controller.operation_token_));
        operation_interlock().release(controller.operation_token_);
    }
    auto competitor=operation_interlock().try_acquire();
    assert(!web.request_ota_preparation());
    assert(web.ota_preparation_state_==OtaPreparationState::IDLE);
    assert(operation_interlock().owns(competitor)); operation_interlock().release(competitor);
    ready(web);
    for (auto mode : {GrindMode::WEIGHT, GrindMode::TIME, GrindMode::MANUAL}) {
        assert(!controller.start_grind(18,5000,mode));
    }
    assert(!web.request_ota_preparation()); // Do not extend a prepared window.
    now += OTA_READY_WINDOW_MS; assert(!web.is_ota_ready()); web.update();
    assert(!web.is_ota_ready()); assert_available();
    ready(web);
    // Only one simultaneous upload may claim preparation.
    uint8_t bytes[]={0xE9,0,0}; AsyncWebServerRequest a,b;
    std::thread first([&]{web.handle_ota_upload(&a,"firmware.bin",0,bytes,3,false);});
    std::thread second([&]{web.handle_ota_upload(&b,"firmware.bin",0,bytes,3,false);});
    first.join(); second.join();
    auto& active=a.response==409 ? b : a;
    assert((a.response==409)!=(b.response==409));
    assert(web.is_ota_active() && !operation_interlock().try_acquire());
    active.disconnected(); assert(!web.is_ota_active()); assert_available();
    ready(web);
    AsyncWebServerRequest newer;
    web.handle_ota_upload(&newer,"firmware.bin",0,bytes,3,false);
    auto token=web.operation_token_;
    active.disconnected(); // Old request cannot abort a new upload.
    assert(web.is_ota_active() && operation_interlock().owns(token));
    unsigned writes=web_firmware_update.writes;
    web.handle_ota_upload(&active,"firmware.bin",3,bytes,3,false);
    assert(web_firmware_update.writes==writes);
    newer.disconnected(); assert_available();
    // Failed writer allocation/validation cleans up and permits retry.
    for (unsigned failure=0; failure<3; ++failure) {
        ready(web); AsyncWebServerRequest request;
        web_firmware_update.fail_begin=failure==0;
        web_firmware_update.fail_write=failure==1;
        web_firmware_update.valid=failure!=2;
        web.handle_ota_upload(&request,"firmware.bin",0,bytes,3,true);
        assert(!web.is_ota_active() && !web_firmware_update.opened); assert_available();
    }
    web_firmware_update.fail_begin=false; web_firmware_update.fail_write=false; web_firmware_update.valid=true;
    ready(web); task_fails=true;
    assert(!web.start_github_ota("v1.5.8")); assert_available(); task_fails=false;
    ready(web); assert(web.start_github_ota("v1.5.8"));
    web.recover_from_ota_failure(); // Preparation cleanup cannot release an active transfer.
    assert(!operation_interlock().try_acquire()); web.finish_ota(false); assert_available();
    ready(web); AsyncWebServerRequest success;
    web.handle_ota_upload(&success,"firmware.bin",0,bytes,3,true);
    assert(web.is_ota_active() && web.reboot_pending_ && !operation_interlock().try_acquire());
    assert(!web.request_ota_preparation());
    now += OTA_REBOOT_DELAY_MS; web.update(); assert(ESP.restarts==1);
    operation_interlock().release(web.operation_token_); // Simulate boot, never used by production.
    // Bluetooth deinit recovery retains ownership until reboot too.
    DeviceWebServer recovery; setup(recovery); bluetooth.enabled=true;
    ready(recovery); now += OTA_READY_WINDOW_MS; recovery.update();
    assert(recovery.reboot_pending_ && recovery.is_ota_active());
    assert(!operation_interlock().try_acquire() && !recovery.request_ota_preparation());
    operation_interlock().release(recovery.operation_token_);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            cpp, binary = Path(tmp) / "web.cpp", Path(tmp) / "web"
            cpp.write_text(harness)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-pthread",
                            "-I", str(ROOT / "src"), str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=20)


if __name__ == "__main__":
    unittest.main()
