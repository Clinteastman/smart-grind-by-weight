#pragma once

#include <esp_app_desc.h>
#include <esp_app_format.h>

#include <cstddef>
#include <cstdint>

//==============================================================================
// FIRMWARE IMAGE CHECKS
//==============================================================================
// Decide whether an application image may be installed before any of it is
// written to flash. The browser upload checks the start of its stream through
// OtaWriter; the release download checks the description esp_https_ota reads.

namespace firmware_image {

/** Bytes at the start of an image that check_header() reads. */
constexpr size_t HEADER_BYTES =
    sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t);

/**
 * Why an image's app description rules it out, or nullptr when it may be
 * installed: it must be Smart Grind firmware, and not the build the bootloader
 * last rolled back from.
 */
const char* check_description(const esp_app_desc_t& description);

/**
 * Why the first HEADER_BYTES of an image rule it out, or nullptr: it must be an
 * application image for this chip, its revision and flash mode, with a
 * description check_description() accepts.
 */
const char* check_header(const uint8_t* header);

}  // namespace firmware_image
