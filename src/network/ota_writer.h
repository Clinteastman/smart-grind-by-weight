#pragma once

#include <esp_ota_ops.h>

#include <array>
#include <cstddef>
#include <cstdint>

#include "firmware_image.h"

//==============================================================================
// WEB OTA IMAGE WRITER
//==============================================================================
// Streams a firmware image into the inactive app partition and marks it bootable
// once it validates. The browser upload streams its file through it, and the
// BLE path streams the image a delta patch produces; the GitHub release
// download goes through esp_https_ota, which checks the same description.

class OtaWriter {
public:
    /**
     * Open the inactive partition for writing.
     *
     * @param expected_size Image size when known, or 0 when it is not. Sizes
     *        larger than the partition are rejected. The partition is erased
     *        sector by sector as the image is written.
     */
    bool begin(size_t expected_size);

    /**
     * Append image bytes. Writes must arrive in order. The start of the image is
     * held back until firmware_image::check_header() accepts it, so a refused
     * image never reaches flash.
     */
    bool write(const uint8_t* data, size_t length);

    /** Validate the image and set it as the boot partition. */
    bool end();

    /** Discard a partially written image. */
    void abort();

    /** Human-readable reason the last operation failed. */
    const char* error() const { return error_; }

    /** True when the last failure refused the image itself rather than flash. */
    bool image_refused() const { return image_refused_; }

    /** The partition being written, once begin() has succeeded. */
    const esp_partition_t* partition() const { return partition_; }

private:
    bool fail(const char* reason);
    bool refuse(const char* reason);
    bool write_to_flash(const uint8_t* data, size_t length);

    esp_ota_handle_t handle_ = 0;
    const esp_partition_t* partition_ = nullptr;
    size_t written_ = 0;
    std::array<uint8_t, firmware_image::HEADER_BYTES> header_{};
    size_t header_size_ = 0;
    bool image_refused_ = false;
    const char* error_ = "";
};
