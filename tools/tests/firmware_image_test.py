"""Run the production firmware image checks against fake image headers."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from controller_serialization_test import function

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "src/network/firmware_image.cpp").read_text()
PROJECT_NAMES = re.search(r"^constexpr const char\* kProjectNames\[\] = [^;]+;$", SOURCE, re.M).group(0)

HARNESS = r'''
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_INVALID_VERSION = 0x10A,
                    ESP_ERR_OTA_SPI_MODE_MISMATCH = 0x150A;
constexpr uint8_t ESP_IMAGE_HEADER_MAGIC = 0xE9;
constexpr uint32_t ESP_APP_DESC_MAGIC_WORD = 0xABCD5432;
enum esp_partition_type_t { ESP_PARTITION_TYPE_APP = 0 };
// The layouts ESP-IDF 6.1 uses, down to the sizes the checks depend on.
struct esp_image_header_t { uint8_t magic; uint8_t rest[23]; };
struct esp_image_segment_header_t { uint32_t load_addr; uint32_t data_len; };
struct esp_app_desc_t {
    uint32_t magic_word, secure_version, reserv1[2];
    char version[32], project_name[32], time[16], date[16], idf_ver[32];
    uint8_t app_elf_sha256[32];
    uint8_t rest[256 - 4 * 4 - 32 * 2 - 16 * 2 - 32 - 32];
};
static_assert(sizeof(esp_image_header_t) == 24 && sizeof(esp_image_segment_header_t) == 8 &&
              sizeof(esp_app_desc_t) == 256, "fake layouts must match ESP-IDF");
struct esp_partition_t {} slot;
const esp_partition_t* invalid_partition = nullptr;
esp_app_desc_t invalid_description{};
const esp_partition_t* esp_ota_get_last_invalid_partition() { return invalid_partition; }
esp_err_t esp_ota_get_partition_description(const esp_partition_t* partition, esp_app_desc_t* out) {
    assert(partition == &slot);
    *out = invalid_description;
    return ESP_OK;
}
esp_err_t validity = ESP_OK;
esp_app_desc_t validated{};
esp_err_t esp_ota_check_image_validity(esp_partition_type_t, const esp_image_header_t*,
                                       const esp_app_desc_t* description) {
    assert(description);
    validated = *description;
    return validity;
}
namespace firmware_image {
constexpr size_t HEADER_BYTES =
    sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t);
''' + PROJECT_NAMES + "\n" + "\n".join(function(SOURCE, signature) for signature in (
    "bool is_smart_grind(const esp_app_desc_t& description)",
    "bool same_build(const esp_app_desc_t& first, const esp_app_desc_t& second)",
    "const char* check_description(const esp_app_desc_t& description)",
    "const char* check_header(const uint8_t* header)",
)) + r'''
}

esp_app_desc_t description(const char* project, uint8_t build) {
    esp_app_desc_t app{};
    app.magic_word = ESP_APP_DESC_MAGIC_WORD;
    strncpy(app.project_name, project, sizeof(app.project_name));
    memset(app.app_elf_sha256, build, sizeof(app.app_elf_sha256));
    return app;
}

std::vector<uint8_t> header(uint8_t magic, const esp_app_desc_t& app) {
    std::vector<uint8_t> bytes(firmware_image::HEADER_BYTES, 0);
    bytes[0] = magic;
    memcpy(bytes.data() + 24 + 8, &app, sizeof(app));
    return bytes;
}

std::string verdict(const char* reason) { return reason ? reason : "accepted"; }

int main() {
    using firmware_image::check_description;
    using firmware_image::check_header;
    // This firmware's images and the Arduino-built 1.5.x releases are accepted.
    assert(verdict(check_description(description("smart-grind-by-weight", 1))) == "accepted");
    assert(verdict(check_description(description("arduino-lib-builder", 2))) == "accepted");
    assert(verdict(check_description(description("hello_world", 3))) == "Not Smart Grind firmware");
    esp_app_desc_t corrupt = description("smart-grind-by-weight", 4);
    corrupt.magic_word = 0;
    assert(verdict(check_description(corrupt)) == "Not a valid ESP32 firmware image");

    // The build the bootloader rolled back from is refused; any other build,
    // including one with the same version string, is not.
    invalid_partition = &slot;
    invalid_description = description("smart-grind-by-weight", 5);
    assert(verdict(check_description(description("smart-grind-by-weight", 5))) ==
           "This firmware failed to start and was rolled back");
    assert(verdict(check_description(description("smart-grind-by-weight", 6))) == "accepted");
    invalid_partition = nullptr;

    // The header check reads the description after the image and segment
    // headers, and maps the chip and flash-mode verdicts.
    const esp_app_desc_t app = description("smart-grind-by-weight", 7);
    const std::vector<uint8_t> good = header(0xE9, app);
    assert(verdict(check_header(good.data())) == "accepted");
    assert(memcmp(validated.project_name, app.project_name, 32) == 0);
    assert(verdict(check_header(header(0x00, app).data())) == "Not a valid ESP32 firmware image");
    validity = ESP_ERR_INVALID_VERSION;
    assert(verdict(check_header(good.data())) ==
           "Firmware is built for a different chip or chip revision");
    validity = ESP_ERR_OTA_SPI_MODE_MISMATCH;
    assert(verdict(check_header(good.data())) == "Firmware is built for a different flash mode");
    validity = ESP_OK;
    assert(verdict(check_header(header(0xE9, description("hello_world", 8)).data())) ==
           "Not Smart Grind firmware");
}
'''


class FirmwareImageTest(unittest.TestCase):
    def test_only_installable_smart_grind_images_are_accepted(self):
        with tempfile.TemporaryDirectory() as folder:
            cpp, binary = Path(folder) / "image.cpp", Path(folder) / "image"
            cpp.write_text(HARNESS)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", str(cpp),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
