#include "ota_handler.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_delta_ota.h>
#include <esp_system.h>
#include "../config/build_info.h"
#include "../config/logging.h"
#include "../hardware/touch_driver.h"
#include "../hardware/hardware_manager.h"
#include "../network/ota_writer.h"
#include "../tasks/task_manager.h"
#include "../system/string_utils.h"
#include <sdkconfig.h>

extern HardwareManager hardware_manager;

namespace {

constexpr char kPatchPartitionLabel[] = "patch";
constexpr uint32_t kFlashSectorSize = 0x1000;
// Erasing the patch area in pieces keeps each flash operation short.
constexpr uint32_t kPatchEraseChunk = 256U * 1024U;
// The stored patch is fed to esp_delta_ota this many bytes at a time.
constexpr size_t kPatchReadChunk = 4096;

// Where esp_delta_ota reads the image a patch was made against and writes the
// image it produces.
struct DeltaTarget {
    // The running image, or nullptr for a full update, whose patch was made
    // against an empty image.
    const esp_partition_t* source;
    OtaWriter* image;
};

esp_err_t read_source(uint8_t* buffer, size_t size, int offset, void* user_data) {
    const auto* target = static_cast<const DeltaTarget*>(user_data);
    if (!target->source) {
        memset(buffer, 0, size);
        return ESP_OK;
    }
    return esp_partition_read(target->source, static_cast<size_t>(offset), buffer, size);
}

esp_err_t write_image(const uint8_t* buffer, size_t size, void* user_data) {
    return static_cast<DeltaTarget*>(user_data)->image->write(buffer, size) ? ESP_OK : ESP_FAIL;
}

}  // namespace

OTAHandler::OTAHandler() 
    : ota_in_progress(false)
    , patch_size(0)
    , received_size(0)
    , current_status(BLE_OTA_IDLE)
    , current_firmware_build_number("")
    , is_full_update(false) {
}

OTAHandler::~OTAHandler() {
    if (ota_in_progress) {
        abort_ota();
    }
}

void OTAHandler::init(Preferences* prefs) {
    preferences = prefs;
    LOG_BLE("OTA: Handler initialized\n");
    
    // Get current firmware build number
    current_firmware_build_number = std::to_string(BUILD_NUMBER);
}

bool OTAHandler::start_ota(uint32_t size, const std::string& expected_build_number,
                           bool is_full_update,
                           const std::string& expected_firmware_version) {
    LOG_OTA_DEBUG("start_ota() called - size=%lu, build=%s, full=%d\n", 
                  (unsigned long)size, expected_build_number.c_str(), is_full_update);
    
    if (ota_in_progress) {
        LOG_BLE("OTA: Update already in progress\n");
        LOG_OTA_DEBUG("start_ota() FAILED - already in progress\n");
        return false;
    }
    // The patch must fit the patch partition's whole sectors. Validate before
    // changing preferences, suspending tasks, or erasing any flash.
    const esp_partition_t* patch = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, kPatchPartitionLabel);
    if (size == 0 || !patch || size > (patch->size / kFlashSectorSize) * kFlashSectorSize) {
        current_status = BLE_OTA_ERROR;
        return false;
    }
    patch_partition = patch;
    
    const auto token = operation_interlock().try_acquire();
    if (!token) return false;
    operation_token = token;

    patch_size = size;
    received_size = 0;
    this->is_full_update = is_full_update;
    
    LOG_BLE("OTA: Starting %s update (%lu KB)\n", is_full_update ? "full" : "delta", (unsigned long)patch_size / 1024);
    LOG_OTA_DEBUG("patch_size=%lu, received_size=%lu, is_full_update=%d\n", 
                  (unsigned long)patch_size, (unsigned long)received_size, this->is_full_update);
    
    // Store expected build number and firmware version for post-reboot verification
    if (!expected_build_number.empty() && preferences) {
        preferences->putString("new_build_nr", expected_build_number);
        LOG_OTA_DEBUG("Stored expected build number: %s\n", expected_build_number.c_str());
    } else {
        LOG_OTA_DEBUG("No expected build number to store\n");
    }
    
    if (!expected_firmware_version.empty() && preferences) {
        preferences->putString("new_fw_ver", expected_firmware_version);
        LOG_OTA_DEBUG("Stored expected firmware version: %s\n", expected_firmware_version.c_str());
    } else if (expected_build_number.empty()) {
        LOG_OTA_DEBUG("No expected firmware version to store\n");
    }
    
    // Reconfigure task watchdog for OTA process with extended timeout
    // This is a CPU and flash-intensive operation that can starve other tasks
    LOG_BLE("OTA: Reconfiguring task watchdog timer for OTA process (1800s timeout)...\n");
    LOG_OTA_DEBUG("Configuring watchdog - timeout_ms=1800000, cores=0x3\n");
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = 1800000,
        .idle_core_mask = (1 << 0) | (1 << 1), // Watch idle tasks on both cores
        .trigger_panic = true,
    };
    if (esp_task_wdt_reconfigure(&wdt_config) != ESP_OK) {
        LOG_BLE("OTA: Unable to configure watchdog; update refused\n");
        recover_failed_update();
        return false;
    }
    watchdog_extended = true;
    LOG_OTA_DEBUG("Watchdog reconfigured successfully\n");

    // Suspend hardware tasks to prevent watchdog timeouts during OTA
    LOG_BLE("OTA: Suspending hardware tasks...\n");
    task_manager.suspend_hardware_tasks();
    hardware_suspended = true;

    LOG_OTA_DEBUG("Calling start_update()...\n");
    if (!start_update()) {
        current_status = BLE_OTA_ERROR;
        LOG_OTA_DEBUG("start_update() FAILED\n");
        
        // Resume hardware tasks on failure
        LOG_BLE("OTA: Resuming hardware tasks after failed start\n");
        recover_failed_update();
        return false;
    }
    LOG_OTA_DEBUG("start_update() SUCCESS\n");
    
    ota_in_progress = true;
    current_status = BLE_OTA_RECEIVING;
    LOG_OTA_DEBUG("OTA started successfully - status=BLE_OTA_RECEIVING\n");
    return true;
}

bool OTAHandler::process_data_chunk(const uint8_t* data, size_t size) {
    if (!ota_in_progress) {
        return false;
    }
    if (!data || size == 0 || size > patch_size - received_size) {
        LOG_BLE("OTA: Invalid or oversized data chunk\n");
        recover_failed_update();
        return false;
    }
    
    // Store the chunk in the patch partition; it is applied once complete.
    if (esp_partition_write(patch_partition, received_size, data, size) != ESP_OK) {
        LOG_BLE("OTA: Patch write failed at offset %lu\n", (unsigned long)received_size);
        current_status = BLE_OTA_ERROR;
        recover_failed_update();
        return false;
    }
    
    received_size += size;
    
    // Progress logging every 16KB for better visibility, plus at start and end
    if (received_size % 16384 == 0 || received_size == size || received_size == patch_size) {
        LOG_BLE("OTA: Transfer %lu KB / %lu KB (%.1f%%)\n", 
                     (unsigned long)received_size / 1024, (unsigned long)patch_size / 1024, 
                     get_progress());
    }
    
    return true;
}

bool OTAHandler::complete_ota() {
    LOG_OTA_DEBUG("complete_ota() called\n");
    
    if (!ota_in_progress) {
        LOG_BLE("OTA: No update in progress\n");
        LOG_OTA_DEBUG("complete_ota() FAILED - no update in progress\n");
        return false;
    }
    
    LOG_BLE("OTA: Finalizing update...\n");
    LOG_OTA_DEBUG("patch_size=%lu, received_size=%lu\n", 
                  (unsigned long)patch_size, (unsigned long)received_size);
    
    // Kamikaze mode: Disable all non-essential systems before flash operations
    LOG_BLE("OTA: Entering kamikaze mode - disabling non-essential systems...\n");
    LOG_OTA_DEBUG("Starting kamikaze mode shutdown sequence...\n");
    
    // Disable I2C operations (TouchDriver) - access through hardware_manager
    LOG_OTA_DEBUG("Disabling TouchDriver I2C operations...\n");
    hardware_manager.get_display()->get_touch_driver()->disable();
    touch_disabled = true;
    LOG_OTA_DEBUG("TouchDriver disabled\n");
    
    // Skip BLE deinitialization - causes hang in kamikaze mode
    // BLE stack will be destroyed during system restart anyway
    // NimBLEDevice::deinit(true);
    LOG_OTA_DEBUG("Skipping BLE deinit (causes hang) - kamikaze restart will handle cleanup\n");
    
    LOG_OTA_DEBUG("Calling finalize_update()...\n");
    bool success = finalize_update();
    if (success) {
        current_status = BLE_OTA_SUCCESS;
        LOG_OTA_DEBUG("finalize_update() SUCCESS\n");
        LOG_BLE("OTA: Update complete (%lu KB)\n", (unsigned long)received_size / 1024);
        LOG_BLE("OTA: Starting restart sequence...\n");
        
        // Restart device
        LOG_OTA_DEBUG("Flushing Serial before restart...\n");
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(100));
        
        // Kamikaze restart - no graceful cleanup needed
        LOG_BLE("OTA: Kamikaze restart in 3...2...1\n");
        LOG_OTA_DEBUG("Final countdown before esp_restart()...\n");
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(100));
        
        LOG_OTA_DEBUG("Calling esp_restart()...\n");
        fflush(stdout);
        esp_restart();
    } else {
        current_status = BLE_OTA_ERROR;
        LOG_BLE("OTA: Finalization failed\n");
        LOG_OTA_DEBUG("finalize_update() FAILED\n");

        // Resume hardware tasks on failure
        LOG_BLE("OTA: Resuming hardware tasks after failed finalization\n");
        recover_failed_update();
    }
    
    ota_in_progress = false;
    LOG_OTA_DEBUG("complete_ota() returning %s\n", success ? "SUCCESS" : "FAILED");
    return success;
}

void OTAHandler::abort_ota() {
    if (ota_in_progress) {
        LOG_BLE("OTA: Aborting update\n");
        recover_failed_update();
    }
}

void OTAHandler::recover_failed_update() {
    current_status = BLE_OTA_ERROR;
    // The SDK initializes the watchdog with these settings at boot. Restore
    // that exact policy, including which idle tasks are watched, on failure.
    if (watchdog_extended) {
        esp_task_wdt_config_t config{};
        config.timeout_ms = CONFIG_ESP_TASK_WDT_TIMEOUT_S * 1000U;
#if CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0
        config.idle_core_mask |= 1U;
#endif
#if CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1
        config.idle_core_mask |= 2U;
#endif
#if CONFIG_ESP_TASK_WDT_PANIC
        config.trigger_panic = true;
#endif
        if (esp_task_wdt_reconfigure(&config) != ESP_OK) {
            // Do not resume normal operation with weakened watchdog protection.
            LOG_BLE("OTA: Watchdog recovery failed; restarting\n");
            esp_restart();
            return;
        }
        watchdog_extended = false;
    }
    if (touch_disabled) {
        hardware_manager.get_display()->get_touch_driver()->enable();
        touch_disabled = false;
    }
    if (hardware_suspended) {
        task_manager.resume_hardware_tasks();
        hardware_suspended = false;
    }
    ota_in_progress = false;
    received_size = 0;
    patch_size = 0;
    if (preferences) {
        preferences->remove("new_build_nr");
        preferences->remove("new_fw_ver");
    }
    // A watchdog recovery that requires reboot deliberately keeps ownership.
    // Only successful recovery makes motor operations available again.
    operation_interlock().release(operation_token);
    operation_token = 0;
}

float OTAHandler::get_progress() const {
    if (patch_size == 0) return 0.0f;
    return 100.0f * received_size / patch_size;
}

bool OTAHandler::start_update() {
    // Erase the sectors the patch will occupy.
    const uint32_t erase_size =
        ((patch_size + kFlashSectorSize - 1) / kFlashSectorSize) * kFlashSectorSize;
    for (uint32_t erased = 0; erased < erase_size; erased += kPatchEraseChunk) {
        const uint32_t chunk = std::min(kPatchEraseChunk, erase_size - erased);
        if (esp_partition_erase_range(patch_partition, erased, chunk) != ESP_OK) {
            LOG_BLE("OTA: Failed to erase the patch partition\n");
            return false;
        }
    }
    return true;
}

bool OTAHandler::finalize_update() {
    LOG_OTA_DEBUG("finalize_update() called\n");
    
    // Verify received size matches expected
    LOG_OTA_DEBUG("Verifying received size: expected=%lu, got=%lu\n", 
                  (unsigned long)patch_size, (unsigned long)received_size);
    if (received_size != patch_size) {
        LOG_BLE("OTA: Size mismatch - expected %lu, got %lu\n", 
                     (unsigned long)patch_size, (unsigned long)received_size);
        LOG_OTA_DEBUG("Size verification FAILED\n");
        return false;
    }
    LOG_OTA_DEBUG("Size verification SUCCESS\n");
    
    // A/B Partition Update Logic
    LOG_OTA_DEBUG("Getting running partition...\n");
    const esp_partition_t* running_partition = esp_ota_get_running_partition();
    if (!running_partition) {
        LOG_BLE("❌ Could not get running partition!\n");
        LOG_OTA_DEBUG("esp_ota_get_running_partition() FAILED\n");
        return false;
    }
    LOG_OTA_DEBUG("Running partition: %s (addr=0x%lx, size=%lu)\n", 
                  running_partition->label, (unsigned long)running_partition->address, 
                  (unsigned long)running_partition->size);

    // esp_delta_ota rebuilds the new image from the running one and the stored
    // patch. OtaWriter streams it into the inactive partition and applies the
    // same admission check as a browser upload before any of it reaches flash.
    OtaWriter writer;
    if (!writer.begin(0)) {
        LOG_BLE("OTA: Could not open the inactive partition: %s\n", writer.error());
        return false;
    }
    LOG_BLE("OTA Info: Running from '%s', updating to '%s'\n",
                  running_partition->label, writer.partition()->label);

    DeltaTarget target{is_full_update ? nullptr : running_partition, &writer};
    esp_delta_ota_cfg_t delta_config = {};
    delta_config.user_data = &target;
    delta_config.read_cb_with_user_data = read_source;
    delta_config.write_cb_with_user_data = write_image;
    esp_delta_ota_handle_t delta = esp_delta_ota_init(&delta_config);
    const std::unique_ptr<uint8_t[]> chunk(new (std::nothrow) uint8_t[kPatchReadChunk]);
    bool applied = delta && chunk;
    for (uint32_t offset = 0; applied && offset < patch_size; offset += kPatchReadChunk) {
        const size_t length = std::min<size_t>(kPatchReadChunk, patch_size - offset);
        applied = esp_partition_read(patch_partition, offset, chunk.get(), length) == ESP_OK &&
                  esp_delta_ota_feed_patch(delta, chunk.get(), static_cast<int>(length)) == ESP_OK;
    }
    applied = applied && esp_delta_ota_finalize(delta) == ESP_OK;
    if (delta) esp_delta_ota_deinit(delta);
    if (!applied) {
        // The writer names an image it refused; a patch or flash failure has
        // no message of its own.
        LOG_BLE("OTA: Delta patch could not be applied%s%s\n",
                *writer.error() ? ": " : "", writer.error());
        writer.abort();
        return false;
    }

    // end() validates the image the patch produced and makes it bootable.
    if (!writer.end()) {
        LOG_BLE("OTA: Patched image was not accepted: %s\n", writer.error());
        return false;
    }

    LOG_OTA_DEBUG("finalize_update() SUCCESS - delta patch applied\n");
    return true;
}

std::string OTAHandler::check_ota_failure_after_boot() {
    if (!preferences) {
        return "";
    }

    const std::string expected_build = preferences->getString("new_build_nr", "");
    const std::string expected_version = preferences->getString("new_fw_ver", "");

    if (expected_build.empty() && expected_version.empty()) {
        return "";
    }

    int current_build = BUILD_NUMBER;
    const std::string current_version = BUILD_FIRMWARE_VERSION;

    // Web flasher sends firmware version - use that for verification (more reliable)
    if (!expected_version.empty()) {
        if (expected_version != current_version) {
            LOG_BLE("OTA: Version check failed - expected v%s, got v%s\n",
                         expected_version.c_str(), current_version.c_str());
            preferences->remove("new_build_nr");
            preferences->remove("new_fw_ver");
            return expected_version;  // Return expected version for display
        } else {
            LOG_BLE("OTA: Version check passed - expected v%s, got v%s\n",
                         expected_version.c_str(), current_version.c_str());
            preferences->remove("new_build_nr");
            preferences->remove("new_fw_ver");
            return "";
        }
    }

    // Python flasher sends build number only - use that for verification
    if (!expected_build.empty()) {
        const int expected_build_num = strings::to_int32(expected_build);
        if (current_build != expected_build_num) {
            LOG_BLE("OTA: Build number check failed - expected #%d, got #%d\n",
                         expected_build_num, current_build);
            preferences->remove("new_build_nr");
            preferences->remove("new_fw_ver");
            return expected_build;
        } else {
            LOG_BLE("OTA: Build number check passed - expected #%d, got #%d\n",
                         expected_build_num, current_build);
            preferences->remove("new_build_nr");
            preferences->remove("new_fw_ver");
            return "";
        }
    }

    // Clean up if we get here (no verification data)
    preferences->remove("new_build_nr");
    preferences->remove("new_fw_ver");
    return "";
}
