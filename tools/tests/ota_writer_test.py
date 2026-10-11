"""Run the production OTA image writer against esp_ota fakes."""
from pathlib import Path
import subprocess
import tempfile
import unittest

from controller_serialization_test import function

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "src/network/ota_writer.cpp").read_text()
HEADER = (ROOT / "src/network/ota_writer.h").read_text()

HARNESS = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>
using esp_err_t = int;
using esp_ota_handle_t = uint32_t;
constexpr esp_err_t ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_OTA_VALIDATE_FAILED = 0x1503;
#define OTA_SIZE_UNKNOWN 0xffffffff
#define OTA_WITH_SEQUENTIAL_WRITES 0xfffffffe
#define LOG_BLE(...) std::printf(__VA_ARGS__)
struct esp_partition_t { size_t size; const char* label; } inactive{0x300000, "ota_1"};
const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t*) { return &inactive; }
size_t begun_with = 0;
unsigned aborts = 0;
std::vector<uint8_t> flash;
esp_err_t write_result = ESP_OK;
esp_err_t esp_ota_begin(const esp_partition_t* partition, size_t image_size, esp_ota_handle_t* out) {
    assert(partition == &inactive);
    begun_with = image_size; flash.clear(); *out = 1; return ESP_OK;
}
esp_err_t esp_ota_write(esp_ota_handle_t, const void* data, size_t length) {
    if (write_result != ESP_OK) return write_result;
    const auto* bytes = static_cast<const uint8_t*>(data);
    flash.insert(flash.end(), bytes, bytes + length);
    return ESP_OK;
}
esp_err_t esp_ota_end(esp_ota_handle_t) { return ESP_OK; }
esp_err_t esp_ota_set_boot_partition(const esp_partition_t*) { return ESP_OK; }
esp_err_t esp_ota_abort(esp_ota_handle_t) { ++aborts; return ESP_OK; }
const char* esp_err_to_name(esp_err_t) { return "ESP_FAIL"; }

// The header check sees exactly the image's first HEADER_BYTES.
namespace firmware_image {
constexpr size_t HEADER_BYTES = 288;
std::vector<uint8_t> checked;
const char* verdict = nullptr;
const char* check_header(const uint8_t* header) {
    checked.assign(header, header + HEADER_BYTES);
    return verdict;
}
}
''' + function(HEADER, "class OtaWriter {") + ";\n" + "\n".join(function(SOURCE, signature) for signature in (
    "bool OtaWriter::fail(",
    "bool OtaWriter::refuse(",
    "bool OtaWriter::begin(",
    "bool OtaWriter::write(",
    "bool OtaWriter::write_to_flash(",
    "bool OtaWriter::end(",
    "void OtaWriter::abort(",
)) + r'''

std::vector<uint8_t> image(size_t size) {
    std::vector<uint8_t> bytes(size);
    for (size_t i = 0; i < size; ++i) bytes[i] = static_cast<uint8_t>(i * 7 + 3);
    return bytes;
}

// Feed an image in chunks of `chunk` bytes; false once the writer refuses one.
bool feed(OtaWriter& writer, const std::vector<uint8_t>& bytes, size_t chunk) {
    for (size_t offset = 0; offset < bytes.size(); offset += chunk) {
        const size_t length = std::min(chunk, bytes.size() - offset);
        if (!writer.write(bytes.data() + offset, length)) return false;
    }
    return true;
}

int main() {
    // A browser upload runs on the HTTP server's only task; erasing the
    // image's extent before the first write stalls every request for seconds.
    for (size_t size : {size_t{0}, size_t{2455776}}) {
        OtaWriter writer; begun_with = 0;
        assert(writer.begin(size) && begun_with == OTA_WITH_SEQUENTIAL_WRITES);
        writer.abort();
    }
    OtaWriter oversized;
    assert(!oversized.begin(inactive.size + 1));

    // Nothing reaches flash until the header has been checked, chunks that
    // straddle its end are split correctly, and the image arrives intact.
    const std::vector<uint8_t> firmware = image(5000);
    for (size_t chunk : {size_t{61}, size_t{288}, size_t{4096}}) {
        OtaWriter writer; firmware_image::verdict = nullptr;
        assert(writer.begin(0));
        assert(writer.write(firmware.data(), 100) && flash.empty());
        assert(feed(writer, std::vector<uint8_t>(firmware.begin() + 100, firmware.end()), chunk));
        assert(writer.end() && flash == firmware);
        assert((firmware_image::checked == std::vector<uint8_t>(firmware.begin(), firmware.begin() + 288)));
    }

    // A refused header never reaches flash and is reported as a refusal.
    OtaWriter refused; firmware_image::verdict = "Not Smart Grind firmware"; aborts = 0;
    assert(refused.begin(0) && !feed(refused, firmware, 61));
    assert(flash.empty() && aborts == 1 && refused.image_refused());
    assert(std::string(refused.error()) == "Not Smart Grind firmware");
    firmware_image::verdict = nullptr;

    // An image too short to carry a header is refused when it ends.
    OtaWriter truncated; aborts = 0;
    assert(truncated.begin(0) && feed(truncated, image(200), 61) && !truncated.end());
    assert(flash.empty() && aborts == 1 && truncated.image_refused());

    // A flash write error is not a refusal, and a retry starts afresh.
    OtaWriter failing; write_result = ESP_FAIL;
    assert(failing.begin(0) && !feed(failing, firmware, 4096) && !failing.image_refused());
    write_result = ESP_OK;
    assert(failing.begin(0) && feed(failing, firmware, 4096) && failing.end() && flash == firmware);
}
'''


class OtaWriterTest(unittest.TestCase):
    def test_image_is_checked_before_it_reaches_flash(self):
        with tempfile.TemporaryDirectory() as folder:
            cpp, binary = Path(folder) / "writer.cpp", Path(folder) / "writer"
            cpp.write_text(HARNESS)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", str(cpp),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10, stdout=subprocess.DEVNULL)


if __name__ == "__main__":
    unittest.main()
