"""Run the production motor-test entry/exit paths with timer fault injection."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from autotune_cancel_test import function

ROOT = Path(__file__).resolve().parents[2]


class MotorTestInterlockTest(unittest.TestCase):
    def test_timer_and_ownership(self):
        source = (ROOT / "src/ui/controllers/menu_controller.cpp").read_text()
        methods = "\n".join(function(source, signature) for signature in (
            "void MenuUIController::run_motor_test()",
            "void MenuUIController::stop_motor_timer()",
            "void MenuUIController::motor_timer_cb(",
            "void MenuUIController::static_motor_timer_cb(",
        ))
        harness = r'''
#include "system/operation_interlock.h"
#include <cassert>
struct lv_timer_t { void* user_data; } timer_storage;
bool timer_failure = false;
unsigned timers_deleted = 0;
lv_timer_t* lv_timer_create(void (*)(lv_timer_t*), unsigned, void* data) {
    if (timer_failure) return nullptr;
    timer_storage.user_data = data;
    return &timer_storage;
}
void lv_timer_del(lv_timer_t*) { ++timers_deleted; }
void* lv_timer_get_user_data(lv_timer_t* timer) { return timer->user_data; }
struct Grinder {
    unsigned pulses = 0, stops = 0;
    void start_pulse_rmt(unsigned ms) {
        assert(ms == 1000 && !operation_interlock().try_acquire()); ++pulses;
    }
    void stop() { assert(!operation_interlock().try_acquire()); ++stops; }
} grinder;
struct Hardware { Grinder* get_grinder() { return &grinder; } } hardware;
struct UIManager {
    bool active = false;
    Hardware* get_hardware_manager() { return &hardware; }
    void set_background_active(bool value) { active = value; }
} ui;
struct { unsigned tests = 0; void update_motor_test(unsigned) { ++tests; } } statistics_manager;
class MenuUIController {
public:
    UIManager* ui_manager_ = &ui;
    lv_timer_t* motor_timer_ = nullptr;
    OperationInterlock::Token motor_test_token_ = 0;
    void run_motor_test();
    void stop_motor_timer();
    void motor_timer_cb(lv_timer_t*);
    static void static_motor_timer_cb(lv_timer_t*);
    void return_to_menu() {}
};
''' + methods + r'''
int main() {
    MenuUIController menu;
    auto competing = operation_interlock().try_acquire();
    menu.run_motor_test();
    assert(grinder.pulses == 0 && !ui.active && menu.motor_timer_ == nullptr);
    assert(operation_interlock().owns(competing));
    operation_interlock().release(competing);
    timer_failure = true;
    menu.run_motor_test();
    assert(grinder.pulses == 0 && statistics_manager.tests == 0 && !ui.active);
    auto available = operation_interlock().try_acquire();
    assert(available); operation_interlock().release(available);
    timer_failure = false;
    menu.run_motor_test();
    assert(grinder.pulses == 1 && ui.active && menu.motor_timer_);
    assert(!operation_interlock().try_acquire());
    menu.run_motor_test();
    assert(grinder.pulses == 1); // Double click does not replace the timer.
    lv_timer_t unrelated{};
    menu.motor_timer_cb(&unrelated);
    assert(grinder.stops == 0 && !operation_interlock().try_acquire());
    MenuUIController::static_motor_timer_cb(menu.motor_timer_);
    assert(grinder.stops == 1 && !ui.active && !menu.motor_timer_);
    assert(timers_deleted == 1);
    auto later = operation_interlock().try_acquire();
    assert(later);
    menu.motor_timer_cb(&timer_storage);
    assert(grinder.stops == 1 && operation_interlock().owns(later));
    operation_interlock().release(later);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            cpp, binary = Path(tmp) / "motor.cpp", Path(tmp) / "motor"
            cpp.write_text(harness)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra",
                            "-I", str(ROOT / "src"), str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=20)


if __name__ == "__main__":
    unittest.main()
