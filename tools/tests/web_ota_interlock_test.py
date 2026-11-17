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
        http_source = (ROOT / "src/network/http_support.cpp").read_text()
        http_header = (ROOT / "src/network/http_support.h").read_text()
        # The upload pumps its body through the production http::stream_body.
        body_pump = "\n".join(re.findall(r"^constexpr \w+ BODY_\w+ = [^;]+;$", http_header, re.M))
        body_pump += "\n" + function(http_source, "bool stream_body(httpd_req_t* request, const BodySink& sink)")
        header = (ROOT / "src/network/device_web_server.h").read_text()
        header = re.sub(r'^#(?:include|pragma).*$', '', header, flags=re.M).replace("private:", "public:")
        constants = "\n".join(re.findall(r"^constexpr \w+ (?:OTA|UPDATE_CHECK)_\w+ = [^;]+;$", source, re.M))
        methods = "\n".join(function(source, sig) for sig in (
            "bool DeviceWebServer::request_ota_preparation()",
            "bool DeviceWebServer::is_ota_ready() const",
            "bool DeviceWebServer::device_busy() const",
            "const char* DeviceWebServer::busy_reason() const",
            "bool DeviceWebServer::internal_heap_ok() const",
            "void DeviceWebServer::recover_from_ota_failure()",
            "void DeviceWebServer::finish_ota(",
            "esp_err_t DeviceWebServer::handle_ota_upload(",
            "bool DeviceWebServer::claim_prepared_update()",
            "bool DeviceWebServer::start_github_ota(",
            "bool DeviceWebServer::install_available_update()",
            "void DeviceWebServer::update()",
        ))
        harness = r'''
#include "system/operation_interlock.h"
#include "system/device_info.h"
#include "system/string_utils.h"
#include "network/http_multipart.cpp" // The real streaming parser feeds the writer.
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <functional>
#include <future>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <vector>
#define SMART_GRIND_SIM 1
#define LOG_BLE(...) ((void)0)
using esp_err_t = int;
using httpd_handle_t = void*;
constexpr esp_err_t ESP_OK = 0;
constexpr int HTTPD_SOCK_ERR_FAIL = -1, HTTPD_SOCK_ERR_TIMEOUT = -3;
// The body arrives in small reads so multipart boundaries straddle chunks. A
// read at drop_at returns drop_result (0: peer closed, negative: socket error).
struct httpd_req_t {
    size_t content_len = 0, offset = 0, chunk = 61, drop_at = SIZE_MAX;
    int drop_result = 0;
    unsigned timeouts = 0; // HTTPD_SOCK_ERR_TIMEOUT results before the next read.
    std::string content_type, body, response;
    std::function<void(httpd_req_t*)> on_recv; // Runs before every read.
    std::atomic<int> status{0};
};
int httpd_req_recv(httpd_req_t* r, char* buffer, size_t length) {
    if (r->on_recv) r->on_recv(r);
    if (r->timeouts) { --r->timeouts; return HTTPD_SOCK_ERR_TIMEOUT; }
    const size_t end = std::min(r->body.size(), r->drop_at);
    if (r->offset >= end) return r->drop_result;
    const size_t n = std::min({length, r->chunk, end - r->offset});
    memcpy(buffer, r->body.data() + r->offset, n); r->offset += n;
    return static_cast<int>(n);
}
namespace http {
esp_err_t send(httpd_req_t* r, int status, const char*, const std::string& body) {
    assert(r->status == 0); r->response = body; r->status = status; return ESP_OK;
}
std::string header(httpd_req_t* r, const char* name) {
    return std::string(name) == "Content-Type" ? r->content_type : std::string();
}
}
const std::string boundary = "----SmartGrindOta";
void load(httpd_req_t& r, char magic='\xE9', const std::string& filename="firmware.bin") {
    std::string image(300, 'Z'); image[0] = magic;
    r.content_type = "multipart/form-data; boundary=" + boundary;
    r.body = "--" + boundary + "\r\nContent-Disposition: form-data; name=\"firmware\"; "
             "filename=\"" + filename + "\"\r\nContent-Type: application/octet-stream\r\n\r\n" +
             image + "\r\n--" + boundary + "--\r\n";
    r.content_len = r.body.size();
}
uint32_t now = 100;
uint32_t millis() { return now; }
namespace http {
using BodySink = std::function<bool(const uint8_t* data, size_t length)>;
''' + body_pump + r'''
}
''' + constants + r'''
constexpr int pdPASS=1;
size_t free_heap=100000;
size_t device_info::free_internal_heap_bytes() { return free_heap; }
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
unsigned restarts=0;
void esp_restart() { ++restarts; }
struct esp_partition_t { size_t size=1000000; } partition;
const esp_partition_t* esp_ota_get_next_update_partition(void*) { return &partition; }
// Mirrors OtaWriter: a failed write discards the open image itself, a refused
// header is reported as such, and abort() of a closed writer is a no-op.
struct OtaWriter {
    bool fail_begin=false, fail_write=false, refuse_header=false, valid=true, opened=false;
    bool refused=false;
    unsigned begins=0; size_t written=0;
    bool begin(size_t) { assert(!opened); ++begins; written=0; refused=false; opened=!fail_begin; return opened; }
    bool write(const uint8_t* data, size_t length) {
        assert(opened && length && (written || data[0]==0xE9));
        if (refuse_header) { opened=false; refused=true; return false; }
        if (fail_write) { opened=false; return false; }
        written+=length; return true;
    }
    bool end() { assert(opened); opened=false; return valid; }
    void abort() { opened=false; }
    bool image_refused() const { return refused; }
    const char* error() const { return refused ? "Not Smart Grind firmware" : "injected"; }
} web_firmware_update;
bool task_fails=false;
unsigned github_tasks=0;
int xTaskCreate(void(*)(void*), const char* name, size_t, void* parameter, int, void*) {
    if (task_fails) return 0;
    // Consume successful ownership transfer without performing network I/O.
    if (std::string(name)=="github_ota") { delete static_cast<std::string*>(parameter); ++github_tasks; }
    return pdPASS;
}
''' + header + grind_start + r'''
DeviceWebServer device_web_server;
void DeviceWebServer::github_ota_task(void*) {}
void DeviceWebServer::firmware_update_check_task(void*) {}
std::string DeviceWebServer::latest_release_tag() const { return "v1.5.8"; }
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
// Bounded wait, so a deadlock fails an assertion instead of hanging the test.
bool eventually(const std::function<bool()>& done) {
    for (int i=0; i<2000 && !done(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return done();
}
int main() {
    DeviceWebServer web; setup(web);
    // One rule says why the device cannot take an update; every route uses it.
    assert(!web.busy_reason() && !web.device_busy());
    controller.active=true; assert(web.busy_reason() && web.device_busy()); controller.active=false;
    bluetooth.transferring=true; assert(web.busy_reason()); bluetooth.transferring=false;
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
    httpd_req_t unprepared; load(unprepared); web.handle_ota_upload(&unprepared);
    assert(unprepared.status==409 && unprepared.offset==0 && !web_firmware_update.begins);
    assert_available();
    // Only one simultaneous upload may claim preparation. The winner pauses
    // part-way through the image until the loser has been answered.
    ready(web);
    httpd_req_t a, b; load(a); load(b);
    a.drop_at=b.drop_at=a.body.size()-4; // The peer then closes before the final boundary.
    std::atomic<httpd_req_t*> winner{nullptr};
    OperationInterlock::Token claimed=0; size_t written_at_refusal=0;
    a.on_recv=b.on_recv=[&](httpd_req_t* r) {
        if (r->offset<200 || winner.exchange(r)) return;
        httpd_req_t& other=r==&a ? b : a;
        assert(eventually([&]{ return other.status.load()!=0; }));
        claimed=web.operation_token_; written_at_refusal=web_firmware_update.written;
        assert(web_firmware_update.opened && operation_interlock().owns(claimed));
    };
    std::thread first([&]{ web.handle_ota_upload(&a); });
    std::thread second([&]{ web.handle_ota_upload(&b); });
    first.join(); second.join();
    assert(winner.load());
    httpd_req_t& active=*winner.load(); httpd_req_t& refused=&active==&a ? b : a;
    assert(refused.status==409 && refused.offset==0); // The loser never read its body.
    assert(written_at_refusal>0 && web_firmware_update.written>written_at_refusal);
    // A closed connection (recv returns 0) aborts the image and releases.
    assert(active.status==500 && !web_firmware_update.opened && web.ota_failed());
    assert(!web.is_ota_active() && !operation_interlock().owns(claimed)); assert_available();
    // Handlers are synchronous, so a stale request cannot outlive its upload.
    // Instead check that a request and service-loop work arriving mid-stream,
    // from another task (the claim lock is not held while streaming), can
    // neither take over nor end the active upload.
    ready(web); httpd_req_t current, late; load(current); load(late);
    bool probed=false;
    current.on_recv=[&](httpd_req_t* r) {
        if (probed || r->offset<200) return;
        probed=true;
        const auto token=web.operation_token_;
        auto probe=std::async(std::launch::async, [&]{
            web.handle_ota_upload(&late); assert(late.status==409 && late.offset==0);
            assert(!web.request_ota_preparation() && !web.start_github_ota("v1.5.8"));
            web.recover_from_ota_failure(); web.update();
            assert(!controller.start_grind(18,5000,GrindMode::TIME));
        });
        assert(probe.wait_for(std::chrono::seconds(2))==std::future_status::ready); probe.get();
        assert(web.is_ota_active() && operation_interlock().owns(token) && web_firmware_update.opened);
        // Socket timeouts are retried; a socket error then aborts.
        r->timeouts=2; r->drop_at=r->offset+r->chunk; r->drop_result=HTTPD_SOCK_ERR_FAIL;
    };
    web.handle_ota_upload(&current);
    assert(probed && current.offset==current.drop_at && current.status==500);
    assert(!web_firmware_update.opened && !web.is_ota_active()); assert_available();
    // A peer that goes silent mid-image without closing is abandoned once it
    // has sent nothing for BODY_IDLE_TIMEOUT_MS, releasing the reservation.
    ready(web); httpd_req_t stalled; load(stalled);
    stalled.on_recv=[&](httpd_req_t* r) {
        if (r->offset<200) return;
        r->timeouts=1; now+=1000; // Every further read times out as time passes.
    };
    const uint32_t stall_started=now;
    web.handle_ota_upload(&stalled);
    assert(stalled.status==500 && stalled.offset>=200 && stalled.offset<stalled.body.size());
    assert(now-stall_started>=http::BODY_IDLE_TIMEOUT_MS);
    assert(!web_firmware_update.opened && !web.is_ota_active()); assert_available();
    // Failed writer begin/write/validation, a non-image, a file that is not a
    // .bin and a non-multipart upload each give the answer the Arduino firmware
    // gave, clean up and permit retry. An image whose header the writer refuses
    // is answered with the writer's reason.
    const struct { int status; const char* message; } expected[]={
        {500,"Could not open the inactive firmware partition"},
        {500,"Firmware write failed"},
        {500,"Firmware update failed"},
        {400,"Not a valid ESP32 firmware image"},
        {400,"Not a valid ESP32 firmware image"},
        {400,"Expected a multipart/form-data firmware upload"},
        {400,"Not Smart Grind firmware"},
    };
    for (unsigned failure=0; failure<7; ++failure) {
        ready(web); httpd_req_t request;
        load(request, failure==3 ? '\0' : '\xE9', failure==4 ? "firmware.elf" : "firmware.bin");
        if (failure==5) request.content_type="application/octet-stream";
        web_firmware_update.fail_begin=failure==0;
        web_firmware_update.fail_write=failure==1;
        web_firmware_update.valid=failure!=2;
        web_firmware_update.refuse_header=failure==6;
        const unsigned begins=web_firmware_update.begins;
        web.handle_ota_upload(&request);
        assert(request.status==expected[failure].status && request.response==expected[failure].message);
        assert(web.ota_failed() && (web_firmware_update.begins>begins)==(failure<3 || failure==6));
        assert(!web.is_ota_active() && !web_firmware_update.opened); assert_available();
    }
    web_firmware_update.fail_begin=false; web_firmware_update.fail_write=false; web_firmware_update.valid=true;
    web_firmware_update.refuse_header=false;
    ready(web); task_fails=true;
    assert(!web.start_github_ota("v1.5.8")); assert_available(); task_fails=false;
    ready(web); assert(web.start_github_ota("v1.5.8"));
    web.recover_from_ota_failure(); // Preparation cleanup cannot release an active transfer.
    assert(!operation_interlock().try_acquire()); web.finish_ota(false); assert_available();
    // The on-device installer reserves through the same interlock.
    web.firmware_update_state_=FirmwareUpdateState::AVAILABLE;
    auto holder=operation_interlock().try_acquire();
    assert(!web.install_available_update() && !web.on_device_update_pending_);
    operation_interlock().release(holder);
    assert(web.install_available_update()); assert(!operation_interlock().try_acquire());
    task_fails=true; web.update(); task_fails=false;
    assert(!web.is_ota_active() && !web.on_device_update_pending_); assert_available();
    // The service loop's on-device install and a browser upload race for one
    // prepared window; exactly one may claim it.
    ready(web); web.on_device_update_pending_=true;
    httpd_req_t browser; load(browser); browser.drop_at=browser.body.size()-4;
    const unsigned tasks=github_tasks;
    std::thread service([&]{ web.update(); });
    std::thread upload([&]{ web.handle_ota_upload(&browser); });
    service.join(); upload.join();
    assert((github_tasks!=tasks)==(browser.status==409));
    if (browser.status==409) { assert(web.is_ota_active()); web.finish_ota(false); }
    assert(!web.is_ota_active() && !web.on_device_update_pending_); assert_available();
    // The extension check ignores letter case, as the web page's does.
    ready(web); httpd_req_t success; load(success, '\xE9', "FIRMWARE.BIN"); success.timeouts=1;
    web.handle_ota_upload(&success);
    assert(success.status==200 && web_firmware_update.written==300 && !web_firmware_update.opened);
    assert(web.is_ota_active() && web.reboot_pending_ && !operation_interlock().try_acquire());
    assert(!web.request_ota_preparation());
    httpd_req_t after; load(after); web.handle_ota_upload(&after); assert(after.status==409);
    web.update(); assert(restarts==0);
    now += OTA_REBOOT_DELAY_MS; web.update(); assert(restarts==1);
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
