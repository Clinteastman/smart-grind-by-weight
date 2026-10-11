"""Name every ESP-IDF reset reason in the startup log."""
from pathlib import Path
import subprocess
import tempfile
import unittest

from controller_serialization_test import function

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "src/main.cpp").read_text()

# esp_reset_reason_t as ESP-IDF 6.1 declares it, in order.
REASONS = (
    "ESP_RST_UNKNOWN", "ESP_RST_POWERON", "ESP_RST_EXT", "ESP_RST_SW", "ESP_RST_PANIC",
    "ESP_RST_INT_WDT", "ESP_RST_TASK_WDT", "ESP_RST_WDT", "ESP_RST_DEEPSLEEP",
    "ESP_RST_BROWNOUT", "ESP_RST_SDIO", "ESP_RST_USB", "ESP_RST_JTAG", "ESP_RST_EFUSE",
    "ESP_RST_PWR_GLITCH", "ESP_RST_CPU_LOCKUP",
)

HARNESS = r'''
#include <cassert>
#include <cstring>
#include <set>
#include <string>
typedef enum { ''' + ", ".join(REASONS) + r''' } esp_reset_reason_t;
''' + function(SOURCE, "const char* reset_reason_name(esp_reset_reason_t reason)") + r'''

int main() {
    std::set<std::string> names;
    for (int reason = ESP_RST_UNKNOWN; reason <= ESP_RST_CPU_LOCKUP; ++reason) {
        const char* name = reset_reason_name(static_cast<esp_reset_reason_t>(reason));
        // Only an undeterminable reset is reported as UNKNOWN.
        assert((reason == ESP_RST_UNKNOWN) == (std::strcmp(name, "UNKNOWN") == 0));
        names.insert(name);
    }
    assert(names.size() == ''' + str(len(REASONS)) + r''');
    assert(std::strcmp(reset_reason_name(ESP_RST_USB), "USB (Serial/JTAG port)") == 0);
}
'''


class ResetReasonTest(unittest.TestCase):
    def test_every_reset_reason_has_its_own_name(self):
        with tempfile.TemporaryDirectory() as folder:
            cpp, binary = Path(folder) / "reasons.cpp", Path(folder) / "reasons"
            cpp.write_text(HARNESS)
            # -Wswitch rejects a reason the switch does not handle.
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", str(cpp),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
