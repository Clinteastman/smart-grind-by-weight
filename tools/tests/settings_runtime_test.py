"""Checked production runtime reloads preserve prior values on read failure."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from settings_persistence_test import method

ROOT = Path(__file__).resolve().parents[2]


class SettingsRuntimeTest(unittest.TestCase):
    def test_failed_reload(self):
        ui = (ROOT / "src/ui/ui_manager.cpp").read_text()
        timing = (ROOT / "src/system/screensaver_settings.cpp").read_text()
        display = (ROOT / "src/ui/controllers/screen_timeout_controller.cpp").read_text()
        harness = r'''
#include <cassert>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#define LOG_BLE(...) ((void)0)
constexpr float USER_AUTO_GRIND_TRIGGER_DELTA_G=50, USER_AUTO_GRIND_TRIGGER_MIN_G=5, USER_AUTO_GRIND_TRIGGER_MAX_G=1000;
uint32_t millis(){return 123;}
struct Preferences {
 inline static bool open=true;
 inline static std::string bad;
 bool begin(const char*,bool){return open;}
 void end(){}
 uint8_t getUChar(const char* key,uint8_t fallback){
   if(bad==key)return fallback;
   return std::string(key)=="startup_s"?3:1;
 }
 uint16_t getUShort(const char* key,uint16_t fallback){return bad==key?fallback:120;}
 float getFloat(const char* key,float fallback){return bad==key?fallback:75;}
};
struct ScreensaverTimingSettings {uint16_t idle_timeout_s;uint8_t startup_timeout_s;bool display_off_enabled;uint16_t display_off_delay_s;};
namespace ScreensaverSettings {
constexpr auto kPrefsNamespace="screensaver",kIdleTimeoutKey="idle_timeout_s",kStartupTimeoutKey="startup_s",kDisplayOffEnabledKey="panel_off",kDisplayOffDelayKey="panel_delay_s";
bool is_valid_idle_timeout(uint16_t v){return v>=30 && v<=3600;}
bool is_valid_startup_timeout(uint8_t v){return v>=1 && v<=30;}
bool is_valid_display_off_delay(uint16_t v){return v>=30 && v<=43200;}
ScreensaverTimingSettings load_timing(){return {300,3,false,3600};}
''' + method(timing, "bool load_timing_checked(") + r'''
}
struct DisplayManager {};
struct Hardware {DisplayManager display;DisplayManager* get_display(){return &display;}};
struct UIManager {
 Hardware* hardware_manager;
 struct {bool auto_start_enabled=false,auto_return_enabled=false;float auto_start_threshold_g=50;uint32_t last_auto_start_ms=0,last_auto_return_ms=0;} auto_actions_;
 bool refresh_auto_action_settings(bool);
};
''' + method(ui, "bool UIManager::refresh_auto_action_settings(") + r'''
struct ScreenTimeoutController {
 UIManager* ui_manager_;
 ScreensaverTimingSettings timing_settings_{300,3,false,3600};
 uint32_t settings_applied_at_ms_=0,last_weight_activity_ms_=0,last_settings_refresh_ms_=0;
 int restores=0;
 void restore_normal_display(DisplayManager*){++restores;}
 bool apply_runtime_settings(bool);
};
''' + method(display, "bool ScreenTimeoutController::apply_runtime_settings(") + r'''
int main(){
 Hardware hardware;UIManager ui{&hardware};ScreenTimeoutController screen{&ui};
 Preferences::open=false;
 assert(!ui.refresh_auto_action_settings(true));assert(!screen.apply_runtime_settings(true));
 assert(ui.auto_actions_.auto_start_threshold_g==50 && !ui.auto_actions_.auto_start_enabled);
 assert(screen.timing_settings_.idle_timeout_s==300 && screen.restores==0);
 Preferences::open=true;
 for(auto key:{"auto_start","auto_return","start_delta_g"}){
   Preferences::bad=key;assert(!ui.refresh_auto_action_settings(true));
   assert(!ui.auto_actions_.auto_start_enabled && ui.auto_actions_.auto_start_threshold_g==50);
 }
 for(auto key:{"idle_timeout_s","startup_s","panel_off","panel_delay_s"}){
   Preferences::bad=key;assert(!screen.apply_runtime_settings(true));
   assert(screen.timing_settings_.idle_timeout_s==300 && screen.restores==0);
 }
 Preferences::bad="";
 assert(ui.refresh_auto_action_settings(true));assert(screen.apply_runtime_settings(true));
 assert(ui.auto_actions_.auto_start_enabled && ui.auto_actions_.auto_start_threshold_g==75);
 assert(screen.timing_settings_.idle_timeout_s==120 && screen.restores==1);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            cpp, binary = Path(tmp) / "runtime.cpp", Path(tmp) / "runtime"
            cpp.write_text(harness)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-fsanitize=address,undefined", str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=15)
