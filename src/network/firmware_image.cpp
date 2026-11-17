#include "firmware_image.h"

#include <esp_err.h>
#include <esp_ota_ops.h>

#include <cstring>

namespace firmware_image {
namespace {

// project_name of this firmware's images, and of 1.5.x and earlier, which the
// Arduino core built as its library-builder project. Accepting both keeps a
// browser downgrade to a 1.5.x release possible.
constexpr const char* kProjectNames[] = {"smart-grind-by-weight", "arduino-lib-builder"};

bool is_smart_grind(const esp_app_desc_t& description) {
    for (const char* name : kProjectNames) {
        if (strncmp(description.project_name, name, sizeof(description.project_name)) == 0) {
            return true;
        }
    }
    return false;
}

bool same_build(const esp_app_desc_t& first, const esp_app_desc_t& second) {
    return memcmp(first.app_elf_sha256, second.app_elf_sha256, sizeof(first.app_elf_sha256)) == 0;
}

}  // namespace

const char* check_description(const esp_app_desc_t& description) {
    if (description.magic_word != ESP_APP_DESC_MAGIC_WORD) {
        return "Not a valid ESP32 firmware image";
    }
    if (!is_smart_grind(description)) return "Not Smart Grind firmware";

    // The bootloader marks an image that never confirmed itself as invalid and
    // returns to the previous one; the same build would fail the same way.
    const esp_partition_t* rolled_back = esp_ota_get_last_invalid_partition();
    esp_app_desc_t rolled_back_description;
    if (rolled_back &&
        esp_ota_get_partition_description(rolled_back, &rolled_back_description) == ESP_OK &&
        same_build(description, rolled_back_description)) {
        return "This firmware failed to start and was rolled back";
    }
    return nullptr;
}

const char* check_header(const uint8_t* header) {
    esp_image_header_t image;
    esp_app_desc_t description;
    memcpy(&image, header, sizeof(image));
    memcpy(&description, header + sizeof(image) + sizeof(esp_image_segment_header_t),
           sizeof(description));
    if (image.magic != ESP_IMAGE_HEADER_MAGIC) return "Not a valid ESP32 firmware image";

    // The same chip, revision and flash-mode check esp_https_ota applies.
    switch (esp_ota_check_image_validity(ESP_PARTITION_TYPE_APP, &image, &description)) {
        case ESP_OK:
            break;
        case ESP_ERR_OTA_SPI_MODE_MISMATCH:
            return "Firmware is built for a different flash mode";
        default:
            return "Firmware is built for a different chip or chip revision";
    }
    return check_description(description);
}

}  // namespace firmware_image
