#include "ota_writer.h"

#include <esp_err.h>

#include <algorithm>
#include <cstring>

#include "../config/constants.h"

bool OtaWriter::fail(const char* reason) {
    error_ = reason;
    if (handle_ != 0) {
        esp_ota_abort(handle_);
        handle_ = 0;
    }
    return false;
}

bool OtaWriter::refuse(const char* reason) {
    image_refused_ = true;
    return fail(reason);
}

bool OtaWriter::begin(size_t expected_size) {
    if (handle_ != 0) return fail("An update is already in progress");

    partition_ = esp_ota_get_next_update_partition(nullptr);
    if (!partition_) return fail("No inactive firmware partition");
    if (expected_size > partition_->size) return fail("Firmware image is too large");

    written_ = 0;
    header_size_ = 0;
    image_refused_ = false;
    error_ = "";
    // Erase each sector as the image reaches it. Erasing the image's extent up
    // front blocks the caller for seconds, and for a browser upload that is the
    // HTTP server's only task.
    const esp_err_t err = esp_ota_begin(partition_, OTA_WITH_SEQUENTIAL_WRITES, &handle_);
    if (err != ESP_OK) {
        handle_ = 0;
        return fail(esp_err_to_name(err));
    }
    return true;
}

bool OtaWriter::write(const uint8_t* data, size_t length) {
    if (handle_ == 0) return fail("No update in progress");
    if (header_size_ < header_.size()) {
        const size_t taken = std::min(length, header_.size() - header_size_);
        memcpy(header_.data() + header_size_, data, taken);
        header_size_ += taken;
        data += taken;
        length -= taken;
        if (header_size_ < header_.size()) return true;
        if (const char* reason = firmware_image::check_header(header_.data())) return refuse(reason);
        if (!write_to_flash(header_.data(), header_.size())) return false;
    }
    return length == 0 || write_to_flash(data, length);
}

bool OtaWriter::write_to_flash(const uint8_t* data, size_t length) {
    if (written_ + length > partition_->size) return fail("Firmware image is too large");

    const esp_err_t err = esp_ota_write(handle_, data, length);
    if (err != ESP_OK) return fail(esp_err_to_name(err));
    written_ += length;
    return true;
}

bool OtaWriter::end() {
    if (handle_ == 0) return fail("No update in progress");
    if (header_size_ < header_.size()) return refuse("Firmware image is too short");

    const esp_ota_handle_t handle = handle_;
    handle_ = 0;
    const esp_err_t validated = esp_ota_end(handle);
    if (validated != ESP_OK) {
        error_ = validated == ESP_ERR_OTA_VALIDATE_FAILED ? "Image failed validation"
                                                          : esp_err_to_name(validated);
        return false;
    }

    const esp_err_t activated = esp_ota_set_boot_partition(partition_);
    if (activated != ESP_OK) {
        error_ = esp_err_to_name(activated);
        return false;
    }
    LOG_BLE("[OTA] Wrote %u bytes to %s\n", static_cast<unsigned>(written_),
            partition_->label);
    return true;
}

void OtaWriter::abort() {
    if (handle_ == 0) return;
    esp_ota_abort(handle_);
    handle_ = 0;
}
