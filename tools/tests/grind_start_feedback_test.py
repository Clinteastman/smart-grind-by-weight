"""Run the real touchscreen start handler with rejected/accepted commands."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class GrindStartFeedbackTest(unittest.TestCase):
    def test_start_result_is_visible_without_retry(self):
        source = (ROOT / "src/ui/controllers/grinding_controller.cpp").read_text()
        start = source.index("void GrindingUIController::handle_grind_button()")
        end = source.index("void GrindingUIController::handle_pulse_button()", start)
        harness = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#define LOG_BLE(...) ((void)0)
#define THEME_COLOR_WARNING 0
int lv_color_hex(int color) { return color; }
enum class GrindMode { WEIGHT, TIME, MANUAL };
enum class UIState { READY, MENU, GRINDING, GRIND_COMPLETE, GRIND_TIMEOUT, PURGE_CONFIRM };
struct ReadyScreen { enum { MENU_TAB_INDEX=5, WIFI_TAB_INDEX=4, MANUAL_TAB_INDEX=0 }; };
struct State { UIState current=UIState::READY; bool is_state(UIState s) { return current==s; } };
struct Controller {
    bool accept=false; int calls=0, stops=0;
    void set_grind_profile_id(int) {}
    bool start_grind(float, uint32_t, GrindMode) { ++calls; return accept; }
    void stop_grind() { ++stops; }
    void return_to_idle() {}
};
struct Profile {
    int get_current_profile() { return 0; }
    float get_current_weight() { return 18; }
    float get_current_time() { return 5; }
};
struct UIManager {
    State* state_machine;
    Controller* grind_controller;
    Profile* profile_controller;
    int current_tab=1, notices=0;
    GrindMode current_mode=GrindMode::WEIGHT;
    void switch_to_state(UIState s) { state_machine->current=s; }
    void show_confirmation(const char* title, const char*, const char*, int,
                           std::nullptr_t, const char*) {
        assert(std::strcmp(title, "Could not start")==0); ++notices;
    }
};
struct GrindingUIController {
    UIManager* ui_manager_;
    char error_message_[32]{}; float error_grind_weight_=0; int error_grind_progress_=0;
    void handle_grind_button();
};
''' + source[start:end] + r'''
int main() {
    for (bool accepted : {false, true}) {
        for (int tab : {0, 1}) {
            State state; Controller control; Profile profile;
            control.accept=accepted;
            UIManager ui{&state, &control, &profile}; ui.current_tab=tab;
            GrindingUIController handler{&ui}; handler.handle_grind_button();
            assert(control.calls==1 && control.stops==0);
            assert(ui.notices==(accepted ? 0 : 1));
        }
    }
}
'''
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / "feedback.cpp"
            cpp.write_text("#include <initializer_list>\n#include <cstddef>\n" + harness)
            binary = Path(directory) / "feedback"
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", str(cpp),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
