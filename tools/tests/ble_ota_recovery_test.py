"""Execute the production OTA handler against host-side fault-injection fakes."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

STUBS = r'''
#include <algorithm>
#include <array>
#include <cstdint>
#include <climits>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <map>
#include <cassert>
#include <stdexcept>
#include <vector>
#include "system/operation_interlock.h"
#include "system/device_info.h"
#include "system/string_utils.h"
#define LOG_BLE(...) ((void)0)
#define LOG_OTA_DEBUG(...) ((void)0)
#define BUILD_NUMBER 1
#define BUILD_FIRMWARE_VERSION "test"
#define CONFIG_ESP_TASK_WDT_TIMEOUT_S 5
#define CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0 1
#define CONFIG_ESP_TASK_WDT_PANIC 1
using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_OTA_VALIDATE_FAILED = 0x1503;
constexpr int ESP_PARTITION_TYPE_DATA=1, ESP_PARTITION_SUBTYPE_DATA_SPIFFS=130;
// The image header check OtaWriter applies before anything reaches flash.
namespace firmware_image {
constexpr size_t HEADER_BYTES = 288;
const char* verdict = nullptr;
const char* check_header(const uint8_t*) { return verdict; }
}
const char* esp_err_to_name(esp_err_t) { return "injected failure"; }
struct Preferences {
    std::map<std::string, std::string> values;
    void putString(const char* k, const std::string& v) { values[k]=v; }
    std::string getString(const char* k, const char* fallback) {
        return values.count(k) ? values[k] : std::string(fallback);
    }
    void remove(const char* k) { values.erase(k); }
};
struct esp_task_wdt_config_t {
    uint32_t timeout_ms; uint32_t idle_core_mask; bool trigger_panic;
};
esp_task_wdt_config_t last_watchdog{};
int watchdog_error=0, init_error=0, write_error=0;
int esp_task_wdt_reconfigure(const esp_task_wdt_config_t* c) {
    if (watchdog_error) return watchdog_error;
    last_watchdog=*c; return ESP_OK;
}
struct Restart : std::exception {};
void esp_restart() { throw Restart{}; }
typedef uint32_t TickType_t;
#define configTICK_RATE_HZ 1000 // CONFIG_FREERTOS_HZ in sdkconfig.defaults
#define pdMS_TO_TICKS(ms) ((TickType_t)((TickType_t)(ms) * configTICK_RATE_HZ / 1000U))
void vTaskDelay(TickType_t) {}
uint32_t device_info::cpu_frequency_mhz() { return 240; }
struct Touch { bool disabled=false; void disable(){disabled=true;} void enable(){disabled=false;} } touch;
struct Display { Touch* get_touch_driver(){return &touch;} } display;
struct HardwareManager { Display* get_display(){return &display;} } hardware_manager;
struct {
    bool suspended=false; int resumes=0;
    void suspend_hardware_tasks(){suspended=true;}
    void resume_hardware_tasks(){suspended=false; ++resumes;}
} task_manager;
// The patch partition (`partition`), the running image and the inactive slot.
struct esp_partition_t { const char* label; uint32_t address; uint32_t size; }
    partition{"patch",0,8192}, running{"ota_0",0,0x300000}, inactive{"ota_1",0,0x300000};
bool partition_present=true;
int init_calls=0;
std::vector<uint8_t> patch_flash(8192, 0xff);
const esp_partition_t* esp_partition_find_first(int,int,const char*){
    return partition_present ? &partition : nullptr;
}
esp_err_t esp_partition_erase_range(const esp_partition_t* p, size_t offset, size_t size){
    assert(p==&partition && offset%4096==0 && size%4096==0 && offset+size<=p->size);
    if (offset==0) ++init_calls;
    if (init_error) return init_error;
    std::fill(patch_flash.begin()+offset, patch_flash.begin()+offset+size, 0xff);
    return ESP_OK;
}
esp_err_t esp_partition_write(const esp_partition_t* p, size_t offset, const void* data, size_t size){
    assert(p==&partition && offset+size<=p->size);
    if (write_error) return write_error;
    memcpy(patch_flash.data()+offset, data, size);
    return ESP_OK;
}
// The running image reads as a position-dependent pattern.
uint8_t running_byte(size_t offset) { return static_cast<uint8_t>(offset*13+5); }
esp_err_t esp_partition_read(const esp_partition_t* p, size_t offset, void* out, size_t size){
    auto* bytes=static_cast<uint8_t*>(out);
    if (p==&partition) { memcpy(bytes, patch_flash.data()+offset, size); return ESP_OK; }
    assert(p==&running);
    for (size_t i=0;i<size;++i) bytes[i]=running_byte(offset+i);
    return ESP_OK;
}
const esp_partition_t* esp_ota_get_running_partition(){return &running;}
const esp_partition_t* esp_ota_get_next_update_partition(void*){return &inactive;}
// esp_ota image writer: what reached the inactive slot, and how it ended.
using esp_ota_handle_t = uint32_t;
constexpr size_t OTA_WITH_SEQUENTIAL_WRITES = 0xfffffffe;
bool image_open=false; std::vector<uint8_t> image; int ota_aborts=0, end_error=0;
const esp_partition_t* boot=nullptr;
esp_err_t esp_ota_begin(const esp_partition_t* p, size_t size, esp_ota_handle_t* handle){
    assert(p==&inactive && size==OTA_WITH_SEQUENTIAL_WRITES && !image_open);
    image_open=true; image.clear(); *handle=7; return ESP_OK;
}
esp_err_t esp_ota_write(esp_ota_handle_t handle, const void* data, size_t size){
    assert(handle==7 && image_open);
    image.insert(image.end(), static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data)+size);
    return ESP_OK;
}
esp_err_t esp_ota_end(esp_ota_handle_t){ assert(image_open); image_open=false; return end_error; }
esp_err_t esp_ota_abort(esp_ota_handle_t){ assert(image_open); image_open=false; ++ota_aborts; return ESP_OK; }
esp_err_t esp_ota_set_boot_partition(const esp_partition_t* p){ boot=p; return ESP_OK; }
// esp_delta_ota: each fed chunk reads as many source bytes, then is written
// to the image unchanged, so the test can see what was fed and read.
struct esp_delta_ota_cfg_t {
    void* user_data;
    esp_err_t (*read_cb_with_user_data)(uint8_t*, size_t, int, void*);
    esp_err_t (*write_cb_with_user_data)(const uint8_t*, size_t, void*);
};
struct Delta { esp_delta_ota_cfg_t config; int source_offset=0; };
using esp_delta_ota_handle_t = Delta*;
enum { DELTA_OK, DELTA_INIT, DELTA_FEED, DELTA_FINALIZE } delta_failure=DELTA_OK;
std::vector<uint8_t> source_read; int delta_open=0;
esp_delta_ota_handle_t esp_delta_ota_init(esp_delta_ota_cfg_t* config){
    if (delta_failure==DELTA_INIT) return nullptr;
    ++delta_open; source_read.clear(); return new Delta{*config};
}
esp_err_t esp_delta_ota_feed_patch(esp_delta_ota_handle_t delta, const uint8_t* data, int size){
    if (delta_failure==DELTA_FEED) return ESP_FAIL;
    std::vector<uint8_t> source(size);
    if (delta->config.read_cb_with_user_data(source.data(), size, delta->source_offset, delta->config.user_data)!=ESP_OK) return ESP_FAIL;
    delta->source_offset+=size;
    source_read.insert(source_read.end(), source.begin(), source.end());
    return delta->config.write_cb_with_user_data(data, size, delta->config.user_data);
}
esp_err_t esp_delta_ota_finalize(esp_delta_ota_handle_t){ return delta_failure==DELTA_FINALIZE ? ESP_FAIL : ESP_OK; }
esp_err_t esp_delta_ota_deinit(esp_delta_ota_handle_t delta){ --delta_open; delete delta; return ESP_OK; }
'''

CASES = r'''
void recovered(OTAHandler& ota, Preferences& prefs) {
    assert(!ota.is_ota_active());
    assert(ota.get_status()==BLE_OTA_ERROR);
    assert(!touch.disabled && !task_manager.suspended);
    assert(last_watchdog.timeout_ms==5000);
    assert(last_watchdog.idle_core_mask==1 && last_watchdog.trigger_panic);
    assert(prefs.values.empty());
    auto token = operation_interlock().try_acquire();
    assert(token); // Recovery must release only after restoring hardware.
    assert(operation_interlock().release(token));
}
int main() {
    // Long enough to carry the image header OtaWriter checks.
    const uint8_t bytes[300]{};
    const uint32_t size = sizeof(bytes);
    Preferences prefs;
    OTAHandler ota;
    ota.init(&prefs);
    auto other_operation = operation_interlock().try_acquire();
    assert(other_operation);
    assert(!ota.start_ota(size,"2",true,"next"));
    assert(prefs.values.empty() && !task_manager.suspended);
    assert(last_watchdog.timeout_ms == 0);
    assert(operation_interlock().owns(other_operation));
    operation_interlock().release(other_operation);
    for (int scenario=0; scenario<6; ++scenario) {
        init_error = scenario==0 ? -1 : 0;
        bool started=ota.start_ota(size,"2",true,"next");
        if (scenario==0) { assert(!started); }
        else {
            assert(started && task_manager.suspended);
            assert(!operation_interlock().try_acquire());
            assert(last_watchdog.timeout_ms==1800000);
            if (scenario==1) ota.abort_ota(); // includes disconnect path
            if (scenario==2) assert(!ota.complete_ota()); // short image
            if (scenario==3) {
                assert(ota.process_data_chunk(bytes,size));
                end_error=-1;
                assert(!ota.complete_ota()); // image validation fails
                end_error=0;
                assert(!image_open && ota_aborts==0 && boot==nullptr && delta_open==0);
            }
            if (scenario==4) assert(!ota.process_data_chunk(bytes,size+1));
            if (scenario==5) {
                write_error=-1;
                assert(!ota.process_data_chunk(bytes,size));
                write_error=0;
            }
        }
        recovered(ota,prefs);
        int resumes=task_manager.resumes;
        ota.abort_ota();
        assert(task_manager.resumes==resumes); // repeated recovery is harmless
    }
    assert(!ota.start_ota(0));
    assert(!ota.start_ota(UINT32_MAX));
    assert(ota.start_ota(8)); ota.abort_ota(); // A tiny patch is accepted; the image it yields is judged at the end.
    const int before=init_calls;
    for(uint32_t size:{8193U,0x7ffff000U,0x7fffffffU,UINT32_MAX})
        assert(!ota.start_ota(size,"2",true,"next"));
    partition_present=false;assert(!ota.start_ota(8));partition_present=true;
    partition.size=8191;assert(!ota.start_ota(4097));partition.size=8192;
    assert(init_calls==before && prefs.values.empty() && !task_manager.suspended);
    assert(ota.start_ota(8192));ota.abort_ota();
    watchdog_error=-1;
    assert(!ota.start_ota(size,"2",true,"next"));
    recovered(ota,prefs);
    watchdog_error=0;
    assert(ota.start_ota(size,"2",true,"next"));
    watchdog_error=-1;
    bool restarted=false;
    try { ota.abort_ota(); } catch (const Restart&) { restarted=true; }
    assert(restarted && task_manager.suspended);
    assert(!operation_interlock().try_acquire()); // Keep locked until reboot/recovery.
    watchdog_error=0;
    ota.abort_ota();
    recovered(ota,prefs);

    // Applying the stored patch: a failure at any step discards the image.
    std::vector<uint8_t> patch(5000);
    for (size_t i=0;i<patch.size();++i) patch[i]=static_cast<uint8_t>(i*7+1);
    for (auto failure : {DELTA_INIT, DELTA_FEED, DELTA_FINALIZE}) {
        delta_failure=failure; ota_aborts=0;
        assert(ota.start_ota(patch.size(),"2",false,""));
        assert(ota.process_data_chunk(patch.data(),patch.size()));
        assert(!ota.complete_ota());
        assert(!image_open && ota_aborts==1 && boot==nullptr && delta_open==0);
        recovered(ota,prefs);
    }
    delta_failure=DELTA_OK;

    // The image a patch produces goes through the same admission check as a
    // browser upload: foreign firmware, or the build the bootloader rolled
    // back from, is discarded and never made bootable.
    firmware_image::verdict="Not Smart Grind firmware"; ota_aborts=0;
    assert(ota.start_ota(patch.size(),"2",false,""));
    assert(ota.process_data_chunk(patch.data(),patch.size()));
    assert(!ota.complete_ota());
    assert(!image_open && ota_aborts==1 && boot==nullptr && delta_open==0);
    recovered(ota,prefs);
    firmware_image::verdict=nullptr;

    // A delta update rebuilds the image from the running one and exactly the
    // stored patch, then boots it; a full update reads an empty source.
    for (bool full : {false, true}) {
        assert(ota.start_ota(patch.size(),"2",full,""));
        for (size_t offset=0; offset<patch.size(); offset+=1000)
            assert(ota.process_data_chunk(patch.data()+offset, std::min<size_t>(1000, patch.size()-offset)));
        bool restarted=false;
        try { ota.complete_ota(); } catch (const Restart&) { restarted=true; }
        assert(restarted && ota.get_status()==BLE_OTA_SUCCESS && boot==&inactive);
        assert(image==patch && !image_open && delta_open==0 && source_read.size()==patch.size());
        for (size_t i=0;i<source_read.size();++i)
            assert(source_read[i]==(full ? 0 : running_byte(i)));
        boot=nullptr;
        ota.abort_ota(); // Recover the fake's state; the device would have restarted.
        recovered(ota,prefs);
    }
}
'''


def without_includes(path):
    return "\n".join(line for line in path.read_text().splitlines()
                     if not line.startswith(("#include", "#pragma once")))


class OtaRecoveryTest(unittest.TestCase):
    def test_production_failure_paths(self):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler, "g++ is required for OTA fault-injection tests")
        source = (STUBS + without_includes(ROOT / "src/network/ota_writer.h")
                  + "\n" + without_includes(ROOT / "src/network/ota_writer.cpp")
                  + "\n" + without_includes(ROOT / "src/bluetooth/ota_handler.h")
                  + "\n" + without_includes(ROOT / "src/bluetooth/ota_handler.cpp")
                  + "\n" + CASES)
        with tempfile.TemporaryDirectory(prefix="smart-grind-ota-test-") as folder:
            cpp = Path(folder) / "test.cpp"
            binary = Path(folder) / "test"
            cpp.write_text(source)
            subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra",
                            "-I", str(ROOT / "src"),
                            str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
