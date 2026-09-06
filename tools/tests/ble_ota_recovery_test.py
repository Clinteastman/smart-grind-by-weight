"""Execute the production OTA handler against host-side fault-injection fakes."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

STUBS = r'''
#include <cstdint>
#include <climits>
#include <string>
#include <map>
#include <cassert>
#include <stdexcept>
#define LOG_BLE(...) ((void)0)
#define LOG_OTA_DEBUG(...) ((void)0)
#define BUILD_NUMBER 1
#define BUILD_FIRMWARE_VERSION "test"
#define BLE_NORMAL_CPU_FREQ_MHZ 240
#define BLE_REDUCED_CPU_FREQ_MHZ 80
#define CONFIG_ESP_TASK_WDT_TIMEOUT_S 5
#define CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0 1
#define CONFIG_ESP_TASK_WDT_PANIC 1
constexpr int ESP_OK = 0;
constexpr int PARTITION_PAGE_SIZE=4096;
constexpr int ESP_PARTITION_TYPE_DATA=1, ESP_PARTITION_SUBTYPE_DATA_SPIFFS=130;
struct String {
    std::string text;
    String(const char* s=""): text(s) {}
    String(int n): text(std::to_string(n)) {}
    bool isEmpty() const { return text.empty(); }
    const char* c_str() const { return text.c_str(); }
    int toInt() const { return std::stoi(text); }
    bool operator!=(const String& other) const { return text != other.text; }
};
struct Preferences {
    std::map<std::string, String> values;
    void putString(const char* k, String v) { values[k]=v; }
    String getString(const char* k, const char* fallback) {
        return values.count(k) ? values[k] : String(fallback);
    }
    void remove(const char* k) { values.erase(k); }
};
struct esp_task_wdt_config_t {
    uint32_t timeout_ms; uint32_t idle_core_mask; bool trigger_panic;
};
esp_task_wdt_config_t last_watchdog{};
int watchdog_error=0, init_error=0, write_error=0, finalize_error=-1;
int esp_task_wdt_reconfigure(const esp_task_wdt_config_t* c) {
    if (watchdog_error) return watchdog_error;
    last_watchdog=*c; return ESP_OK;
}
struct Restart : std::exception {};
void esp_restart() { throw Restart{}; }
struct { void restart() { esp_restart(); } } ESP;
struct { void flush() {} } Serial;
void delay(int) {}
uint32_t getCpuFrequencyMhz() { return 240; }
bool setCpuFrequencyMhz(uint32_t) { return true; }
struct Touch { bool disabled=false; void disable(){disabled=true;} void enable(){disabled=false;} } touch;
struct Display { Touch* get_touch_driver(){return &touch;} } display;
struct HardwareManager { Display* get_display(){return &display;} } hardware_manager;
struct {
    bool suspended=false; int resumes=0;
    void suspend_hardware_tasks(){suspended=true;}
    void resume_hardware_tasks(){suspended=false; ++resumes;}
} task_manager;
struct delta_partition_writer_t {};
struct delta_opts_t { const char* src; const char* dest; const char* patch; int is_full_update; };
struct esp_partition_t { const char* label; uint32_t address; uint32_t size; } partition{"test",0,8192};
bool partition_present=true;
int init_calls=0;
const esp_partition_t* esp_partition_find_first(int,int,const char*){
    return partition_present ? &partition : nullptr;
}
const esp_partition_t* esp_ota_get_running_partition(){return &partition;}
const esp_partition_t* esp_ota_get_next_update_partition(void*){return &partition;}
int delta_partition_init(delta_partition_writer_t*,const char*,int){++init_calls;return init_error;}
int delta_partition_write(delta_partition_writer_t*,const char*,size_t){return write_error;}
int delta_check_and_apply(uint32_t,delta_opts_t*){return finalize_error;}
const char* delta_error_as_string(int){return "injected failure";}
'''

CASES = r'''
void recovered(OTAHandler& ota, Preferences& prefs) {
    assert(!ota.is_ota_active());
    assert(ota.get_status()==BLE_OTA_ERROR);
    assert(!touch.disabled && !task_manager.suspended);
    assert(last_watchdog.timeout_ms==5000);
    assert(last_watchdog.idle_core_mask==1 && last_watchdog.trigger_panic);
    assert(prefs.values.empty());
}
int main() {
    const uint8_t bytes[8]{};
    Preferences prefs;
    OTAHandler ota;
    ota.init(&prefs);
    for (int scenario=0; scenario<6; ++scenario) {
        init_error = scenario==0 ? -1 : 0;
        bool started=ota.start_ota(8,"2",true,"next");
        if (scenario==0) { assert(!started); }
        else {
            assert(started && task_manager.suspended);
            assert(last_watchdog.timeout_ms==1800000);
            if (scenario==1) ota.abort_ota(); // includes disconnect path
            if (scenario==2) assert(!ota.complete_ota()); // short image
            if (scenario==3) {
                assert(ota.process_data_chunk(bytes,8));
                assert(!ota.complete_ota()); // image validation fails
            }
            if (scenario==4) assert(!ota.process_data_chunk(bytes,9));
            if (scenario==5) {
                write_error=-1;
                assert(!ota.process_data_chunk(bytes,8));
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
    const int before=init_calls;
    for(uint32_t size:{8193U,0x7ffff000U,0x7fffffffU,UINT32_MAX})
        assert(!ota.start_ota(size,"2",true,"next"));
    partition_present=false;assert(!ota.start_ota(8));partition_present=true;
    partition.size=8191;assert(!ota.start_ota(4097));partition.size=8192;
    assert(init_calls==before && prefs.values.empty() && !task_manager.suspended);
    assert(ota.start_ota(8192));ota.abort_ota();
    watchdog_error=-1;
    assert(!ota.start_ota(8,"2",true,"next"));
    recovered(ota,prefs);
    watchdog_error=0;
    assert(ota.start_ota(8,"2",true,"next"));
    watchdog_error=-1;
    bool restarted=false;
    try { ota.abort_ota(); } catch (const Restart&) { restarted=true; }
    assert(restarted && task_manager.suspended);
    watchdog_error=0;
    ota.abort_ota();
    recovered(ota,prefs);
}
'''


def without_includes(path):
    return "\n".join(line for line in path.read_text().splitlines()
                     if not line.startswith(("#include", "#pragma once")))


class OtaRecoveryTest(unittest.TestCase):
    def test_production_failure_paths(self):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler, "g++ is required for OTA fault-injection tests")
        source = (STUBS + without_includes(ROOT / "src/bluetooth/ota_handler.h")
                  + "\n" + without_includes(ROOT / "src/bluetooth/ota_handler.cpp")
                  + "\n" + CASES)
        with tempfile.TemporaryDirectory(prefix="smart-grind-ota-test-") as folder:
            cpp = Path(folder) / "test.cpp"
            binary = Path(folder) / "test"
            cpp.write_text(source)
            subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra",
                            str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
