"""Keep an updated firmware from being rolled back at the next reset."""
from pathlib import Path
import subprocess
import tempfile
import unittest

from controller_serialization_test import function

ROOT = Path(__file__).resolve().parents[2]
MAIN = (ROOT / "src/main.cpp").read_text()


class FirmwareRollbackTest(unittest.TestCase):
    def test_rollback_matches_the_arduino_bootloader(self):
        settings = (ROOT / "sdkconfig.defaults").read_text().splitlines()
        self.assertIn("CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y", settings)

    def test_confirmation_runs_once_start_up_completes(self):
        # An image that crashes or hangs before its tasks are running stays
        # unconfirmed, so the bootloader returns to the previous one.
        self.assertNotIn("confirm_running_image();", function(MAIN, "void init_platform_services()"))
        setup = function(MAIN, "void setup()")
        self.assertEqual(setup.count("confirm_running_image();"), 1)
        self.assertLess(setup.index("task_manager.init("), setup.index("confirm_running_image();"))
        self.assertTrue(setup.rstrip()[:-1].rstrip().endswith("confirm_running_image();"))

    def test_only_an_unconfirmed_update_is_confirmed(self):
        code = r'''
#include <cassert>
#include <cstdio>
#include <initializer_list>
using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0, ESP_ERR_NOT_SUPPORTED = 0x106;
enum esp_ota_img_states_t { ESP_OTA_IMG_NEW, ESP_OTA_IMG_PENDING_VERIFY, ESP_OTA_IMG_VALID,
                            ESP_OTA_IMG_INVALID, ESP_OTA_IMG_ABORTED, ESP_OTA_IMG_UNDEFINED };
struct esp_partition_t {} running;
const esp_partition_t* esp_ota_get_running_partition() { return &running; }
esp_err_t state_result = ESP_OK;
esp_ota_img_states_t running_state = ESP_OTA_IMG_UNDEFINED;
esp_err_t esp_ota_get_state_partition(const esp_partition_t* partition, esp_ota_img_states_t* out) {
    assert(partition == &running);
    if (state_result == ESP_OK) *out = running_state;
    return state_result;
}
int confirmations = 0;
esp_err_t esp_ota_mark_app_valid_cancel_rollback() { ++confirmations; return ESP_OK; }
const char* esp_err_to_name(esp_err_t) { return "ESP_OK"; }
#define LOG_BLE(...) std::printf(__VA_ARGS__)
''' + function(MAIN, "void confirm_running_image()") + r'''
int main() {
    for (auto state : {ESP_OTA_IMG_VALID, ESP_OTA_IMG_UNDEFINED, ESP_OTA_IMG_NEW}) {
        running_state = state; confirm_running_image();
    }
    state_result = ESP_ERR_NOT_SUPPORTED; confirm_running_image();  // factory image
    assert(confirmations == 0);
    state_result = ESP_OK; running_state = ESP_OTA_IMG_PENDING_VERIFY; confirm_running_image();
    assert(confirmations == 1);
}
'''
        with tempfile.TemporaryDirectory() as folder:
            cpp, binary = Path(folder) / "rollback.cpp", Path(folder) / "rollback"
            cpp.write_text(code)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", str(cpp),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
