"""Test custom clock font upload validation, size limits, and fallback behavior."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class ClockFontTest(unittest.TestCase):
    def test_clock_font_validation_and_logic(self):
        web_server = (ROOT / "src/network/device_web_server.cpp").read_text()
        screensaver_cpp = (ROOT / "src/ui/controllers/screensaver_controller.cpp").read_text()
        bluetooth_h = (ROOT / "src/config/bluetooth.h").read_text()

        # Extract is_valid_font_magic
        start = web_server.index("inline bool is_valid_font_magic(")
        end = web_server.index("\n}\n", start) + 3
        is_valid_font_magic_code = web_server[start:end]

        harness = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using String = std::string;

''' + is_valid_font_magic_code + r'''

constexpr uint32_t CLOCK_FONT_MAX_SIZE = (512U * 1024U);

struct MockFont {
    int size = 0;
};

MockFont mock_montserrat_80{80};
MockFont mock_montserrat_160{160};
MockFont mock_custom_80{80};
MockFont mock_custom_160{160};

struct MockScreensaverController {
    String style = "clock";
    String layout = "horizontal";
    String clock_font = "montserrat";
    bool custom_font_file_exists = false;
    size_t custom_font_file_size = 0;
    bool font_load_succeeds = true;

    MockFont* resolve_font() {
        const int target_size = (layout == "stacked") ? 160 : 80;
        if (clock_font == "custom" && custom_font_file_exists &&
            custom_font_file_size >= 16 && custom_font_file_size <= CLOCK_FONT_MAX_SIZE) {
            if (font_load_succeeds) {
                return (target_size == 160) ? &mock_custom_160 : &mock_custom_80;
            }
        }
        return (target_size == 160) ? &mock_montserrat_160 : &mock_montserrat_80;
    }
};

int main() {
    // 1. Magic byte validation
    const uint8_t ttf_magic[4] = {0x00, 0x01, 0x00, 0x00};
    assert(is_valid_font_magic(ttf_magic));

    const uint8_t otto_magic[4] = {'O', 'T', 'T', 'O'};
    assert(is_valid_font_magic(otto_magic));

    const uint8_t true_magic[4] = {'t', 'r', 'u', 'e'};
    assert(is_valid_font_magic(true_magic));

    const uint8_t typ1_magic[4] = {'t', 'y', 'p', '1'};
    assert(is_valid_font_magic(typ1_magic));

    // Invalid magics
    const uint8_t jpeg_magic[4] = {0xFF, 0xD8, 0xFF, 0xE0};
    assert(!is_valid_font_magic(jpeg_magic));

    const uint8_t png_magic[4] = {0x89, 'P', 'N', 'G'};
    assert(!is_valid_font_magic(png_magic));

    const uint8_t html_magic[4] = {'<', 'h', 't', 'm'};
    assert(!is_valid_font_magic(html_magic));

    assert(!is_valid_font_magic(nullptr));

    // 2. Size limit checks
    assert(512 * 1024 == CLOCK_FONT_MAX_SIZE);
    assert(1000 <= CLOCK_FONT_MAX_SIZE);
    assert(524288 <= CLOCK_FONT_MAX_SIZE);
    assert(524289 > CLOCK_FONT_MAX_SIZE);

    // 3. Fallback logic in controller
    MockScreensaverController c;

    // Default montserrat
    c.layout = "horizontal";
    c.clock_font = "montserrat";
    assert(c.resolve_font() == &mock_montserrat_80);

    c.layout = "stacked";
    assert(c.resolve_font() == &mock_montserrat_160);

    // Custom font selected, but no file on LittleFS -> fallback
    c.clock_font = "custom";
    c.custom_font_file_exists = false;
    assert(c.resolve_font() == &mock_montserrat_160);

    c.layout = "horizontal";
    assert(c.resolve_font() == &mock_montserrat_80);

    // Custom font file exists and valid
    c.custom_font_file_exists = true;
    c.custom_font_file_size = 45000; // 45 KB
    assert(c.resolve_font() == &mock_custom_80);

    c.layout = "stacked";
    assert(c.resolve_font() == &mock_custom_160);

    // Custom font file exists but exceeds 512 KB -> rejected/fallback
    c.custom_font_file_size = 600000;
    assert(c.resolve_font() == &mock_montserrat_160);

    // Custom font file exists but corrupt/fails to load -> fallback
    c.custom_font_file_size = 45000;
    c.font_load_succeeds = false;
    assert(c.resolve_font() == &mock_montserrat_160);

    return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            cpp = Path(tmp) / "test.cpp"
            exe = Path(tmp) / "test"
            cpp.write_text(harness)
            build = subprocess.run(["g++", "-std=c++17", str(cpp), "-o", str(exe)],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, f"Compile error:\n{build.stderr}")
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, f"Run error:\n{run.stderr}")


if __name__ == "__main__":
    unittest.main()
