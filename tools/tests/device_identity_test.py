"""Keep the device identity, heap figures and internal RAM the Arduino firmware had."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class DeviceIdentityTest(unittest.TestCase):
    def test_identity_and_heap_match_arduino(self):
        source = (ROOT / "src/system/device_info.cpp").read_text()
        heap = source[source.index("size_t free_heap_bytes()"):
                      source.index("size_t free_internal_heap_bytes()")]
        mac = source[source.index("uint64_t efuse_mac()"):
                     source.index("uint32_t cpu_frequency_mhz()")]
        code = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#define ESP_OK 0
#define ESP_FAIL -1
#define MALLOC_CAP_8BIT (1 << 2)
#define MALLOC_CAP_SPIRAM (1 << 10)
#define MALLOC_CAP_INTERNAL (1 << 11)
#define MALLOC_CAP_DEFAULT (1 << 12)
using esp_err_t = int;
// MAC of a V2 board whose 1.5.9 firmware broadcast "SmartGrind-858428".
const uint8_t board_mac[6] = {0x28, 0x84, 0x85, 0x8d, 0x4b, 0x84};
esp_err_t mac_result = ESP_OK;
esp_err_t esp_efuse_mac_get_default(uint8_t* out) {
    if (mac_result == ESP_OK) memcpy(out, board_mac, sizeof(board_mac));
    return mac_result;
}
// Internal RAM and the PSRAM-inclusive default heap differ by megabytes, so a
// helper that counts PSRAM cannot pass for the internal figure.
size_t heap_caps_get_free_size(uint32_t caps) {
    return caps == MALLOC_CAP_INTERNAL ? 60U * 1024U : 7U * 1024U * 1024U;
}
size_t heap_caps_get_total_size(uint32_t caps) {
    return caps == MALLOC_CAP_INTERNAL ? 320U * 1024U : 8U * 1024U * 1024U;
}
size_t esp_get_free_heap_size() { return 7U * 1024U * 1024U; }
namespace device_info {
''' + heap + mac + r'''
}  // namespace device_info

int main() {
    assert(device_info::efuse_mac() == 0x844b8d858428ULL);

    char text[32];
    snprintf(text, sizeof(text), "SmartGrind-%06lx",
             static_cast<unsigned long>(device_info::efuse_mac() & 0xFFFFFFULL));
    assert(strcmp(text, "SmartGrind-858428") == 0);
    snprintf(text, sizeof(text), "%012llx",
             static_cast<unsigned long long>(device_info::efuse_mac()));
    assert(strcmp(text, "844b8d858428") == 0);

    mac_result = ESP_FAIL;
    assert(device_info::efuse_mac() == 0);

    assert(device_info::free_heap_bytes() == 60U * 1024U);
    assert(device_info::total_heap_bytes() == 320U * 1024U);
}
'''
        with tempfile.TemporaryDirectory() as folder:
            cpp = Path(folder) / "device_identity.cpp"
            binary = Path(folder) / "device_identity"
            cpp.write_text(code)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra",
                            "-fsanitize=address,undefined", str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)

    def test_bluetooth_address_matches_arduino(self):
        # The Arduino core reserved two universal MAC addresses, which puts the
        # Bluetooth address at base + 1; four would move it to base + 2.
        settings = (ROOT / "sdkconfig.defaults").read_text().splitlines()
        self.assertIn("CONFIG_ESP32S3_UNIVERSAL_MAC_ADDRESSES_TWO=y", settings)

    def test_wifi_code_leaves_internal_ram_to_the_heap(self):
        # The Arduino core kept the Wi-Fi hot paths in flash. Moving them to
        # IRAM costs the internal heap that browser updates are admitted on.
        settings = (ROOT / "sdkconfig.defaults").read_text().splitlines()
        self.assertIn("CONFIG_ESP_WIFI_IRAM_OPT=n", settings)
        self.assertIn("CONFIG_ESP_WIFI_RX_IRAM_OPT=n", settings)


if __name__ == "__main__":
    unittest.main()
