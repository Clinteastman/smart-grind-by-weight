import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class DiagnosticsNoiseTest(unittest.TestCase):
    def test_weight_sensor_noise_diagnostic_reset_and_threshold(self):
        sensor_h = (ROOT / "src/hardware/WeightSensor.h").read_text(encoding="utf-8")
        self.assertIn("void reset_noise_diagnostic();", sensor_h)

        sensor_cpp = (ROOT / "src/hardware/WeightSensor.cpp").read_text(encoding="utf-8")
        self.assertIn("void WeightSensor::reset_noise_diagnostic()", sensor_cpp)
        self.assertIn("now - not_settled_start_time >= 3000", sensor_cpp)

        menu_ctrl = (ROOT / "src/ui/controllers/menu_controller.cpp").read_text(encoding="utf-8")
        self.assertIn("sensor->reset_noise_diagnostic();", menu_ctrl)
        self.assertIn("ui_manager_->menu_screen.is_diagnostics_page_active()", menu_ctrl)

        menu_screen_h = (ROOT / "src/ui/screens/menu_screen.h").read_text(encoding="utf-8")
        self.assertIn("bool is_diagnostics_page_active() const { return diagnostics_active; }", menu_screen_h)

        menu_screen = (ROOT / "src/ui/screens/menu_screen.cpp").read_text(encoding="utf-8")
        # Ensure the 1000ms gate inside update_diagnostics has been removed
        update_diag_body = menu_screen.split("void MenuScreen::update_diagnostics(WeightSensor* weight_sensor)", 1)[1].split("\nvoid ", 1)[0]
        self.assertNotIn("last_std_dev_update >= 1000", update_diag_body)
        self.assertIn("!diagnostics_active", update_diag_body)
        self.assertIn("self->diagnostics_active = true;", menu_screen)

    def test_noise_diagnostic_behavior_simulation(self):
        cpp_code = r"""
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cmath>

static uint32_t fake_millis = 0;
uint32_t millis() { return fake_millis; }

struct FakeWeightSensor {
    mutable unsigned long not_settled_start_time = 0;
    mutable bool currently_not_settled = false;
    mutable bool display_noisy_state = false;
    float mock_std_dev = 0.002f;

    float get_standard_deviation_g(uint32_t) const { return mock_std_dev; }

    void reset_noise_diagnostic() {
        not_settled_start_time = 0;
        currently_not_settled = false;
        display_noisy_state = false;
    }

    bool noise_level_diagnostic() const {
        float std_dev_g = get_standard_deviation_g(500);
        bool currently_settled = std_dev_g < 0.010f;
        unsigned long now = millis();

        if (!currently_settled) {
            if (!currently_not_settled) {
                not_settled_start_time = now;
                currently_not_settled = true;
            }
            if (now - not_settled_start_time >= 3000) {
                display_noisy_state = true;
            }
        } else {
            currently_not_settled = false;
            display_noisy_state = false;
        }
        return !display_noisy_state;
    }
};

int main() {
    FakeWeightSensor sensor;

    // Initially settled
    assert(sensor.noise_level_diagnostic() == true);

    // Vibration starts (e.g. touch) at t=100
    fake_millis = 100;
    sensor.mock_std_dev = 0.025f; // Unsettled
    assert(sensor.noise_level_diagnostic() == true); // Within grace period

    // At t=2500 ms (2.4s elapsed), still within 3s grace period
    fake_millis = 2500;
    assert(sensor.noise_level_diagnostic() == true);

    // At t=3200 ms (>3.0s elapsed), noisy state trips
    fake_millis = 3200;
    assert(sensor.noise_level_diagnostic() == false);

    // Reset clears noisy state immediately
    sensor.reset_noise_diagnostic();
    assert(sensor.display_noisy_state == false);
    assert(sensor.not_settled_start_time == 0);

    // Once settled, immediate recovery
    sensor.mock_std_dev = 0.003f;
    fake_millis = 3500;
    assert(sensor.noise_level_diagnostic() == true);

    printf("ALL TESTS PASSED\n");
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as td:
            src = Path(td) / "test.cpp"
            out = Path(td) / "test"
            src.write_text(cpp_code, encoding="utf-8")
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", str(src), "-o", str(out)], check=True)
            res = subprocess.run([str(out)], capture_output=True, text=True, check=True)
            self.assertIn("ALL TESTS PASSED", res.stdout)


if __name__ == "__main__":
    unittest.main()
