"""Production timing sliders must show retained values after a failed save."""
from pathlib import Path
import subprocess
import tempfile
import unittest

from settings_persistence_test import method

ROOT = Path(__file__).resolve().parents[2]


class SettingsSliderTest(unittest.TestCase):
    def test_retained_value(self):
        source = (ROOT / "src/ui/controllers/menu_controller.cpp").read_text()
        methods = "\n".join(method(source, name) for name in (
            "void MenuUIController::handle_coast_ratio_slider_released()",
            "void MenuUIController::handle_motor_latency_slider_released()"))
        harness = r'''
#include <cassert>
#include <algorithm>
#define LOG_BLE(...) ((void)0)
#define LOG_DEBUG_PRINT(...) ((void)0)
#define LOG_DEBUG_PRINTLN(...) ((void)0)
#define LOG_DEBUG_PRINTF(...) ((void)0)
constexpr int LV_ANIM_OFF=0;
constexpr float GRIND_LATENCY_TO_COAST_RATIO_MIN=.7f, GRIND_LATENCY_TO_COAST_RATIO_MAX=1.5f;
constexpr float GRIND_AUTOTUNE_LATENCY_MIN_MS=30, GRIND_AUTOTUNE_LATENCY_MAX_MS=300;
struct Slider {int value;};
int lv_slider_get_value(Slider* s){return s->value;}
void lv_slider_set_value(Slider* s,int v,int){s->value=v;}
struct MenuScreen {
 static constexpr float kCoastRatioSliderScale=100;
 static constexpr int kMotorLatencySliderStepMs=5;
 Slider coast{140},latency{120}; float coast_label=0,latency_label=0;
 Slider* get_coast_ratio_slider(){return &coast;}
 Slider* get_motor_latency_slider(){return &latency;}
 void update_coast_ratio_label(float v){coast_label=v;}
 void update_motor_latency_label(float v){latency_label=v;}
};
struct Controller {
 bool persist=false; float coast=1,latency=75;
 bool save_coast_ratio(float v){if(!persist)return false;coast=v;return true;}
 bool save_motor_latency(float v){if(!persist)return false;latency=v;return true;}
 float get_coast_ratio(){return coast;}
 float get_motor_response_latency(){return latency;}
};
struct UI {MenuScreen menu_screen;Controller control;Controller* get_grind_controller(){return &control;}};
struct MenuUIController {
 UI* ui_manager_;
 void handle_coast_ratio_slider_released();
 void handle_motor_latency_slider_released();
};
''' + methods + r'''
int main(){
 UI ui;MenuUIController menu{&ui};
 menu.handle_coast_ratio_slider_released();menu.handle_motor_latency_slider_released();
 assert(ui.menu_screen.coast.value==100 && ui.menu_screen.coast_label==1);
 assert(ui.menu_screen.latency.value==75 && ui.menu_screen.latency_label==75);
 ui.control.persist=true;ui.menu_screen.coast.value=140;ui.menu_screen.latency.value=120;
 menu.handle_coast_ratio_slider_released();menu.handle_motor_latency_slider_released();
 assert(ui.menu_screen.coast_label==ui.control.coast && ui.control.coast==1.4f);
 assert(ui.menu_screen.latency_label==120 && ui.control.latency==120);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            cpp, binary = Path(tmp) / "sliders.cpp", Path(tmp) / "sliders"
            cpp.write_text(harness)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)
